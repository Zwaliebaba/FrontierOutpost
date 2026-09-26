// NeuronClient/Buffer.cpp
#include "pch.h"

#include "Buffer.h"
#include "GraphicsCore.h"
#include "Unicode.h"

#include <exception>
#include <format>
#include <utility>

namespace Neuron
{

Buffer Buffer::Make(const std::shared_ptr<GraphicsCore>& _core, const Desc& _desc)
{
  Buffer buffer;
  GraphicsCore& core = *_core;
  if (_desc.sizeBytes == 0)
  {
    core.Fail(std::format("Direct3D 12: buffer {} cannot be empty", _desc.name));
    return buffer;
  }
  auto native = std::make_unique<Native>();
  native->desc = _desc;
  native->desc.name = {};
  native->name = std::string(_desc.name);
  // A buffer is made in COMMON whatever state is asked for.
  const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC resourceDesc = BufferDescription(_desc.sizeBytes);
  if (!core.Check(core.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                       IID_PPV_ARGS(&native->resource)),
                  std::format("ID3D12Device::CreateCommittedResource for buffer {}", native->name)))
  {
    return buffer;
  }
  native->resource->SetName(Utf8ToUtf16(native->name).c_str());

  buffer.m_core = _core;
  buffer.m_native = std::move(native);
  return buffer;
}

Buffer::Buffer() noexcept = default;

Buffer::~Buffer()
{
  Release();
}

Buffer::Buffer(Buffer&& _other) noexcept = default;

Buffer& Buffer::operator=(Buffer&& _other) noexcept
{
  if (this != &_other)
  {
    Release();
    m_core = std::move(_other.m_core);
    m_native = std::move(_other.m_native);
  }
  return *this;
}

/// As Texture::Release: the release holds the buffer's objects and not the core.
void Buffer::Release() noexcept
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

Buffer::operator bool() const noexcept
{
  return m_native != nullptr;
}

std::uint32_t Buffer::SizeBytes() const noexcept
{
  return m_native ? m_native->desc.sizeBytes : 0;
}

std::uint32_t Buffer::StrideBytes() const noexcept
{
  return m_native ? m_native->desc.strideBytes : 0;
}

} // namespace Neuron
