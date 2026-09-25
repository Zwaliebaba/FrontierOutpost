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
      (depth && _desc.dimension != TextureDimension::Texture2D))
  {
    core.Fail(std::format("Direct3D 12: texture {} cannot be {} by {} by {} with {} mip levels", _desc.name, _desc.widthPixels,
                          _desc.heightPixels, slices, _desc.mipLevels));
    return texture;
  }

  auto native = std::make_unique<Native>();
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

/// The resource goes once the GPU has finished with what was recorded so far. The release holds
/// the texture's objects and not the core, which runs it. An exception cannot be reported from
/// here, so one, which can only be memory running out, ends the program.
void Texture::Release() noexcept
{
  if (!m_native)
  {
    return;
  }
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
