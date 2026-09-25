// NeuronClient/Texture.cpp
#include "pch.h"

#include "GraphicsCore.h"
#include "Texture.h"
#include "Unicode.h"

#include <algorithm>
#include <bit>
#include <exception>
#include <format>
#include <utility>

namespace Neuron
{

namespace
{

std::uint32_t MipSize(std::uint32_t _sizePixels, std::uint32_t _mip) noexcept
{
  return _mip < 32 ? std::max<std::uint32_t>(_sizePixels >> _mip, 1) : 1;
}

} // namespace

std::uint32_t TexelBytes(TextureFormat _format) noexcept
{
  switch (_format)
  {
  case TextureFormat::R8:
    return 1;
  case TextureFormat::Rg8:
    return 2;
  case TextureFormat::Rgba8:
    return 4;
  case TextureFormat::R16F:
    return 2;
  case TextureFormat::Rgba16F:
    return 8;
  case TextureFormat::R32F:
    return 4;
  case TextureFormat::Rgba32F:
    return 16;
  case TextureFormat::Depth32F:
    return 4;
  }
  return 0;
}

DXGI_FORMAT ResourceFormat(TextureFormat _format) noexcept
{
  switch (_format)
  {
  case TextureFormat::R8:
    return DXGI_FORMAT_R8_UNORM;
  case TextureFormat::Rg8:
    return DXGI_FORMAT_R8G8_UNORM;
  case TextureFormat::Rgba8:
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  case TextureFormat::R16F:
    return DXGI_FORMAT_R16_FLOAT;
  case TextureFormat::Rgba16F:
    return DXGI_FORMAT_R16G16B16A16_FLOAT;
  case TextureFormat::R32F:
    return DXGI_FORMAT_R32_FLOAT;
  case TextureFormat::Rgba32F:
    return DXGI_FORMAT_R32G32B32A32_FLOAT;
  case TextureFormat::Depth32F:
    return DXGI_FORMAT_R32_TYPELESS;
  }
  return DXGI_FORMAT_UNKNOWN;
}

std::uint32_t Texture::FullMipLevels(std::uint32_t _widthPixels, std::uint32_t _heightPixels, std::uint32_t _depthPixels) noexcept
{
  const std::uint32_t largest = std::max({_widthPixels, _heightPixels, _depthPixels, 1u});
  return static_cast<std::uint32_t>(std::bit_width(largest));
}

Texture Texture::Make(const std::shared_ptr<GraphicsCore>& _core, const Desc& _desc)
{
  Texture texture;
  GraphicsCore& core = *_core;
  const bool cube = _desc.dimension == TextureDimension::TextureCube;
  const bool volume = _desc.dimension == TextureDimension::Texture3D;
  const bool depth = _desc.format == TextureFormat::Depth32F;
  const std::uint32_t slices = volume ? _desc.depthPixels : 1;
  const std::uint32_t faces = cube ? 6 : 1;
  if (_desc.widthPixels == 0 || _desc.heightPixels == 0 || slices == 0 || _desc.mipLevels == 0 ||
      _desc.mipLevels > FullMipLevels(_desc.widthPixels, _desc.heightPixels, slices) || (cube && _desc.widthPixels != _desc.heightPixels) ||
      (depth && (_desc.dimension != TextureDimension::Texture2D || _desc.mipLevels != 1)))
  {
    core.Fail(std::format("Direct3D 12: texture {} cannot be {} by {} by {} with {} mip levels", _desc.name, _desc.widthPixels,
                          _desc.heightPixels, slices, _desc.mipLevels));
    return texture;
  }

  auto native = std::make_unique<Native>();
  native->core = _core.get();
  native->desc = _desc;
  native->desc.depthPixels = slices;
  native->desc.name = {};
  native->name = std::string(_desc.name);
  native->faces = faces;
  D3D12_RESOURCE_DESC& resourceDesc = native->resourceDesc;
  resourceDesc.Dimension = volume ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resourceDesc.Width = _desc.widthPixels;
  resourceDesc.Height = _desc.heightPixels;
  resourceDesc.DepthOrArraySize = static_cast<UINT16>(volume ? slices : faces);
  resourceDesc.MipLevels = static_cast<UINT16>(_desc.mipLevels);
  resourceDesc.Format = ResourceFormat(_desc.format);
  resourceDesc.SampleDesc.Count = 1;
  resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  if (depth)
  {
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  }
  else
  {
    // liblt may bind any colour texture as a target; mips are made, and 3D fields written, by
    // compute shaders.
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (_desc.mipLevels > 1 || volume)
    {
      resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
  }

  const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  if (!core.Check(core.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                       IID_PPV_ARGS(&native->resource)),
                  std::format("ID3D12Device::CreateCommittedResource for texture {}", native->name)))
  {
    return texture;
  }
  native->resource->SetName(Utf8ToUtf16(native->name).c_str());
  const std::size_t subresources = static_cast<std::size_t>(_desc.mipLevels) * faces;
  native->states.assign(subresources, D3D12_RESOURCE_STATE_COMMON);
  native->copiedLists.assign(subresources, 0);

  texture.m_core = _core;
  texture.m_native = std::move(native);
  return texture;
}

/// An exception cannot be reported from here, so one, which can only be memory running out while
/// a view is handed back, ends the program.
Texture::Native::~Native()
{
  if (core == nullptr)
  {
    return;
  }
  try
  {
    for (const auto& [key, view] : targetViews)
    {
      core->targetViewPool.Free(view);
    }
    if (depthView.ptr != 0)
    {
      core->depthViewPool.Free(depthView);
    }
    if (shaderView.ptr != 0)
    {
      core->shaderViewPool.Free(shaderView);
    }
    for (const auto& [mip, view] : unorderedViews)
    {
      core->shaderViewPool.Free(view);
    }
  }
  catch (...)
  {
    std::terminate();
  }
}

bool Texture::Native::TargetView(std::uint32_t _mip, std::uint32_t _layer, D3D12_CPU_DESCRIPTOR_HANDLE& _outView)
{
  const std::uint64_t key = (std::uint64_t{_mip} << 32) | _layer;
  if (const auto found = targetViews.find(key); found != targetViews.end())
  {
    _outView = found->second;
    return true;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE view{};
  if (!core->targetViewPool.Allocate(*core, view))
  {
    return false;
  }
  D3D12_RENDER_TARGET_VIEW_DESC viewDesc{};
  viewDesc.Format = resourceDesc.Format;
  switch (desc.dimension)
  {
  case TextureDimension::Texture2D:
    viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    viewDesc.Texture2D.MipSlice = _mip;
    break;
  case TextureDimension::TextureCube:
    viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
    viewDesc.Texture2DArray.MipSlice = _mip;
    viewDesc.Texture2DArray.FirstArraySlice = _layer;
    viewDesc.Texture2DArray.ArraySize = 1;
    break;
  case TextureDimension::Texture3D:
    viewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
    viewDesc.Texture3D.MipSlice = _mip;
    viewDesc.Texture3D.FirstWSlice = _layer;
    viewDesc.Texture3D.WSize = 1;
    break;
  }
  core->device->CreateRenderTargetView(resource.Get(), &viewDesc, view);
  targetViews.emplace(key, view);
  _outView = view;
  return true;
}

bool Texture::Native::DepthView(D3D12_CPU_DESCRIPTOR_HANDLE& _outView)
{
  if (depthView.ptr == 0)
  {
    if (!core->depthViewPool.Allocate(*core, depthView))
    {
      return false;
    }
    D3D12_DEPTH_STENCIL_VIEW_DESC viewDesc{};
    viewDesc.Format = DXGI_FORMAT_D32_FLOAT;
    viewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    core->device->CreateDepthStencilView(resource.Get(), &viewDesc, depthView);
  }
  _outView = depthView;
  return true;
}

bool Texture::Native::ShaderView(D3D12_CPU_DESCRIPTOR_HANDLE& _outView)
{
  if (shaderView.ptr == 0)
  {
    if (!core->shaderViewPool.Allocate(*core, shaderView))
    {
      return false;
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format = desc.format == TextureFormat::Depth32F ? DXGI_FORMAT_R32_FLOAT : resourceDesc.Format;
    viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    switch (desc.dimension)
    {
    case TextureDimension::Texture2D:
      viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      viewDesc.Texture2D.MipLevels = desc.mipLevels;
      break;
    case TextureDimension::TextureCube:
      viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      viewDesc.TextureCube.MipLevels = desc.mipLevels;
      break;
    case TextureDimension::Texture3D:
      viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      viewDesc.Texture3D.MipLevels = desc.mipLevels;
      break;
    }
    core->device->CreateShaderResourceView(resource.Get(), &viewDesc, shaderView);
  }
  _outView = shaderView;
  return true;
}

bool Texture::Native::UnorderedView(std::uint32_t _mip, D3D12_CPU_DESCRIPTOR_HANDLE& _outView)
{
  if (const auto found = unorderedViews.find(_mip); found != unorderedViews.end())
  {
    _outView = found->second;
    return true;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE view{};
  if (!core->shaderViewPool.Allocate(*core, view))
  {
    return false;
  }
  D3D12_UNORDERED_ACCESS_VIEW_DESC viewDesc{};
  viewDesc.Format = resourceDesc.Format;
  switch (desc.dimension)
  {
  case TextureDimension::Texture2D:
    viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    viewDesc.Texture2D.MipSlice = _mip;
    break;
  case TextureDimension::TextureCube:
    viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    viewDesc.Texture2DArray.MipSlice = _mip;
    viewDesc.Texture2DArray.ArraySize = faces;
    break;
  case TextureDimension::Texture3D:
    viewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    viewDesc.Texture3D.MipSlice = _mip;
    viewDesc.Texture3D.WSize = MipSize(desc.depthPixels, _mip);
    break;
  }
  core->device->CreateUnorderedAccessView(resource.Get(), nullptr, &viewDesc, view);
  unorderedViews.emplace(_mip, view);
  _outView = view;
  return true;
}

Texture::Texture() noexcept = default;

Texture::~Texture()
{
  Release();
}

Texture::Texture(Texture&& _other) noexcept = default;

Texture& Texture::operator=(Texture&& _other) noexcept
{
  if (this != &_other)
  {
    Release();
    m_core = std::move(_other.m_core);
    m_native = std::move(_other.m_native);
  }
  return *this;
}

/// The resource goes once the GPU has finished with what was recorded so far, and stops being a
/// target now. The release holds the texture's objects and not the core, which runs it. An
/// exception cannot be reported from here, so one, which can only be memory running out, ends the
/// program.
void Texture::Release() noexcept
{
  if (!m_native)
  {
    return;
  }
  m_core->ForgetTexture(*m_native);
  try
  {
    m_core->DeferRelease([native = std::shared_ptr<Native>(std::move(m_native))] {});
  }
  catch (...)
  {
    std::terminate();
  }
  m_core.reset();
}

Texture::operator bool() const noexcept
{
  return m_native != nullptr;
}

TextureDimension Texture::Dimension() const noexcept
{
  return m_native ? m_native->desc.dimension : TextureDimension::Texture2D;
}

TextureFormat Texture::Format() const noexcept
{
  return m_native ? m_native->desc.format : TextureFormat::Rgba8;
}

std::uint32_t Texture::MipLevels() const noexcept
{
  return m_native ? m_native->desc.mipLevels : 0;
}

std::uint32_t Texture::Faces() const noexcept
{
  return m_native ? m_native->faces : 0;
}

std::uint32_t Texture::WidthPixels(std::uint32_t _mip) const noexcept
{
  return m_native ? MipSize(m_native->desc.widthPixels, _mip) : 0;
}

std::uint32_t Texture::HeightPixels(std::uint32_t _mip) const noexcept
{
  return m_native ? MipSize(m_native->desc.heightPixels, _mip) : 0;
}

std::uint32_t Texture::DepthPixels(std::uint32_t _mip) const noexcept
{
  return m_native ? MipSize(m_native->desc.depthPixels, _mip) : 0;
}

} // namespace Neuron
