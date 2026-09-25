// NeuronClient/DrawContext.cpp
#include "pch.h"

#include "DrawContext.h"
#include "GraphicsCore.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

/// Uploads of a buffer's bytes need no alignment; this keeps them on cache lines' boundaries.
constexpr std::uint64_t BUFFER_UPLOAD_ALIGNMENT_BYTES = 16;

std::uint64_t AlignUp(std::uint64_t _value, std::uint64_t _alignment) noexcept
{
  return (_value + _alignment - 1) / _alignment * _alignment;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* _resource, std::uint32_t _subresource, D3D12_RESOURCE_STATES _before,
                                  D3D12_RESOURCE_STATES _after) noexcept
{
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = _resource;
  barrier.Transition.Subresource = _subresource;
  barrier.Transition.StateBefore = _before;
  barrier.Transition.StateAfter = _after;
  return barrier;
}

D3D12_TEXTURE_COPY_LOCATION SubresourceLocation(ID3D12Resource* _texture, std::uint32_t _subresource) noexcept
{
  D3D12_TEXTURE_COPY_LOCATION location{};
  location.pResource = _texture;
  location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  location.SubresourceIndex = _subresource;
  return location;
}

D3D12_TEXTURE_COPY_LOCATION FootprintLocation(ID3D12Resource* _buffer, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& _footprint) noexcept
{
  D3D12_TEXTURE_COPY_LOCATION location{};
  location.pResource = _buffer;
  location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  location.PlacedFootprint = _footprint;
  return location;
}

/// Where an upload's bytes go: a page of the ring, or a staging buffer of its own.
struct Upload
{
  ID3D12Resource* buffer = nullptr;
  std::uint64_t offsetBytes = 0;
  std::byte* cpu = nullptr;
};

/// How a subresource is laid out in a buffer: its rows padded to the copy pitch.
struct Footprint
{
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed{};
  std::uint32_t rows = 0;       // per slice
  std::uint64_t rowBytes = 0;   // of texels, without the padding
  std::uint64_t totalBytes = 0; // of the whole subresource in the buffer
};

/// Copies _slices slices of _rows rows of _rowBytes between two layouts, each given by its row
/// pitch and its rows per slice.
void CopyRows(std::byte* _to, std::uint64_t _toPitch, std::uint64_t _toRows, const std::byte* _from, std::uint64_t _fromPitch,
              std::uint64_t _fromRows, std::uint64_t _rowBytes, std::uint32_t _rows, std::uint32_t _slices) noexcept
{
  for (std::uint64_t slice = 0; slice < _slices; ++slice)
  {
    for (std::uint64_t row = 0; row < _rows; ++row)
    {
      std::memcpy(_to + (((slice * _toRows) + row) * _toPitch), _from + (((slice * _fromRows) + row) * _fromPitch), _rowBytes);
    }
  }
}

} // namespace

struct DrawContext::Native
{
  struct Allocator
  {
    ComPtr<ID3D12CommandAllocator> allocator;
    std::uint64_t fenceValue; // the allocator is free once the fence reaches this
  };

  /// A page of the upload ring, mapped for as long as it lives. The CPU only writes it.
  struct Page
  {
    ComPtr<ID3D12Resource> buffer;
    std::byte* cpu = nullptr;
    std::uint64_t usedBytes = 0;
    std::uint64_t fenceValue = 0; // the page is free once the fence reaches this
  };

  GraphicsCore& core;
  ComPtr<ID3D12GraphicsCommandList> list;
  ComPtr<ID3D12CommandAllocator> allocator;     // the open list's
  std::deque<Allocator> allocators;             // submitted, oldest first
  Page page;                                    // where uploads are taken from now
  std::vector<Page> fullPages;                  // pages the open list filled
  std::deque<Page> pages;                       // submitted pages, oldest first
  std::vector<ComPtr<ID3D12Resource>> staging;  // the open list's own staging buffers
  std::vector<D3D12_RESOURCE_BARRIER> barriers; // waiting to be recorded
  std::uint64_t listSerial = 1;                 // counts command lists: a buffer's state is from one of them
  bool open = false;

  explicit Native(GraphicsCore& _core)
    : core(_core)
  {
  }

  /// Opens a command list, on an allocator the GPU has finished with or a new one.
  bool Open()
  {
    if (open)
    {
      return true;
    }
    ComPtr<ID3D12CommandAllocator> next;
    if (!allocators.empty() && allocators.front().fenceValue <= core.Completed())
    {
      next = std::move(allocators.front().allocator);
      allocators.pop_front();
      if (!core.Check(next->Reset(), "ID3D12CommandAllocator::Reset"))
      {
        return false;
      }
    }
    else if (!core.Check(core.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&next)),
                         "ID3D12Device::CreateCommandAllocator"))
    {
      return false;
    }
    if (!list)
    {
      if (!core.Check(core.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, next.Get(), nullptr, IID_PPV_ARGS(&list)),
                      "ID3D12Device::CreateCommandList"))
      {
        return false;
      }
      list->SetName(L"NeuronClient command list");
    }
    else if (!core.Check(list->Reset(next.Get(), nullptr), "ID3D12GraphicsCommandList::Reset"))
    {
      return false;
    }
    allocator = std::move(next);
    open = true;
    return true;
  }

  /// Submits the open list, if there is one, and marks the queue after it. What the list used is
  /// free, or released, once the fence reaches that mark.
  void Submit()
  {
    if (open)
    {
      FlushBarriers();
      if (core.Check(list->Close(), "ID3D12GraphicsCommandList::Close"))
      {
        const std::array<ID3D12CommandList*, 1> lists = {list.Get()};
        core.queue->ExecuteCommandLists(static_cast<UINT>(lists.size()), lists.data());
      }
      open = false;
      ++listSerial;
    }
    // Deferred before the signal, so that they wait for it.
    for (ComPtr<ID3D12Resource>& buffer : staging)
    {
      core.DeferRelease([held = std::move(buffer)] {});
    }
    staging.clear();
    const std::uint64_t value = core.Signal();
    if (allocator)
    {
      allocators.push_back({std::move(allocator), value});
    }
    for (Page& full : fullPages)
    {
      full.fenceValue = value;
      pages.push_back(std::move(full));
    }
    fullPages.clear();
    if (page.buffer)
    {
      page.fenceValue = value;
      pages.push_back(std::move(page));
      page = Page{};
    }
  }

  void FlushBarriers()
  {
    if (!barriers.empty())
    {
      list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
      barriers.clear();
    }
  }

  /// Records what brings subresource _subresource of _texture, or all of them for
  /// D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, to _state: one barrier when they all share a state.
  void Require(Texture::Native& _texture, std::uint32_t _subresource, D3D12_RESOURCE_STATES _state)
  {
    std::vector<D3D12_RESOURCE_STATES>& states = _texture.states;
    if (_subresource != D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES)
    {
      D3D12_RESOURCE_STATES& current = states[_subresource];
      if (current != _state)
      {
        barriers.push_back(Transition(_texture.resource.Get(), _subresource, current, _state));
        current = _state;
      }
      return;
    }
    const D3D12_RESOURCE_STATES first = states.front();
    if (std::ranges::all_of(states, [first](D3D12_RESOURCE_STATES _each) { return _each == first; }))
    {
      if (first != _state)
      {
        barriers.push_back(Transition(_texture.resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, first, _state));
        std::ranges::fill(states, _state);
      }
      return;
    }
    for (std::size_t index = 0; index < states.size(); ++index)
    {
      if (states[index] != _state)
      {
        barriers.push_back(Transition(_texture.resource.Get(), static_cast<std::uint32_t>(index), states[index], _state));
        states[index] = _state;
      }
    }
  }

  /// Records what brings _buffer to _state. A buffer decays to COMMON when the command list that
  /// used it finishes, so a state from an earlier list is COMMON now.
  void Require(Buffer::Native& _buffer, D3D12_RESOURCE_STATES _state)
  {
    if (_buffer.stateList != listSerial)
    {
      _buffer.state = D3D12_RESOURCE_STATE_COMMON;
      _buffer.stateList = listSerial;
    }
    if (_buffer.state != _state)
    {
      barriers.push_back(Transition(_buffer.resource.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, _buffer.state, _state));
      _buffer.state = _state;
    }
  }

  /// Readies a subresource to be copied into. Copies are not ordered among themselves, so a second
  /// copy into it in the same list first waits for the one before, through a transition out of
  /// COPY_DEST and back. The two go in ResourceBarrier calls of their own: two transitions of one
  /// subresource in one call are the debug layer's warning 1008.
  void RequireCopyDestination(Texture::Native& _texture, std::uint32_t _subresource)
  {
    if (_texture.copiedLists[_subresource] == listSerial && _texture.states[_subresource] == D3D12_RESOURCE_STATE_COPY_DEST)
    {
      Require(_texture, _subresource, D3D12_RESOURCE_STATE_COMMON);
      FlushBarriers();
    }
    Require(_texture, _subresource, D3D12_RESOURCE_STATE_COPY_DEST);
    _texture.copiedLists[_subresource] = listSerial;
  }

  void RequireCopyDestination(Buffer::Native& _buffer)
  {
    if (_buffer.copiedList == listSerial && _buffer.stateList == listSerial && _buffer.state == D3D12_RESOURCE_STATE_COPY_DEST)
    {
      Require(_buffer, D3D12_RESOURCE_STATE_COMMON);
      FlushBarriers();
    }
    Require(_buffer, D3D12_RESOURCE_STATE_COPY_DEST);
    _buffer.copiedList = listSerial;
  }

  bool CreateBuffer(D3D12_HEAP_TYPE _heap, D3D12_RESOURCE_STATES _state, std::uint64_t _sizeBytes, ComPtr<ID3D12Resource>& _outBuffer,
                    std::string_view _what)
  {
    const D3D12_HEAP_PROPERTIES heap = HeapProperties(_heap);
    const D3D12_RESOURCE_DESC desc = BufferDescription(_sizeBytes);
    return core.Check(core.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, _state, nullptr, IID_PPV_ARGS(&_outBuffer)),
                      std::format("ID3D12Device::CreateCommittedResource for {}", _what));
  }

  /// An upload buffer, mapped for as long as it lives.
  bool CreateUploadBuffer(std::uint64_t _sizeBytes, ComPtr<ID3D12Resource>& _outBuffer, std::byte*& _outCpu)
  {
    if (!CreateBuffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, _sizeBytes, _outBuffer, "an upload buffer"))
    {
      return false;
    }
    const D3D12_RANGE nothingRead{0, 0};
    void* mapped = nullptr;
    if (!core.Check(_outBuffer->Map(0, &nothingRead, &mapped), "ID3D12Resource::Map for an upload buffer"))
    {
      return false;
    }
    _outCpu = static_cast<std::byte*>(mapped);
    return true;
  }

  /// The next page of the ring: the oldest the GPU has finished with, or a new one.
  bool NextPage()
  {
    if (!pages.empty() && pages.front().fenceValue <= core.Completed())
    {
      page = std::move(pages.front());
      pages.pop_front();
      page.usedBytes = 0;
      return true;
    }
    Page fresh;
    if (!CreateUploadBuffer(core.desc.uploadPageBytes, fresh.buffer, fresh.cpu))
    {
      return false;
    }
    fresh.buffer->SetName(L"NeuronClient upload page");
    page = std::move(fresh);
    return true;
  }

  /// _sizeBytes of upload memory, _alignment-aligned, for the open list: from the ring, or from
  /// a staging buffer of its own when it would not fit in a page.
  bool AllocateUpload(std::uint64_t _sizeBytes, std::uint64_t _alignment, Upload& _outUpload)
  {
    const std::uint64_t pageBytes = core.desc.uploadPageBytes;
    if (_sizeBytes > pageBytes)
    {
      ComPtr<ID3D12Resource> buffer;
      std::byte* cpu = nullptr;
      if (!CreateUploadBuffer(_sizeBytes, buffer, cpu))
      {
        return false;
      }
      buffer->SetName(L"NeuronClient staging buffer");
      _outUpload = {buffer.Get(), 0, cpu};
      staging.push_back(std::move(buffer));
      return true;
    }
    std::uint64_t offset = AlignUp(page.usedBytes, _alignment);
    if (!page.buffer || offset + _sizeBytes > pageBytes)
    {
      if (page.buffer)
      {
        fullPages.push_back(std::move(page));
        page = Page{};
      }
      if (!NextPage())
      {
        return false;
      }
      offset = 0;
    }
    page.usedBytes = offset + _sizeBytes;
    _outUpload = {page.buffer.Get(), offset, page.cpu + offset};
    return true;
  }

  Footprint FootprintOf(const Texture::Native& _texture, std::uint32_t _subresource) const
  {
    Footprint footprint;
    UINT rows = 0;
    UINT64 rowBytes = 0;
    UINT64 totalBytes = 0;
    core.device->GetCopyableFootprints(&_texture.resourceDesc, _subresource, 1, 0, &footprint.placed, &rows, &rowBytes, &totalBytes);
    footprint.rows = rows;
    footprint.rowBytes = rowBytes;
    footprint.totalBytes = totalBytes;
    return footprint;
  }

  /// Submits, waits for the GPU, and returns whether the device is still there to be read from.
  bool SubmitAndWait()
  {
    Submit();
    core.WaitFor(core.lastSignaled);
    core.Retire();
    return !core.removed;
  }
};

DrawContext::DrawContext(GraphicsCore& _core)
  : m_native(std::make_unique<Native>(_core))
{
}

DrawContext::~DrawContext() = default;

void DrawContext::UpdateTexture(Texture& _texture, std::uint32_t _mip, std::uint32_t _face, std::span<const std::byte> _texels)
{
  Native& context = *m_native;
  Texture::Native* const texture = _texture.m_native.get();
  if (texture == nullptr || texture->desc.format == TextureFormat::Depth32F || _mip >= texture->desc.mipLevels || _face >= texture->faces)
  {
    context.core.Fail(std::format("Direct3D 12: UpdateTexture cannot fill mip {} of face {} of {}", _mip, _face,
                                  texture != nullptr ? texture->name : std::string("an empty texture")));
    return;
  }
  const std::uint32_t height = _texture.HeightPixels(_mip);
  const std::uint32_t slices = _texture.DepthPixels(_mip);
  const std::uint32_t subresource = texture->Subresource(_mip, _face);
  const Footprint footprint = context.FootprintOf(*texture, subresource);
  const std::uint64_t levelBytes = footprint.rowBytes * height * slices;
  if (_texels.size() != levelBytes)
  {
    context.core.Fail(std::format("Direct3D 12: UpdateTexture was given {} bytes for mip {} of {}, which holds {}", _texels.size(), _mip,
                                  texture->name, levelBytes));
    return;
  }
  Upload upload;
  if (!context.Open() || !context.AllocateUpload(footprint.totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, upload))
  {
    return;
  }
  CopyRows(upload.cpu, footprint.placed.Footprint.RowPitch, footprint.rows, _texels.data(), footprint.rowBytes, height, footprint.rowBytes,
           height, slices);
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed = footprint.placed;
  placed.Offset = upload.offsetBytes;
  context.RequireCopyDestination(*texture, subresource);
  context.FlushBarriers();
  const D3D12_TEXTURE_COPY_LOCATION destination = SubresourceLocation(texture->resource.Get(), subresource);
  const D3D12_TEXTURE_COPY_LOCATION source = FootprintLocation(upload.buffer, placed);
  context.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
}

bool DrawContext::ReadTexture(const Texture& _texture, std::uint32_t _mip, std::uint32_t _face, std::vector<std::byte>& _outTexels)
{
  Native& context = *m_native;
  Texture::Native* const texture = _texture.m_native.get();
  if (texture == nullptr || _mip >= texture->desc.mipLevels || _face >= texture->faces)
  {
    context.core.Fail(std::format("Direct3D 12: ReadTexture cannot read mip {} of face {} of {}", _mip, _face,
                                  texture != nullptr ? texture->name : std::string("an empty texture")));
    return false;
  }
  const std::uint32_t height = _texture.HeightPixels(_mip);
  const std::uint32_t slices = _texture.DepthPixels(_mip);
  const std::uint32_t subresource = texture->Subresource(_mip, _face);
  const Footprint footprint = context.FootprintOf(*texture, subresource);
  ComPtr<ID3D12Resource> readback;
  if (!context.Open() ||
      !context.CreateBuffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, footprint.totalBytes, readback, "a readback buffer"))
  {
    return false;
  }
  context.Require(*texture, subresource, D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.FlushBarriers();
  const D3D12_TEXTURE_COPY_LOCATION destination = FootprintLocation(readback.Get(), footprint.placed);
  const D3D12_TEXTURE_COPY_LOCATION source = SubresourceLocation(texture->resource.Get(), subresource);
  context.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  if (!context.SubmitAndWait())
  {
    return false;
  }

  const D3D12_RANGE everything{0, static_cast<SIZE_T>(footprint.totalBytes)};
  void* mapped = nullptr;
  if (!context.core.Check(readback->Map(0, &everything, &mapped), "ID3D12Resource::Map for a readback buffer"))
  {
    return false;
  }
  std::vector<std::byte> texels(static_cast<std::size_t>(footprint.rowBytes * height * slices));
  CopyRows(texels.data(), footprint.rowBytes, height, static_cast<const std::byte*>(mapped), footprint.placed.Footprint.RowPitch,
           footprint.rows, footprint.rowBytes, height, slices);
  const D3D12_RANGE nothingWritten{0, 0};
  readback->Unmap(0, &nothingWritten);
  _outTexels = std::move(texels);
  return true;
}

void DrawContext::UpdateBuffer(Buffer& _buffer, std::uint32_t _offsetBytes, std::span<const std::byte> _bytes)
{
  Native& context = *m_native;
  Buffer::Native* const buffer = _buffer.m_native.get();
  if (buffer == nullptr || std::uint64_t{_offsetBytes} + _bytes.size() > buffer->desc.sizeBytes)
  {
    context.core.Fail(std::format("Direct3D 12: UpdateBuffer cannot write {} bytes at {} of {}", _bytes.size(), _offsetBytes,
                                  buffer != nullptr ? buffer->name : std::string("an empty buffer")));
    return;
  }
  Upload upload;
  if (_bytes.empty() || !context.Open() || !context.AllocateUpload(_bytes.size(), BUFFER_UPLOAD_ALIGNMENT_BYTES, upload))
  {
    return;
  }
  std::memcpy(upload.cpu, _bytes.data(), _bytes.size());
  context.RequireCopyDestination(*buffer);
  context.FlushBarriers();
  context.list->CopyBufferRegion(buffer->resource.Get(), _offsetBytes, upload.buffer, upload.offsetBytes, _bytes.size());
}

bool DrawContext::ReadBuffer(const Buffer& _buffer, std::vector<std::byte>& _outBytes)
{
  Native& context = *m_native;
  Buffer::Native* const buffer = _buffer.m_native.get();
  if (buffer == nullptr)
  {
    context.core.Fail("Direct3D 12: ReadBuffer cannot read an empty buffer");
    return false;
  }
  const std::uint64_t sizeBytes = buffer->desc.sizeBytes;
  ComPtr<ID3D12Resource> readback;
  if (!context.Open() ||
      !context.CreateBuffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, sizeBytes, readback, "a readback buffer"))
  {
    return false;
  }
  context.Require(*buffer, D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.FlushBarriers();
  context.list->CopyBufferRegion(readback.Get(), 0, buffer->resource.Get(), 0, sizeBytes);
  if (!context.SubmitAndWait())
  {
    return false;
  }

  const D3D12_RANGE everything{0, static_cast<SIZE_T>(sizeBytes)};
  void* mapped = nullptr;
  if (!context.core.Check(readback->Map(0, &everything, &mapped), "ID3D12Resource::Map for a readback buffer"))
  {
    return false;
  }
  const auto* bytes = static_cast<const std::byte*>(mapped);
  std::vector<std::byte> copy(bytes, bytes + sizeBytes);
  const D3D12_RANGE nothingWritten{0, 0};
  readback->Unmap(0, &nothingWritten);
  _outBytes = std::move(copy);
  return true;
}

void DrawContext::Flush()
{
  m_native->Submit();
}

} // namespace Neuron
