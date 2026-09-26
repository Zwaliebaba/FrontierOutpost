// NeuronClient/DrawContext.cpp
#include "pch.h"

#include "CompiledShaders/GenerateMipsCS.h"
#include "CompiledShaders/PresentPS.h"
#include "CompiledShaders/PresentVS.h"
#include "DrawContext.h"
#include "GraphicsCore.h"
#include "Unicode.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <deque>
#include <format>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
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

/// A table of views or of samplers: both registers' ranges are sixteen long (ADR-007).
constexpr UINT TABLE_DESCRIPTORS = Program::MAX_SHADER_RESOURCES;
static_assert(Program::MAX_SAMPLERS == TABLE_DESCRIPTORS);

/// The shader-visible samplers: the default table first, then the tables draws use (ADR-007).
constexpr UINT SAMPLER_DESCRIPTORS = 2048;

/// The root signature's parameters, in its order.
constexpr UINT VERTEX_CONSTANTS_PARAMETER = 0;
constexpr UINT PIXEL_CONSTANTS_PARAMETER = 1;
constexpr UINT SHADER_RESOURCES_PARAMETER = 2;
constexpr UINT SAMPLERS_PARAMETER = 3;

/// How BeginEvent's data is read without PIX's runtime: WINPIX_EVENT_UNICODE_VERSION, a UTF-16
/// name and its terminator.
constexpr UINT PIX_UNICODE_EVENT = 0;

/// The compute root signature's.
constexpr UINT COMPUTE_CONSTANTS_PARAMETER = 0;
constexpr UINT COMPUTE_READS_PARAMETER = 1;
constexpr UINT COMPUTE_WRITES_PARAMETER = 2;

/// What a vertex shader's input reads when the layout gives it no attribute: GL's current
/// attribute, which liblt leaves at (0, 0, 0, 1) (plan §5.5). It comes from its own vertex buffer,
/// one instance of it, which every vertex of the one instance a draw makes reads.
constexpr std::array<float, 4> DEFAULT_ATTRIBUTE = {0.0f, 0.0f, 0.0f, 1.0f};
constexpr UINT DEFAULT_ATTRIBUTE_SLOT = 1;

/// Everything a pipeline state is made from, which the context caches them under (ADR-007).
struct PipelineKey
{
  std::uint64_t program = 0;
  BlendMode blend = BlendMode::Opaque;
  CullMode cull = CullMode::None;
  bool depthTest = false;
  bool depthWrite = false;
  bool wireframe = false;
  std::uint32_t targetCount = 0;
  std::array<DXGI_FORMAT, DrawContext::MAX_COLOR_TARGETS> targetFormats{};
  DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;
  std::string inputLayout; // the attributes the vertex shader reads, as text

  auto operator<=>(const PipelineKey&) const = default;
};

/// One state for all targets. Every field is stated: D3D12_BLEND has no zero value, so {} would
/// not be a blend.
D3D12_BLEND_DESC BlendDescription(BlendMode _mode) noexcept
{
  D3D12_RENDER_TARGET_BLEND_DESC target{FALSE,
                                        FALSE,
                                        D3D12_BLEND_ONE,
                                        D3D12_BLEND_ZERO,
                                        D3D12_BLEND_OP_ADD,
                                        D3D12_BLEND_ONE,
                                        D3D12_BLEND_ZERO,
                                        D3D12_BLEND_OP_ADD,
                                        D3D12_LOGIC_OP_NOOP,
                                        D3D12_COLOR_WRITE_ENABLE_ALL};
  switch (_mode)
  {
  case BlendMode::Opaque:
    break;
  case BlendMode::Alpha:
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    target.DestBlendAlpha = D3D12_BLEND_ONE;
    break;
  case BlendMode::Additive:
    target.BlendEnable = TRUE;
    target.DestBlend = D3D12_BLEND_ONE;
    target.DestBlendAlpha = D3D12_BLEND_ONE;
    break;
  }
  return D3D12_BLEND_DESC{FALSE, FALSE, {target, target, target, target, target, target, target, target}};
}

/// The clip-space macro negates y, which turns GL's counter-clockwise front faces clockwise, as
/// Direct3D's are by default (plan §5.5).
D3D12_RASTERIZER_DESC RasterizerDescription(CullMode _cull, bool _wireframe) noexcept
{
  D3D12_CULL_MODE cull = D3D12_CULL_MODE_NONE;
  if (_cull == CullMode::Back)
  {
    cull = D3D12_CULL_MODE_BACK;
  }
  else if (_cull == CullMode::Front)
  {
    cull = D3D12_CULL_MODE_FRONT;
  }
  return D3D12_RASTERIZER_DESC{_wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID,
                               cull,
                               FALSE,
                               D3D12_DEFAULT_DEPTH_BIAS,
                               D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
                               D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
                               TRUE,
                               FALSE,
                               FALSE,
                               0,
                               D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
}

/// GL's depth test, GL_LESS, with no stencil.
D3D12_DEPTH_STENCIL_DESC DepthStencilDescription(bool _test, bool _write) noexcept
{
  const D3D12_DEPTH_STENCILOP_DESC keep{D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
  return D3D12_DEPTH_STENCIL_DESC{_test ? TRUE : FALSE,
                                  _test && _write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO,
                                  D3D12_COMPARISON_FUNC_LESS,
                                  FALSE,
                                  D3D12_DEFAULT_STENCIL_READ_MASK,
                                  D3D12_DEFAULT_STENCIL_WRITE_MASK,
                                  keep,
                                  keep};
}

DXGI_FORMAT ElementFormat(VertexFormat _format) noexcept
{
  switch (_format)
  {
  case VertexFormat::Float1:
    return DXGI_FORMAT_R32_FLOAT;
  case VertexFormat::Float2:
    return DXGI_FORMAT_R32G32_FLOAT;
  case VertexFormat::Float3:
    return DXGI_FORMAT_R32G32B32_FLOAT;
  case VertexFormat::Float4:
    return DXGI_FORMAT_R32G32B32A32_FLOAT;
  }
  return DXGI_FORMAT_UNKNOWN;
}

/// What slots no sampler is set in sample with, and the table the heap starts with.
constexpr D3D12_SAMPLER_DESC POINT_SAMPLER{D3D12_FILTER_MIN_MAG_MIP_POINT,
                                           D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                           D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                           D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                           0.0f,
                                           1,
                                           D3D12_COMPARISON_FUNC_NEVER,
                                           {0.0f, 0.0f, 0.0f, 0.0f},
                                           0.0f,
                                           D3D12_FLOAT32_MAX};

/// A sampler's fields, in an order to sort by.
auto Fields(const D3D12_SAMPLER_DESC& _sampler) noexcept
{
  return std::tuple(_sampler.Filter, _sampler.AddressU, _sampler.AddressV, _sampler.AddressW, _sampler.MipLODBias, _sampler.MaxAnisotropy,
                    _sampler.ComparisonFunc, _sampler.BorderColor[0], _sampler.BorderColor[1], _sampler.BorderColor[2],
                    _sampler.BorderColor[3], _sampler.MinLOD, _sampler.MaxLOD);
}

/// A table of samplers, which the heap holds once however many draws use it.
struct SamplerTable
{
  std::array<D3D12_SAMPLER_DESC, TABLE_DESCRIPTORS> samplers;

  SamplerTable() noexcept
  {
    samplers.fill(POINT_SAMPLER);
  }

  bool operator<(const SamplerTable& _other) const noexcept
  {
    return std::ranges::lexicographical_compare(samplers, _other.samplers, [](const D3D12_SAMPLER_DESC& _a, const D3D12_SAMPLER_DESC& _b)
                                                { return Fields(_a) < Fields(_b); });
  }
};

D3D12_TEXTURE_ADDRESS_MODE AddressMode(TextureWrap _wrap) noexcept
{
  switch (_wrap)
  {
  case TextureWrap::Repeat:
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  case TextureWrap::MirroredRepeat:
    return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
  case TextureWrap::ClampToEdge:
    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  case TextureWrap::ClampToBorder:
    return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
  }
  return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
}

/// GL's sampling as Direct3D's. Without mips only the first is sampled, as in GL; anisotropy needs
/// linear minification between mips (plan §5.5).
D3D12_SAMPLER_DESC SamplerDescription(const SamplerDesc& _sampler) noexcept
{
  // D3D12_FILTER holds minification from bit 4, magnification from bit 2 and mips from bit 0, with
  // 1 for linear.
  const bool mipmapped = _sampler.mipFilter != MipFilter::None;
  const int minification = _sampler.minFilter == TextureFilter::Linear ? 1 : 0;
  const int magnification = _sampler.magFilter == TextureFilter::Linear ? 1 : 0;
  const int mip = _sampler.mipFilter == MipFilter::Linear ? 1 : 0;
  const bool anisotropic = _sampler.maxAnisotropy > 1 && minification == 1 && mip == 1;
  const D3D12_FILTER filter =
    anisotropic ? D3D12_FILTER_ANISOTROPIC : static_cast<D3D12_FILTER>((minification << 4) | (magnification << 2) | mip);
  const float minLod = mipmapped ? _sampler.minLod : 0.0f;
  const float maxLod = mipmapped ? std::max(_sampler.maxLod, _sampler.minLod) : 0.0f;
  return D3D12_SAMPLER_DESC{filter,
                            AddressMode(_sampler.wrapU),
                            AddressMode(_sampler.wrapV),
                            AddressMode(_sampler.wrapW),
                            std::clamp(_sampler.lodBias, -16.0f, 15.99f),
                            anisotropic ? std::min(_sampler.maxAnisotropy, 16u) : 1u,
                            D3D12_COMPARISON_FUNC_NEVER,
                            {_sampler.borderColor[0], _sampler.borderColor[1], _sampler.borderColor[2], _sampler.borderColor[3]},
                            minLod,
                            maxLod};
}

/// The part of _rect inside a level _widthPixels by _heightPixels. Row 0 is the bottom row in both,
/// so its numbers carry over (plan §5.5); what is left may be empty.
D3D12_RECT ClippedRect(const PixelRect& _rect, std::uint32_t _widthPixels, std::uint32_t _heightPixels) noexcept
{
  const auto clamped = [](std::int64_t _value, std::uint32_t _limit)
  { return static_cast<LONG>(std::clamp<std::int64_t>(_value, 0, _limit)); };
  return D3D12_RECT{clamped(_rect.xPixels, _widthPixels), clamped(_rect.yPixels, _heightPixels),
                    clamped(std::int64_t{_rect.xPixels} + _rect.widthPixels, _widthPixels),
                    clamped(std::int64_t{_rect.yPixels} + _rect.heightPixels, _heightPixels)};
}

/// Semantics are matched as HLSL matches them, whatever their case.
bool SameSemantic(std::string_view _a, std::string_view _b) noexcept
{
  return std::ranges::equal(_a, _b, [](char _x, char _y)
                            { return std::toupper(static_cast<unsigned char>(_x)) == std::toupper(static_cast<unsigned char>(_y)); });
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
  // What the open list has been given.
  bool heapsReady = false;
  bool graphicsReady = false; // the graphics root signature
  bool computeReady = false;  // the compute root signature

  // The draw path's objects, which Initialize makes.
  ComPtr<ID3D12RootSignature> graphicsSignature;
  ComPtr<ID3D12DescriptorHeap> shaderHeap;  // shader-visible: the null table, then the ring
  ComPtr<ID3D12DescriptorHeap> samplerHeap; // shader-visible: the default table, then the tables in use
  std::map<PipelineKey, ComPtr<ID3D12PipelineState>> pipelines;
  ComPtr<ID3D12PipelineState> presentPipeline; // PresentVS.hlsl's and PresentPS.hlsl's, into BACK_BUFFER_FORMAT

  // What an input without an attribute reads, which Initialize makes.
  ComPtr<ID3D12Resource> defaultAttribute;

  // What the next draw uses, which stays set from one draw to the next.
  struct Target
  {
    Texture::Native* texture = nullptr; // null once it has gone
    std::uint32_t mip = 0;
    std::uint32_t layer = 0;
  };
  std::array<Target, MAX_COLOR_TARGETS> colorTargets{};
  std::uint32_t colorTargetCount = 0;
  Texture::Native* depthTarget = nullptr;
  std::uint32_t targetWidth = 0; // the colour targets', or the depth target's when there are none
  std::uint32_t targetHeight = 0;
  std::uint32_t depthWidth = 0;
  std::uint32_t depthHeight = 0;
  D3D12_VIEWPORT viewport{};
  D3D12_RECT scissor{};
  RenderState state{.blend = BlendMode::Opaque, .cull = CullMode::None, .depthTest = false, .depthWrite = true, .wireframe = false};
  Program::Native* program = nullptr;
  std::array<Texture::Native*, TABLE_DESCRIPTORS> textures{};
  std::array<D3D12_SAMPLER_DESC, TABLE_DESCRIPTORS> samplers = SamplerTable().samplers;
  // What PrepareDraw found for the draw it readied, which RecordDraw binds.
  std::uint32_t boundTargets = 0; // of the program's outputs, those that have a target
  bool defaultsRead = false;      // whether an input reads the default attribute

  // The ring of shader-visible views each draw's table is taken from, after the null table. A
  // table is free again once the GPU has finished the list that took it.
  struct RingMark
  {
    UINT count;               // descriptors the list took, ends skipped at a wrap included
    std::uint64_t fenceValue; // they are free once the fence reaches this
  };
  UINT shaderIncrement = 0;
  UINT samplerIncrement = 0;
  D3D12_CPU_DESCRIPTOR_HANDLE nullShaderView{}; // CPU-only, copied into the slots nothing is bound to
  UINT ringCapacity = 0;
  UINT ringHead = 0;
  UINT ringUsed = 0;    // from the oldest table still in use up to ringHead
  UINT ringPending = 0; // what the open list has taken so far
  std::deque<RingMark> ringMarks;

  // The last table of views, which the next draw reuses when it reads the same textures in the
  // same list.
  std::array<Texture::Native*, TABLE_DESCRIPTORS> tableTextures{};
  UINT shaderTable = 0;        // its first descriptor in the heap; 0 is the null table
  std::uint64_t tableList = 0; // the list it was taken for; 0 for none

  // The sampler tables in the heap, by their contents, and the next draw's.
  std::map<SamplerTable, UINT> samplerTables;
  UINT nextSamplerTable = 1; // table 0 is the point samplers'
  UINT samplerTable = 0;

  // What dispatches use.
  struct Unordered
  {
    Texture::Native* texture = nullptr; // null once it has gone
    std::uint32_t mip = 0;
  };
  ComPtr<ID3D12RootSignature> computeSignature;
  ComPtr<ID3D12PipelineState> mipPipeline;                               // GenerateMipsCS.hlsl's
  std::map<std::uint64_t, ComPtr<ID3D12PipelineState>> computePipelines; // by program
  std::array<Unordered, Program::MAX_UNORDERED_RESOURCES> unordered{};
  D3D12_CPU_DESCRIPTOR_HANDLE nullUnorderedView{}; // CPU-only, copied into the u slots nothing is bound to

  // Reads that do not wait for the GPU, oldest first.
  struct PendingRead
  {
    std::uint64_t ticket = 0;
    std::uint64_t fenceValue = 0; // the copy's submission's, once it is submitted
    ComPtr<ID3D12Resource> buffer;
    Footprint footprint;
    std::uint32_t heightPixels = 0;
    std::uint32_t depthPixels = 0;
  };
  std::vector<PendingRead> reads;
  std::uint64_t nextReadTicket = 1;

  // The regions BeginEvent started that are still open, outermost first.
  std::vector<std::wstring> events;

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
    heapsReady = false;
    graphicsReady = false;
    computeReady = false;
    // Each list begins the regions still open, and ends them before it goes, so that each is whole.
    for (const std::wstring& event : events)
    {
      BeginEventOnList(event);
    }
    return true;
  }

  /// Begins a region named _name in the open list.
  void BeginEventOnList(const std::wstring& _name) const
  {
    list->BeginEvent(PIX_UNICODE_EVENT, _name.c_str(), static_cast<UINT>((_name.size() + 1) * sizeof(wchar_t)));
  }

  /// Submits the open list, if there is one, and marks the queue after it. What the list used is
  /// free, or released, once the fence reaches that mark.
  void Submit()
  {
    if (open)
    {
      FlushBarriers();
      for (std::size_t event = 0; event < events.size(); ++event)
      {
        list->EndEvent();
      }
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
    if (ringPending > 0)
    {
      ringMarks.push_back({ringPending, value});
      ringPending = 0;
    }
    for (PendingRead& read : reads)
    {
      if (read.fenceValue == 0)
      {
        read.fenceValue = value;
      }
    }
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

  static std::string NameOf(const Texture::Native* _texture)
  {
    return _texture != nullptr ? _texture->name : std::string("an empty texture");
  }

  void FailDraw(std::string_view _why) const
  {
    core.Fail(std::format("Direct3D 12: a draw {}; nothing was recorded", _why));
  }

  /// Takes _tables tables of views, one after the other, from the ring for the open list, and
  /// returns the first one's first descriptor in the heap. They are taken together, so that making
  /// room for one cannot free another. When the ring is full it waits for the GPU, first submitting
  /// the open list when that is what fills it, so it can close the list.
  UINT AllocateTables(UINT _tables = 1)
  {
    const UINT count = _tables * TABLE_DESCRIPTORS;
    for (;;)
    {
      const std::uint64_t completed = core.Completed();
      while (!ringMarks.empty() && ringMarks.front().fenceValue <= completed)
      {
        ringUsed -= ringMarks.front().count;
        ringMarks.pop_front();
      }
      if (ringUsed == 0)
      {
        ringHead = 0;
      }
      // Tables do not wrap: the ring's end is skipped, and freed with them.
      const bool wrap = ringHead + count > ringCapacity;
      const UINT skipped = wrap ? ringCapacity - ringHead : 0;
      if (ringCapacity - ringUsed >= skipped + count)
      {
        if (wrap)
        {
          ringHead = 0;
        }
        const UINT first = TABLE_DESCRIPTORS + ringHead;
        ringHead += count;
        ringUsed += skipped + count;
        ringPending += skipped + count;
        return first;
      }
      if (ringMarks.empty())
      {
        Submit();
      }
      core.WaitFor(ringMarks.front().fenceValue);
    }
  }

  /// The table in the sampler heap that holds _table, written the first time it is asked for. When
  /// the heap is full it starts again, once the GPU has finished with every table in it, so it can
  /// close the list.
  UINT SamplerTableFor(const SamplerTable& _table)
  {
    if (const auto found = samplerTables.find(_table); found != samplerTables.end())
    {
      return found->second;
    }
    if (nextSamplerTable == SAMPLER_DESCRIPTORS / TABLE_DESCRIPTORS)
    {
      static_cast<void>(SubmitAndWait());
      samplerTables.clear();
      samplerTables.emplace(SamplerTable(), 0);
      nextSamplerTable = 1;
    }
    const UINT index = nextSamplerTable++;
    const D3D12_CPU_DESCRIPTOR_HANDLE start = samplerHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT slot = 0; slot < TABLE_DESCRIPTORS; ++slot)
    {
      const SIZE_T offset = (static_cast<SIZE_T>(index) * TABLE_DESCRIPTORS) + slot;
      core.device->CreateSampler(&_table.samplers[slot], {start.ptr + (offset * samplerIncrement)});
    }
    samplerTables.emplace(_table, index);
    return index;
  }

  /// Picks the tables of views and samplers the next draw reads through: fixed ones for a program
  /// that reads none, and otherwise the last ones or new ones. False when a view could not be
  /// made, which the core has reported.
  bool PrepareTables(const std::array<Texture::Native*, TABLE_DESCRIPTORS>& _read)
  {
    // The samplers first: starting their heap again waits for the GPU, which frees the ring too.
    SamplerTable wanted;
    for (const auto& [name, slot] : program->samplers)
    {
      wanted.samplers[static_cast<std::size_t>(slot)] = samplers[static_cast<std::size_t>(slot)];
    }
    samplerTable = program->samplers.empty() ? 0 : SamplerTableFor(wanted);

    if (std::ranges::all_of(_read, [](const Texture::Native* _texture) { return _texture == nullptr; }))
    {
      shaderTable = 0;
      return true;
    }
    if (tableList == listSerial && _read == tableTextures)
    {
      return true;
    }
    const UINT first = AllocateTables();
    const D3D12_CPU_DESCRIPTOR_HANDLE start = shaderHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT slot = 0; slot < TABLE_DESCRIPTORS; ++slot)
    {
      D3D12_CPU_DESCRIPTOR_HANDLE view = nullShaderView;
      if (_read[slot] != nullptr && !_read[slot]->ShaderView(view))
      {
        return false;
      }
      const SIZE_T offset = static_cast<SIZE_T>(first) + slot;
      core.device->CopyDescriptorsSimple(1, {start.ptr + (offset * shaderIncrement)}, view, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    tableTextures = _read;
    shaderTable = first;
    tableList = listSerial;
    return true;
  }

  /// Checks that the set targets, program and _layout can be drawn with, and finds or makes the
  /// pipeline state for them. Opens the list, and gives it the root signature and the heaps.
  bool PrepareDraw(const VertexLayout& _layout, ID3D12PipelineState*& _outPipeline)
  {
    if (program == nullptr)
    {
      FailDraw("has no program set");
      return false;
    }
    if (colorTargetCount == 0 && depthTarget == nullptr)
    {
      FailDraw(std::format("of {} has no target set", program->name));
      return false;
    }
    // Only as many targets are bound as the program writes, and the pipeline state names no more
    // than are set: what the program writes past the last of them is discarded (plan §5.5).
    boundTargets = std::min(program->targetCount, colorTargetCount);
    for (std::uint32_t index = 0; index < boundTargets; ++index)
    {
      if (colorTargets[index].texture == nullptr)
      {
        FailDraw(std::format("of {} has colour target {}, which has gone", program->name, index));
        return false;
      }
    }
    // Depth is bound only when the draw tests it; with no depth target, it passes, as in GL.
    const bool depthBound = state.depthTest && depthTarget != nullptr;
    if (depthBound && colorTargetCount > 0 && (depthWidth != targetWidth || depthHeight != targetHeight))
    {
      FailDraw(std::format("of {} tests a depth target of {} by {} under targets of {} by {}", program->name, depthWidth, depthHeight,
                           targetWidth, targetHeight));
      return false;
    }

    PipelineKey key;
    key.program = program->id;
    key.blend = state.blend;
    key.cull = state.cull;
    key.depthTest = depthBound;
    key.depthWrite = depthBound && state.depthWrite;
    key.wireframe = state.wireframe;
    key.targetCount = boundTargets;
    for (std::uint32_t index = 0; index < boundTargets; ++index)
    {
      key.targetFormats[index] = colorTargets[index].texture->resourceDesc.Format;
    }
    key.depthFormat = depthBound ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_UNKNOWN;

    // The elements the vertex shader reads, in its signature's order; the layout may have more. An
    // input it has no attribute for reads the default attribute's one instance.
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    defaultsRead = false;
    for (const ProgramInput& input : program->inputs)
    {
      const auto attribute =
        std::ranges::find_if(_layout.attributes, [&input](const VertexAttribute& _attribute)
                             { return _attribute.index == input.index && SameSemantic(_attribute.semantic, input.semantic); });
      if (attribute == _layout.attributes.end())
      {
        elements.push_back({input.semantic.c_str(), input.index, DXGI_FORMAT_R32G32B32A32_FLOAT, DEFAULT_ATTRIBUTE_SLOT, 0,
                            D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1});
        key.inputLayout += std::format("{}{}:default;", input.semantic, input.index);
        defaultsRead = true;
        continue;
      }
      const DXGI_FORMAT format = ElementFormat(attribute->format);
      elements.push_back(
        {input.semantic.c_str(), input.index, format, 0, attribute->offsetBytes, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
      key.inputLayout += std::format("{}{}:{}@{};", input.semantic, input.index, static_cast<int>(format), attribute->offsetBytes);
    }

    // The textures the program reads; slots it does not read are left unbound, and a texture may
    // wait in one while it is drawn into.
    std::array<Texture::Native*, TABLE_DESCRIPTORS> read{};
    for (const auto& [name, slot] : program->shaderResources)
    {
      Texture::Native* const texture = textures[static_cast<std::size_t>(slot)];
      const bool drawnInto = std::ranges::any_of(std::span(colorTargets).first(boundTargets),
                                                 [texture](const Target& _target) { return _target.texture == texture; }) ||
                             (depthBound && depthTarget == texture);
      if (texture != nullptr && drawnInto)
      {
        FailDraw(std::format("of {} reads {} at t{} while it draws into it", program->name, texture->name, slot));
        return false;
      }
      read[static_cast<std::size_t>(slot)] = texture;
    }

    _outPipeline = PipelineFor(key, elements);
    // The tables before the list opens: making room for them may submit it.
    if (_outPipeline == nullptr || !PrepareTables(read) || !Open())
    {
      return false;
    }
    ReadyHeaps();
    if (!graphicsReady)
    {
      list->SetGraphicsRootSignature(graphicsSignature.Get());
      graphicsReady = true;
    }
    return true;
  }

  /// Gives the open list the shader-visible heaps, which draws and dispatches share.
  void ReadyHeaps()
  {
    if (!heapsReady)
    {
      const std::array<ID3D12DescriptorHeap*, 2> heaps = {shaderHeap.Get(), samplerHeap.Get()};
      list->SetDescriptorHeaps(static_cast<UINT>(heaps.size()), heaps.data());
      heapsReady = true;
    }
  }

  /// A compute program's pipeline state, made the first time it is asked for.
  ID3D12PipelineState* ComputePipelineFor(const Program::Native& _program)
  {
    if (const auto found = computePipelines.find(_program.id); found != computePipelines.end())
    {
      return found->second.Get();
    }
    const D3D12_COMPUTE_PIPELINE_STATE_DESC desc{computeSignature.Get(),
                                                 {_program.computeShader.data(), _program.computeShader.size()},
                                                 0,
                                                 {nullptr, 0},
                                                 D3D12_PIPELINE_STATE_FLAG_NONE};
    ComPtr<ID3D12PipelineState> pipeline;
    if (!core.Check(core.device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline)),
                    std::format("ID3D12Device::CreateComputePipelineState for program {}", _program.name)))
    {
      return nullptr;
    }
    return computePipelines.emplace(_program.id, std::move(pipeline)).first->second.Get();
  }

  /// Copies a table's views into the ring: _views[slot], or _null where that is empty.
  void WriteTable(UINT _first, std::span<const D3D12_CPU_DESCRIPTOR_HANDLE> _views, D3D12_CPU_DESCRIPTOR_HANDLE _null)
  {
    const D3D12_CPU_DESCRIPTOR_HANDLE start = shaderHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT slot = 0; slot < TABLE_DESCRIPTORS; ++slot)
    {
      const D3D12_CPU_DESCRIPTOR_HANDLE view = slot < _views.size() && _views[slot].ptr != 0 ? _views[slot] : _null;
      const SIZE_T offset = static_cast<SIZE_T>(_first) + slot;
      core.device->CopyDescriptorsSimple(1, {start.ptr + (offset * shaderIncrement)}, view, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
  }

  /// Makes mip _mip of face _face of _texture from the mip above it. _sizes are the mip above's
  /// width and height in pixels, then this one's. False when the list cannot open or there is no
  /// upload memory, which the core has reported.
  bool MakeMip(Texture::Native& _texture, std::uint32_t _mip, std::uint32_t _face, std::array<std::uint32_t, 4> _sizes)
  {
    // One slice of the mip above to read, and one of this to write, in tables before the list
    // opens: making room for them may submit it.
    const UINT sourceTable = AllocateTables(2);
    const UINT destinationTable = sourceTable + TABLE_DESCRIPTORS;
    WriteTable(sourceTable, {}, nullShaderView);
    WriteTable(destinationTable, {}, nullUnorderedView);
    const D3D12_CPU_DESCRIPTOR_HANDLE start = shaderHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_SHADER_RESOURCE_VIEW_DESC sourceDesc{};
    sourceDesc.Format = _texture.resourceDesc.Format;
    sourceDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    sourceDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sourceDesc.Texture2DArray.MostDetailedMip = _mip - 1;
    sourceDesc.Texture2DArray.MipLevels = 1;
    sourceDesc.Texture2DArray.FirstArraySlice = _face;
    sourceDesc.Texture2DArray.ArraySize = 1;
    core.device->CreateShaderResourceView(_texture.resource.Get(), &sourceDesc,
                                          {start.ptr + (std::uint64_t{sourceTable} * shaderIncrement)});
    D3D12_UNORDERED_ACCESS_VIEW_DESC destinationDesc{};
    destinationDesc.Format = _texture.resourceDesc.Format;
    destinationDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
    destinationDesc.Texture2DArray.MipSlice = _mip;
    destinationDesc.Texture2DArray.FirstArraySlice = _face;
    destinationDesc.Texture2DArray.ArraySize = 1;
    core.device->CreateUnorderedAccessView(_texture.resource.Get(), nullptr, &destinationDesc,
                                           {start.ptr + (std::uint64_t{destinationTable} * shaderIncrement)});
    if (!Open())
    {
      return false;
    }
    ReadyHeaps();
    if (!computeReady)
    {
      list->SetComputeRootSignature(computeSignature.Get());
      computeReady = true;
    }
    // The shader's $Globals: the mip above's size, then this one's.
    Upload constants;
    if (!AllocateUpload(sizeof(_sizes), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, constants))
    {
      return false;
    }
    std::memcpy(constants.cpu, _sizes.data(), sizeof(_sizes));
    Require(_texture, _texture.Subresource(_mip - 1, _face),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    Require(_texture, _texture.Subresource(_mip, _face), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    FlushBarriers();
    list->SetPipelineState(mipPipeline.Get());
    list->SetComputeRootConstantBufferView(COMPUTE_CONSTANTS_PARAMETER, constants.buffer->GetGPUVirtualAddress() + constants.offsetBytes);
    list->SetComputeRootDescriptorTable(COMPUTE_READS_PARAMETER, ShaderTableHandle(sourceTable));
    list->SetComputeRootDescriptorTable(COMPUTE_WRITES_PARAMETER, ShaderTableHandle(destinationTable));
    // Eight by eight threads a group.
    list->Dispatch((_sizes[2] + 7) / 8, (_sizes[3] + 7) / 8, 1);
    return true;
  }

  /// Records the present pass DrawContext::RecordPresent readied. False when a view, the capture
  /// buffer or upload memory could not be made, or the list could not open, which the core has
  /// reported.
  bool RecordPresent(Texture::Native& _image, PresentTarget& _target)
  {
    D3D12_CPU_DESCRIPTOR_HANDLE imageView{};
    if (!_image.ShaderView(imageView))
    {
      return false;
    }
    if (_target.capture)
    {
      const D3D12_RESOURCE_DESC bufferDesc = _target.buffer->GetDesc();
      UINT64 totalBytes = 0;
      core.device->GetCopyableFootprints(&bufferDesc, 0, 1, 0, &_target.captureFootprint, nullptr, nullptr, &totalBytes);
      if (!CreateBuffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, totalBytes, _target.captureBuffer,
                        "a capture of the back buffer"))
      {
        return false;
      }
    }
    // The table before the list opens: making room for it may submit it.
    const UINT table = AllocateTables();
    WriteTable(table, std::span(&imageView, 1), nullShaderView);
    if (!Open())
    {
      return false;
    }
    ReadyHeaps();
    if (!graphicsReady)
    {
      list->SetGraphicsRootSignature(graphicsSignature.Get());
      graphicsReady = true;
    }
    // Neither shader has constants, but every root parameter is set.
    Upload constants;
    if (!AllocateUpload(16, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, constants))
    {
      return false;
    }
    std::memset(constants.cpu, 0, 16);
    const D3D12_GPU_VIRTUAL_ADDRESS constantsAddress = constants.buffer->GetGPUVirtualAddress() + constants.offsetBytes;
    Require(_image, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barriers.push_back(Transition(_target.buffer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_PRESENT,
                                  D3D12_RESOURCE_STATE_RENDER_TARGET));
    FlushBarriers();

    list->SetPipelineState(presentPipeline.Get());
    list->SetGraphicsRootConstantBufferView(VERTEX_CONSTANTS_PARAMETER, constantsAddress);
    list->SetGraphicsRootConstantBufferView(PIXEL_CONSTANTS_PARAMETER, constantsAddress);
    list->SetGraphicsRootDescriptorTable(SHADER_RESOURCES_PARAMETER, ShaderTableHandle(table));
    list->SetGraphicsRootDescriptorTable(SAMPLERS_PARAMETER, samplerHeap->GetGPUDescriptorHandleForHeapStart());
    list->OMSetRenderTargets(1, &_target.view, FALSE, nullptr);
    const D3D12_VIEWPORT whole{0.0f, 0.0f, static_cast<float>(_target.widthPixels), static_cast<float>(_target.heightPixels), 0.0f, 1.0f};
    const D3D12_RECT all{0, 0, static_cast<LONG>(_target.widthPixels), static_cast<LONG>(_target.heightPixels)};
    list->RSSetViewports(1, &whole);
    list->RSSetScissorRects(1, &all);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->DrawInstanced(3, 1, 0, 0);

    // Back to PRESENT, by way of the copy when one is asked for.
    D3D12_RESOURCE_STATES last = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (_target.capture)
    {
      barriers.push_back(Transition(_target.buffer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                    D3D12_RESOURCE_STATE_COPY_SOURCE));
      FlushBarriers();
      const D3D12_TEXTURE_COPY_LOCATION destination = FootprintLocation(_target.captureBuffer.Get(), _target.captureFootprint);
      const D3D12_TEXTURE_COPY_LOCATION source = SubresourceLocation(_target.buffer, 0);
      list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
      last = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    barriers.push_back(Transition(_target.buffer, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, last, D3D12_RESOURCE_STATE_PRESENT));
    FlushBarriers();
    return true;
  }

  D3D12_GPU_DESCRIPTOR_HANDLE ShaderTableHandle(UINT _first) const
  {
    return {shaderHeap->GetGPUDescriptorHandleForHeapStart().ptr + (std::uint64_t{_first} * shaderIncrement)};
  }

  /// The pipeline state for _key, made the first time it is asked for.
  ID3D12PipelineState* PipelineFor(const PipelineKey& _key, const std::vector<D3D12_INPUT_ELEMENT_DESC>& _elements)
  {
    if (const auto found = pipelines.find(_key); found != pipelines.end())
    {
      return found->second.Get();
    }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{
      .pRootSignature = graphicsSignature.Get(),
      .VS = {program->vertexShader.data(), program->vertexShader.size()},
      .PS = {program->pixelShader.data(), program->pixelShader.size()},
      .DS = {nullptr, 0},
      .HS = {nullptr, 0},
      .GS = {nullptr, 0},
      .StreamOutput = {nullptr, 0, nullptr, 0, 0},
      .BlendState = BlendDescription(_key.blend),
      .SampleMask = std::numeric_limits<UINT>::max(),
      .RasterizerState = RasterizerDescription(_key.cull, _key.wireframe),
      .DepthStencilState = DepthStencilDescription(_key.depthTest, _key.depthWrite),
      .InputLayout = {_elements.data(), static_cast<UINT>(_elements.size())},
      .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
      .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
      .NumRenderTargets = _key.targetCount,
      .RTVFormats = {},
      .DSVFormat = _key.depthFormat,
      .SampleDesc = {1, 0},
      .NodeMask = 0,
      .CachedPSO = {nullptr, 0},
      .Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
    };
    std::ranges::copy(_key.targetFormats, std::begin(desc.RTVFormats));
    ComPtr<ID3D12PipelineState> pipeline;
    if (!core.Check(core.device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)),
                    std::format("ID3D12Device::CreateGraphicsPipelineState for program {}", program->name)))
    {
      return nullptr;
    }
    return pipelines.emplace(_key, std::move(pipeline)).first->second.Get();
  }

  /// A stage's constants as the program holds them now, in the upload ring. A stage without
  /// constants gets some all the same, so that every root parameter is set. 0 when there is no
  /// upload memory, which the core has reported.
  D3D12_GPU_VIRTUAL_ADDRESS UploadConstants(ShaderStage _stage)
  {
    const std::vector<std::byte>& values = program->constantValues[static_cast<std::size_t>(_stage)];
    const std::uint64_t sizeBytes = std::max<std::uint64_t>(values.size(), 16);
    Upload upload;
    if (!AllocateUpload(sizeBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, upload))
    {
      return 0;
    }
    std::memset(upload.cpu, 0, static_cast<std::size_t>(sizeBytes));
    if (!values.empty())
    {
      std::memcpy(upload.cpu, values.data(), values.size());
    }
    return upload.buffer->GetGPUVirtualAddress() + upload.offsetBytes;
  }

  /// Records the draw PrepareDraw readied, with its constants, targets and state.
  void RecordDraw(ID3D12PipelineState* _pipeline, const D3D12_VERTEX_BUFFER_VIEW& _vertices, const D3D12_INDEX_BUFFER_VIEW& _indices,
                  std::uint32_t _firstIndex, std::uint32_t _indexCount)
  {
    const D3D12_GPU_VIRTUAL_ADDRESS vertexConstants = UploadConstants(ShaderStage::Vertex);
    const D3D12_GPU_VIRTUAL_ADDRESS pixelConstants = UploadConstants(ShaderStage::Pixel);
    if (vertexConstants == 0 || pixelConstants == 0)
    {
      return;
    }
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, MAX_COLOR_TARGETS> targetViews{};
    for (std::uint32_t index = 0; index < boundTargets; ++index)
    {
      const Target& target = colorTargets[index];
      if (!target.texture->TargetView(target.mip, target.layer, targetViews[index]))
      {
        return;
      }
      // The slices of a 3D texture's mip are one subresource.
      const bool volume = target.texture->desc.dimension == TextureDimension::Texture3D;
      Require(*target.texture, target.texture->Subresource(target.mip, volume ? 0 : target.layer), D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    if (shaderTable != 0)
    {
      for (Texture::Native* const texture : tableTextures)
      {
        if (texture != nullptr)
        {
          Require(*texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
      }
    }
    D3D12_CPU_DESCRIPTOR_HANDLE depthView{};
    const bool depthBound = state.depthTest && depthTarget != nullptr;
    if (depthBound)
    {
      if (!depthTarget->DepthView(depthView))
      {
        return;
      }
      Require(*depthTarget, 0, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    FlushBarriers();

    list->SetPipelineState(_pipeline);
    list->SetGraphicsRootConstantBufferView(VERTEX_CONSTANTS_PARAMETER, vertexConstants);
    list->SetGraphicsRootConstantBufferView(PIXEL_CONSTANTS_PARAMETER, pixelConstants);
    const D3D12_GPU_DESCRIPTOR_HANDLE shaderStart = shaderHeap->GetGPUDescriptorHandleForHeapStart();
    const D3D12_GPU_DESCRIPTOR_HANDLE samplerStart = samplerHeap->GetGPUDescriptorHandleForHeapStart();
    list->SetGraphicsRootDescriptorTable(SHADER_RESOURCES_PARAMETER, {shaderStart.ptr + (std::uint64_t{shaderTable} * shaderIncrement)});
    list->SetGraphicsRootDescriptorTable(SAMPLERS_PARAMETER,
                                         {samplerStart.ptr + (std::uint64_t{samplerTable} * TABLE_DESCRIPTORS * samplerIncrement)});
    list->OMSetRenderTargets(boundTargets, targetViews.data(), FALSE, depthBound ? &depthView : nullptr);
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &_vertices);
    if (defaultsRead)
    {
      const D3D12_VERTEX_BUFFER_VIEW defaults{defaultAttribute->GetGPUVirtualAddress(), sizeof(DEFAULT_ATTRIBUTE),
                                              sizeof(DEFAULT_ATTRIBUTE)};
      list->IASetVertexBuffers(DEFAULT_ATTRIBUTE_SLOT, 1, &defaults);
    }
    list->IASetIndexBuffer(&_indices);
    list->DrawIndexedInstanced(_indexCount, 1, _firstIndex, 0, 0);
  }
};

DrawContext::DrawContext(GraphicsCore& _core)
  : m_native(std::make_unique<Native>(_core))
{
}

DrawContext::~DrawContext() = default;

void DrawContext::UpdateTexture(Texture& _texture, std::uint32_t _mip, std::uint32_t _face, std::span<const std::byte> _texels)
{
  UpdateTexture(_texture, _mip, _face, {0, 0, 0, _texture.WidthPixels(_mip), _texture.HeightPixels(_mip), _texture.DepthPixels(_mip)},
                _texels);
}

void DrawContext::UpdateTexture(Texture& _texture, std::uint32_t _mip, std::uint32_t _face, const TextureRegion& _region,
                                std::span<const std::byte> _texels)
{
  Native& context = *m_native;
  Texture::Native* const texture = _texture.m_native.get();
  if (texture == nullptr || texture->desc.format == TextureFormat::Depth32F || _mip >= texture->desc.mipLevels || _face >= texture->faces)
  {
    context.core.Fail(std::format("Direct3D 12: UpdateTexture cannot fill mip {} of face {} of {}", _mip, _face,
                                  texture != nullptr ? texture->name : std::string("an empty texture")));
    return;
  }
  // Each end is checked alone, so that no sum can wrap.
  const std::uint32_t width = _texture.WidthPixels(_mip);
  const std::uint32_t height = _texture.HeightPixels(_mip);
  const std::uint32_t slices = _texture.DepthPixels(_mip);
  const auto inside = [](std::uint32_t _start, std::uint32_t _length, std::uint32_t _limit)
  { return _length > 0 && _start < _limit && _length <= _limit - _start; };
  if (!inside(_region.xPixels, _region.widthPixels, width) || !inside(_region.yPixels, _region.heightPixels, height) ||
      !inside(_region.zPixels, _region.depthPixels, slices))
  {
    context.core.Fail(
      std::format("Direct3D 12: UpdateTexture was given {} by {} by {} from ({}, {}, {}), which is not inside mip {} of {}, "
                  "{} by {} by {}",
                  _region.widthPixels, _region.heightPixels, _region.depthPixels, _region.xPixels, _region.yPixels, _region.zPixels, _mip,
                  texture->name, width, height, slices));
    return;
  }
  const std::uint64_t rowBytes = std::uint64_t{_region.widthPixels} * TexelBytes(texture->desc.format);
  const std::uint64_t regionBytes = rowBytes * _region.heightPixels * _region.depthPixels;
  if (_texels.size() != regionBytes)
  {
    context.core.Fail(std::format("Direct3D 12: UpdateTexture was given {} bytes for {} by {} by {} texels of mip {} of {}, which take {}",
                                  _texels.size(), _region.widthPixels, _region.heightPixels, _region.depthPixels, _mip, texture->name,
                                  regionBytes));
    return;
  }
  const std::uint64_t rowPitch = AlignUp(rowBytes, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
  Upload upload;
  if (!context.Open() ||
      !context.AllocateUpload(rowPitch * _region.heightPixels * _region.depthPixels, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, upload))
  {
    return;
  }
  CopyRows(upload.cpu, rowPitch, _region.heightPixels, _texels.data(), rowBytes, _region.heightPixels, rowBytes, _region.heightPixels,
           _region.depthPixels);
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT placed{};
  placed.Offset = upload.offsetBytes;
  placed.Footprint = {texture->resourceDesc.Format, _region.widthPixels, _region.heightPixels, _region.depthPixels,
                      static_cast<UINT>(rowPitch)};
  const std::uint32_t subresource = texture->Subresource(_mip, _face);
  context.RequireCopyDestination(*texture, subresource);
  context.FlushBarriers();
  const D3D12_TEXTURE_COPY_LOCATION destination = SubresourceLocation(texture->resource.Get(), subresource);
  const D3D12_TEXTURE_COPY_LOCATION source = FootprintLocation(upload.buffer, placed);
  context.list->CopyTextureRegion(&destination, _region.xPixels, _region.yPixels, _region.zPixels, &source, nullptr);
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

void DrawContext::ClearColor(Texture& _texture, std::uint32_t _mip, std::uint32_t _layer, const std::array<float, 4>& _color)
{
  ClearColorIn(_texture, _mip, _layer, _color, nullptr);
}

void DrawContext::ClearColor(Texture& _texture, std::uint32_t _mip, std::uint32_t _layer, const std::array<float, 4>& _color,
                             const PixelRect& _rect)
{
  ClearColorIn(_texture, _mip, _layer, _color, &_rect);
}

void DrawContext::ClearDepth(Texture& _texture, float _depth)
{
  ClearDepthIn(_texture, _depth, nullptr);
}

void DrawContext::ClearDepth(Texture& _texture, float _depth, const PixelRect& _rect)
{
  ClearDepthIn(_texture, _depth, &_rect);
}

void DrawContext::ClearColorIn(Texture& _texture, std::uint32_t _mip, std::uint32_t _layer, const std::array<float, 4>& _color,
                               const PixelRect* _rect)
{
  Native& context = *m_native;
  Texture::Native* const texture = _texture.m_native.get();
  const bool volume = texture != nullptr && texture->desc.dimension == TextureDimension::Texture3D;
  const std::uint32_t layers = volume ? _texture.DepthPixels(_mip) : _texture.Faces();
  if (texture == nullptr || texture->desc.format == TextureFormat::Depth32F || _mip >= texture->desc.mipLevels || _layer >= layers)
  {
    context.core.Fail(std::format("Direct3D 12: ClearColor cannot clear mip {} of layer {} of {}", _mip, _layer,
                                  texture != nullptr ? texture->name : std::string("an empty texture")));
    return;
  }
  const D3D12_RECT rect =
    _rect != nullptr ? ClippedRect(*_rect, _texture.WidthPixels(_mip), _texture.HeightPixels(_mip)) : D3D12_RECT{0, 0, 1, 1};
  if (rect.left >= rect.right || rect.top >= rect.bottom)
  {
    return;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE view{};
  if (!context.Open() || !texture->TargetView(_mip, _layer, view))
  {
    return;
  }
  // The slices of a 3D texture's mip are one subresource.
  context.Require(*texture, texture->Subresource(_mip, volume ? 0 : _layer), D3D12_RESOURCE_STATE_RENDER_TARGET);
  context.FlushBarriers();
  context.list->ClearRenderTargetView(view, _color.data(), _rect != nullptr ? 1 : 0, _rect != nullptr ? &rect : nullptr);
}

void DrawContext::ClearDepthIn(Texture& _texture, float _depth, const PixelRect* _rect)
{
  Native& context = *m_native;
  Texture::Native* const texture = _texture.m_native.get();
  // Put so that NaN fails it too.
  if (texture == nullptr || texture->desc.format != TextureFormat::Depth32F || !(_depth >= 0.0f && _depth <= 1.0f))
  {
    context.core.Fail(std::format("Direct3D 12: ClearDepth cannot clear {} to {}",
                                  texture != nullptr ? texture->name : std::string("an empty texture"), _depth));
    return;
  }
  const D3D12_RECT rect = _rect != nullptr ? ClippedRect(*_rect, _texture.WidthPixels(), _texture.HeightPixels()) : D3D12_RECT{0, 0, 1, 1};
  if (rect.left >= rect.right || rect.top >= rect.bottom)
  {
    return;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE view{};
  if (!context.Open() || !texture->DepthView(view))
  {
    return;
  }
  context.Require(*texture, 0, D3D12_RESOURCE_STATE_DEPTH_WRITE);
  context.FlushBarriers();
  context.list->ClearDepthStencilView(view, D3D12_CLEAR_FLAG_DEPTH, _depth, 0, _rect != nullptr ? 1 : 0,
                                      _rect != nullptr ? &rect : nullptr);
}

bool DrawContext::Initialize(std::string& _error)
{
  Native& context = *m_native;
  ID3D12Device& device = *context.core.device.Get();

  // The one root signature every liblt program draws with: its two stages' $Globals at b0, and
  // tables of t0 to t15 and s0 to s15 (ADR-007).
  const D3D12_DESCRIPTOR_RANGE shaderResources{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, Program::MAX_SHADER_RESOURCES, 0, 0, 0};
  const D3D12_DESCRIPTOR_RANGE samplers{D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, Program::MAX_SAMPLERS, 0, 0, 0};
  std::array<D3D12_ROOT_PARAMETER, 4> parameters{};
  parameters[VERTEX_CONSTANTS_PARAMETER].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[VERTEX_CONSTANTS_PARAMETER].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[PIXEL_CONSTANTS_PARAMETER].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[PIXEL_CONSTANTS_PARAMETER].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[SHADER_RESOURCES_PARAMETER].DescriptorTable = {1, &shaderResources};
  parameters[SAMPLERS_PARAMETER].DescriptorTable = {1, &samplers};
  const D3D12_ROOT_SIGNATURE_DESC signatureDesc{static_cast<UINT>(parameters.size()), parameters.data(), 0, nullptr,
                                                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT};
  ComPtr<ID3DBlob> signature;
  ComPtr<ID3DBlob> errors;
  HRESULT result = D3D12SerializeRootSignature(&signatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
  if (SUCCEEDED(result))
  {
    result =
      device.CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&context.graphicsSignature));
  }
  if (FAILED(result))
  {
    const std::string_view why = errors ? std::string_view(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
                                        : std::string_view("no message");
    _error = std::format("Direct3D 12: the root signature was not made (0x{:08x}): {}", static_cast<unsigned long>(result), why);
    return false;
  }
  context.graphicsSignature->SetName(L"NeuronClient graphics root signature");

  // The one every compute program runs with: its $Globals at b0, and tables of t0 to t15 and u0 to
  // u7 (ADR-007).
  const D3D12_DESCRIPTOR_RANGE computeReads{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, Program::MAX_SHADER_RESOURCES, 0, 0, 0};
  const D3D12_DESCRIPTOR_RANGE computeWrites{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, Program::MAX_UNORDERED_RESOURCES, 0, 0, 0};
  std::array<D3D12_ROOT_PARAMETER, 3> computeParameters{};
  computeParameters[COMPUTE_CONSTANTS_PARAMETER].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  computeParameters[COMPUTE_READS_PARAMETER].DescriptorTable = {1, &computeReads};
  computeParameters[COMPUTE_WRITES_PARAMETER].DescriptorTable = {1, &computeWrites};
  const D3D12_ROOT_SIGNATURE_DESC computeDesc{static_cast<UINT>(computeParameters.size()), computeParameters.data(), 0, nullptr,
                                              D3D12_ROOT_SIGNATURE_FLAG_NONE};
  signature.Reset();
  errors.Reset();
  result = D3D12SerializeRootSignature(&computeDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors);
  if (SUCCEEDED(result))
  {
    result =
      device.CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&context.computeSignature));
  }
  if (FAILED(result))
  {
    const std::string_view why = errors ? std::string_view(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize())
                                        : std::string_view("no message");
    _error = std::format("Direct3D 12: the compute root signature was not made (0x{:08x}): {}", static_cast<unsigned long>(result), why);
    return false;
  }
  context.computeSignature->SetName(L"NeuronClient compute root signature");
  const D3D12_COMPUTE_PIPELINE_STATE_DESC mipDesc{
    context.computeSignature.Get(), {GENERATE_MIPS_CS, sizeof(GENERATE_MIPS_CS)}, 0, {nullptr, 0}, D3D12_PIPELINE_STATE_FLAG_NONE};
  result = device.CreateComputePipelineState(&mipDesc, IID_PPV_ARGS(&context.mipPipeline));
  if (FAILED(result))
  {
    _error = std::format("Direct3D 12: the mip generator's pipeline state was not made (0x{:08x})", static_cast<unsigned long>(result));
    return false;
  }
  // The present pass's: a triangle over the back buffer, which reads the image through t0.
  const D3D12_GRAPHICS_PIPELINE_STATE_DESC presentDesc{
    .pRootSignature = context.graphicsSignature.Get(),
    .VS = {PRESENT_VS, sizeof(PRESENT_VS)},
    .PS = {PRESENT_PS, sizeof(PRESENT_PS)},
    .DS = {nullptr, 0},
    .HS = {nullptr, 0},
    .GS = {nullptr, 0},
    .StreamOutput = {nullptr, 0, nullptr, 0, 0},
    .BlendState = BlendDescription(BlendMode::Opaque),
    .SampleMask = std::numeric_limits<UINT>::max(),
    .RasterizerState = RasterizerDescription(CullMode::None, false),
    .DepthStencilState = DepthStencilDescription(false, false),
    .InputLayout = {nullptr, 0},
    .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .NumRenderTargets = 1,
    .RTVFormats = {BACK_BUFFER_FORMAT},
    .DSVFormat = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {1, 0},
    .NodeMask = 0,
    .CachedPSO = {nullptr, 0},
    .Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
  };
  result = device.CreateGraphicsPipelineState(&presentDesc, IID_PPV_ARGS(&context.presentPipeline));
  if (FAILED(result))
  {
    _error = std::format("Direct3D 12: the present pass's pipeline state was not made (0x{:08x})", static_cast<unsigned long>(result));
    return false;
  }

  // The null table, and room for at least two tables in the ring.
  const UINT shaderDescriptors = context.core.desc.shaderDescriptors;
  if (shaderDescriptors < 3 * TABLE_DESCRIPTORS)
  {
    _error =
      std::format("Direct3D 12: {} shader descriptors are too few; a device takes at least {}", shaderDescriptors, 3 * TABLE_DESCRIPTORS);
    return false;
  }
  const D3D12_DESCRIPTOR_HEAP_DESC shaderHeapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, shaderDescriptors,
                                                  D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  const D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc{D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, SAMPLER_DESCRIPTORS,
                                                   D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
  result = device.CreateDescriptorHeap(&shaderHeapDesc, IID_PPV_ARGS(&context.shaderHeap));
  if (SUCCEEDED(result))
  {
    result = device.CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&context.samplerHeap));
  }
  if (FAILED(result))
  {
    _error = std::format("Direct3D 12: ID3D12Device::CreateDescriptorHeap failed with 0x{:08x}", static_cast<unsigned long>(result));
    return false;
  }
  context.shaderHeap->SetName(L"NeuronClient shader descriptors");
  context.samplerHeap->SetName(L"NeuronClient samplers");

  // A table of null views for slots nothing is bound to, and one of point samplers.
  D3D12_SHADER_RESOURCE_VIEW_DESC nullView{};
  nullView.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  nullView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  nullView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  nullView.Texture2D.MipLevels = 1;
  const UINT shaderIncrement = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  const UINT samplerIncrement = device.GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  const D3D12_CPU_DESCRIPTOR_HANDLE shaderStart = context.shaderHeap->GetCPUDescriptorHandleForHeapStart();
  const D3D12_CPU_DESCRIPTOR_HANDLE samplerStart = context.samplerHeap->GetCPUDescriptorHandleForHeapStart();
  for (UINT slot = 0; slot < TABLE_DESCRIPTORS; ++slot)
  {
    device.CreateShaderResourceView(nullptr, &nullView, {shaderStart.ptr + (static_cast<SIZE_T>(slot) * shaderIncrement)});
  }
  for (UINT slot = 0; slot < TABLE_DESCRIPTORS; ++slot)
  {
    device.CreateSampler(&POINT_SAMPLER, {samplerStart.ptr + (static_cast<SIZE_T>(slot) * samplerIncrement)});
  }
  context.samplerTables.emplace(SamplerTable(), 0);

  // The null view the ring's tables copy into the slots nothing is bound to, which must be CPU-only.
  if (!context.core.shaderViewPool.Allocate(context.core, context.nullShaderView))
  {
    _error = "Direct3D 12: the null view's descriptor was not made";
    return false;
  }
  device.CreateShaderResourceView(nullptr, &nullView, context.nullShaderView);
  if (!context.core.shaderViewPool.Allocate(context.core, context.nullUnorderedView))
  {
    _error = "Direct3D 12: the null unordered view's descriptor was not made";
    return false;
  }
  D3D12_UNORDERED_ACCESS_VIEW_DESC nullUnordered{};
  nullUnordered.Format = DXGI_FORMAT_R32_FLOAT;
  nullUnordered.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  device.CreateUnorderedAccessView(nullptr, nullptr, &nullUnordered, context.nullUnorderedView);

  // What a program's inputs read when the layout has no attribute for them (plan §5.5).
  std::byte* defaults = nullptr;
  if (!context.CreateUploadBuffer(sizeof(DEFAULT_ATTRIBUTE), context.defaultAttribute, defaults))
  {
    _error = "Direct3D 12: the default vertex attribute's buffer was not made";
    return false;
  }
  std::memcpy(defaults, DEFAULT_ATTRIBUTE.data(), sizeof(DEFAULT_ATTRIBUTE));
  context.defaultAttribute->SetName(L"NeuronClient default vertex attribute");
  context.shaderIncrement = shaderIncrement;
  context.samplerIncrement = samplerIncrement;
  context.ringCapacity = shaderDescriptors - TABLE_DESCRIPTORS;
  return true;
}

void DrawContext::Forget(const Texture::Native& _texture) noexcept
{
  Native& context = *m_native;
  for (Native::Target& target : context.colorTargets)
  {
    if (target.texture == &_texture)
    {
      target.texture = nullptr;
    }
  }
  if (context.depthTarget == &_texture)
  {
    context.depthTarget = nullptr;
  }
  for (Texture::Native*& texture : context.textures)
  {
    if (texture == &_texture)
    {
      texture = nullptr;
    }
  }
  for (Native::Unordered& write : context.unordered)
  {
    if (write.texture == &_texture)
    {
      write.texture = nullptr;
    }
  }
  // Another texture may be made where this one was, so the last table is not taken again.
  context.tableList = 0;
}

void DrawContext::Forget(const Program::Native& _program)
{
  Native& context = *m_native;
  if (context.program == &_program)
  {
    context.program = nullptr;
  }
  // Recorded draws and dispatches may still use them.
  if (const auto compute = context.computePipelines.find(_program.id); compute != context.computePipelines.end())
  {
    context.core.DeferRelease([held = std::move(compute->second)] {});
    context.computePipelines.erase(compute);
  }
  for (auto pipeline = context.pipelines.begin(); pipeline != context.pipelines.end();)
  {
    if (pipeline->first.program == _program.id)
    {
      context.core.DeferRelease([held = std::move(pipeline->second)] {});
      pipeline = context.pipelines.erase(pipeline);
    }
    else
    {
      ++pipeline;
    }
  }
}

std::size_t DrawContext::PipelineStates() const noexcept
{
  return m_native->pipelines.size() + m_native->computePipelines.size();
}

bool DrawContext::RecordPresent(const Texture& _image, PresentTarget& _target)
{
  Native& context = *m_native;
  Texture::Native* const image = _image.m_native.get();
  if (image == nullptr || image->desc.dimension != TextureDimension::Texture2D || image->desc.format == TextureFormat::Depth32F)
  {
    context.core.Fail(std::format("Direct3D 12: Present cannot show {}; only a 2D colour texture is shown", Native::NameOf(image)));
    return false;
  }
  return context.RecordPresent(*image, _target);
}

void DrawContext::GenerateMips(Texture& _texture)
{
  Native& context = *m_native;
  Texture::Native* const texture = _texture.m_native.get();
  if (texture == nullptr || texture->desc.dimension == TextureDimension::Texture3D || texture->desc.format == TextureFormat::Depth32F)
  {
    context.core.Fail(std::format("Direct3D 12: GenerateMips cannot make the mips of {}; only a 2D or cube colour texture's are made",
                                  Native::NameOf(texture)));
    return;
  }
  // Each mip is made from the one above once that is done: the transition from unordered access
  // to reading orders them.
  for (std::uint32_t mip = 1; mip < texture->desc.mipLevels; ++mip)
  {
    const std::array<std::uint32_t, 4> sizes = {_texture.WidthPixels(mip - 1), _texture.HeightPixels(mip - 1), _texture.WidthPixels(mip),
                                                _texture.HeightPixels(mip)};
    for (std::uint32_t face = 0; face < texture->faces; ++face)
    {
      if (!context.MakeMip(*texture, mip, face, sizes))
      {
        return;
      }
    }
  }
}

void DrawContext::SetTargets(std::span<const ColorTarget> _colors, Texture* _depth)
{
  Native& context = *m_native;
  context.colorTargetCount = 0;
  context.depthTarget = nullptr;
  const auto fail = [&context](std::string_view _why)
  {
    context.core.Fail(std::format("Direct3D 12: SetTargets: {}; no target is set", _why));
    context.colorTargetCount = 0;
    context.depthTarget = nullptr;
  };
  if (_colors.size() > MAX_COLOR_TARGETS)
  {
    fail(std::format("{} colour targets, where {} can be", _colors.size(), MAX_COLOR_TARGETS));
    return;
  }
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  for (std::size_t index = 0; index < _colors.size(); ++index)
  {
    const ColorTarget& color = _colors[index];
    Texture::Native* const texture = color.texture != nullptr ? color.texture->m_native.get() : nullptr;
    const bool volume = texture != nullptr && texture->desc.dimension == TextureDimension::Texture3D;
    if (texture == nullptr || texture->desc.format == TextureFormat::Depth32F || color.mip >= texture->desc.mipLevels ||
        color.layer >= (volume ? color.texture->DepthPixels(color.mip) : texture->faces))
    {
      fail(std::format("colour target {}, mip {} of layer {} of {}, is not a colour texture's", index, color.mip, color.layer,
                       Native::NameOf(texture)));
      return;
    }
    const std::uint32_t targetWidth = color.texture->WidthPixels(color.mip);
    const std::uint32_t targetHeight = color.texture->HeightPixels(color.mip);
    if (index == 0)
    {
      width = targetWidth;
      height = targetHeight;
    }
    else if (targetWidth != width || targetHeight != height)
    {
      fail(std::format("colour target {} is {} by {}, and target 0 is {} by {}", index, targetWidth, targetHeight, width, height));
      return;
    }
    context.colorTargets[index] = {texture, color.mip, color.layer};
  }
  Texture::Native* depth = nullptr;
  if (_depth != nullptr)
  {
    depth = _depth->m_native.get();
    if (depth == nullptr || depth->desc.format != TextureFormat::Depth32F)
    {
      fail(std::format("{} is not a depth texture", Native::NameOf(depth)));
      return;
    }
    context.depthWidth = _depth->WidthPixels();
    context.depthHeight = _depth->HeightPixels();
    if (_colors.empty())
    {
      width = context.depthWidth;
      height = context.depthHeight;
    }
  }
  context.colorTargetCount = static_cast<std::uint32_t>(_colors.size());
  context.depthTarget = depth;
  context.targetWidth = width;
  context.targetHeight = height;
  SetViewport(0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
  DisableScissor();
}

void DrawContext::SetViewport(std::int32_t _x, std::int32_t _y, std::int32_t _width, std::int32_t _height)
{
  // Row 0 is the bottom row, where GL's viewport starts, once images are stored bottom row first
  // (plan §5.5).
  m_native->viewport = {
    static_cast<float>(_x), static_cast<float>(_y), static_cast<float>(_width), static_cast<float>(_height), 0.0f, 1.0f};
}

void DrawContext::SetScissor(std::int32_t _x, std::int32_t _y, std::int32_t _width, std::int32_t _height)
{
  m_native->scissor = {_x, _y, _x + _width, _y + _height};
}

void DrawContext::DisableScissor()
{
  Native& context = *m_native;
  context.scissor = {0, 0, static_cast<LONG>(context.targetWidth), static_cast<LONG>(context.targetHeight)};
}

void DrawContext::SetState(const RenderState& _state)
{
  m_native->state = _state;
}

void DrawContext::SetProgram(Program& _program)
{
  Native& context = *m_native;
  Program::Native* const program = _program.m_native.get();
  if (program == nullptr || program->compute)
  {
    context.core.Fail(std::format("Direct3D 12: SetProgram was given {}, which draws nothing",
                                  program != nullptr ? program->name : std::string("an empty program")));
    context.program = nullptr;
    return;
  }
  context.program = program;
}

void DrawContext::SetTexture(std::uint32_t _slot, const Texture* _texture)
{
  Native& context = *m_native;
  if (_slot >= Program::MAX_SHADER_RESOURCES)
  {
    context.core.Fail(std::format("Direct3D 12: SetTexture was given t{}, past t{}", _slot, Program::MAX_SHADER_RESOURCES - 1));
    return;
  }
  context.textures[_slot] = _texture != nullptr ? _texture->m_native.get() : nullptr;
}

void DrawContext::SetSampler(std::uint32_t _slot, const SamplerDesc& _sampler)
{
  Native& context = *m_native;
  if (_slot >= Program::MAX_SAMPLERS)
  {
    context.core.Fail(std::format("Direct3D 12: SetSampler was given s{}, past s{}", _slot, Program::MAX_SAMPLERS - 1));
    return;
  }
  context.samplers[_slot] = SamplerDescription(_sampler);
}

void DrawContext::SetUnorderedTexture(std::uint32_t _slot, Texture* _texture, std::uint32_t _mip)
{
  Native& context = *m_native;
  Texture::Native* const texture = _texture != nullptr ? _texture->m_native.get() : nullptr;
  if (_slot >= Program::MAX_UNORDERED_RESOURCES || (texture != nullptr && (!texture->IsUnordered() || _mip >= texture->desc.mipLevels)))
  {
    context.core.Fail(std::format("Direct3D 12: SetUnorderedTexture cannot set mip {} of {} at u{}; only 3D textures and those with "
                                  "mips are written so, at u0 to u{}",
                                  _mip, Native::NameOf(texture), _slot, Program::MAX_UNORDERED_RESOURCES - 1));
    return;
  }
  context.unordered[_slot] = {texture, _mip};
}

void DrawContext::Dispatch(Program& _program, std::uint32_t _groupsX, std::uint32_t _groupsY, std::uint32_t _groupsZ)
{
  Native& context = *m_native;
  const Program::Native* const program = _program.m_native.get();
  const auto fail = [&context, program](std::string_view _why)
  {
    context.core.Fail(std::format("Direct3D 12: a dispatch of {} {}; nothing was recorded",
                                  program != nullptr ? program->name : std::string("an empty program"), _why));
  };
  // D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION, which not every SDK's headers have.
  constexpr std::uint32_t MAX_GROUPS = 65535;
  if (program == nullptr || !program->compute)
  {
    fail("which is no compute program");
    return;
  }
  if (_groupsX == 0 || _groupsY == 0 || _groupsZ == 0 || _groupsX > MAX_GROUPS || _groupsY > MAX_GROUPS || _groupsZ > MAX_GROUPS)
  {
    fail(std::format("in {} by {} by {} groups, where each is from 1 to {}", _groupsX, _groupsY, _groupsZ, MAX_GROUPS));
    return;
  }

  // What it reads and writes, which may not be the same texture.
  std::array<Texture::Native*, TABLE_DESCRIPTORS> read{};
  for (const auto& [name, slot] : program->shaderResources)
  {
    read[static_cast<std::size_t>(slot)] = context.textures[static_cast<std::size_t>(slot)];
  }
  std::array<Native::Unordered, Program::MAX_UNORDERED_RESOURCES> written{};
  for (const auto& [name, slot] : program->unorderedResources)
  {
    const Native::Unordered& write = context.unordered[static_cast<std::size_t>(slot)];
    if (write.texture == nullptr)
    {
      fail(std::format("writes {} at u{}, where no texture is set", name, slot));
      return;
    }
    if (std::ranges::find(read, write.texture) != read.end())
    {
      fail(std::format("reads and writes {}", write.texture->name));
      return;
    }
    written[static_cast<std::size_t>(slot)] = write;
  }
  ID3D12PipelineState* const pipeline = context.ComputePipelineFor(*program);
  if (pipeline == nullptr)
  {
    return;
  }

  // The views, in tables from the ring before the list opens: making room may submit it.
  std::array<D3D12_CPU_DESCRIPTOR_HANDLE, TABLE_DESCRIPTORS> readViews{};
  for (std::size_t slot = 0; slot < read.size(); ++slot)
  {
    if (read[slot] != nullptr && !read[slot]->ShaderView(readViews[slot]))
    {
      return;
    }
  }
  std::array<D3D12_CPU_DESCRIPTOR_HANDLE, Program::MAX_UNORDERED_RESOURCES> writeViews{};
  for (std::size_t slot = 0; slot < written.size(); ++slot)
  {
    if (written[slot].texture != nullptr && !written[slot].texture->UnorderedView(written[slot].mip, writeViews[slot]))
    {
      return;
    }
  }
  const UINT readTable = context.AllocateTables(2);
  const UINT writeTable = readTable + TABLE_DESCRIPTORS;
  context.WriteTable(readTable, readViews, context.nullShaderView);
  context.WriteTable(writeTable, writeViews, context.nullUnorderedView);
  if (!context.Open())
  {
    return;
  }
  context.ReadyHeaps();
  if (!context.computeReady)
  {
    context.list->SetComputeRootSignature(context.computeSignature.Get());
    context.computeReady = true;
  }
  const std::vector<std::byte>& values = program->constantValues[static_cast<std::size_t>(ShaderStage::Compute)];
  const std::uint64_t sizeBytes = std::max<std::uint64_t>(values.size(), 16);
  Upload constants;
  if (!context.AllocateUpload(sizeBytes, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, constants))
  {
    return;
  }
  std::memset(constants.cpu, 0, static_cast<std::size_t>(sizeBytes));
  if (!values.empty())
  {
    std::memcpy(constants.cpu, values.data(), values.size());
  }

  for (Texture::Native* const texture : read)
  {
    if (texture != nullptr)
    {
      context.Require(*texture, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
  }
  for (const Native::Unordered& write : written)
  {
    if (write.texture == nullptr)
    {
      continue;
    }
    // A mip that is already unordered was written by an earlier dispatch, which this one waits for.
    Texture::Native& texture = *write.texture;
    const std::uint32_t layers = texture.desc.dimension == TextureDimension::TextureCube ? texture.faces : 1;
    bool waits = false;
    for (std::uint32_t layer = 0; layer < layers; ++layer)
    {
      const std::uint32_t subresource = texture.Subresource(write.mip, layer);
      waits = waits || texture.states[subresource] == D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      context.Require(texture, subresource, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    if (waits)
    {
      D3D12_RESOURCE_BARRIER barrier{};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
      barrier.UAV.pResource = texture.resource.Get();
      context.barriers.push_back(barrier);
    }
  }
  context.FlushBarriers();

  ID3D12GraphicsCommandList& list = *context.list.Get();
  list.SetPipelineState(pipeline);
  list.SetComputeRootConstantBufferView(COMPUTE_CONSTANTS_PARAMETER, constants.buffer->GetGPUVirtualAddress() + constants.offsetBytes);
  list.SetComputeRootDescriptorTable(COMPUTE_READS_PARAMETER, context.ShaderTableHandle(readTable));
  list.SetComputeRootDescriptorTable(COMPUTE_WRITES_PARAMETER, context.ShaderTableHandle(writeTable));
  list.Dispatch(_groupsX, _groupsY, _groupsZ);
}

std::uint64_t DrawContext::RequestRead(const Texture& _texture, std::uint32_t _mip, std::uint32_t _face)
{
  Native& context = *m_native;
  Texture::Native* const texture = _texture.m_native.get();
  if (texture == nullptr || _mip >= texture->desc.mipLevels || _face >= texture->faces)
  {
    context.core.Fail(std::format("Direct3D 12: RequestRead cannot read mip {} of face {} of {}", _mip, _face, Native::NameOf(texture)));
    return 0;
  }
  const std::uint32_t subresource = texture->Subresource(_mip, _face);
  Native::PendingRead read;
  read.footprint = context.FootprintOf(*texture, subresource);
  read.heightPixels = _texture.HeightPixels(_mip);
  read.depthPixels = _texture.DepthPixels(_mip);
  if (!context.Open() || !context.CreateBuffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, read.footprint.totalBytes,
                                               read.buffer, "a readback buffer"))
  {
    return 0;
  }
  context.Require(*texture, subresource, D3D12_RESOURCE_STATE_COPY_SOURCE);
  context.FlushBarriers();
  const D3D12_TEXTURE_COPY_LOCATION destination = FootprintLocation(read.buffer.Get(), read.footprint.placed);
  const D3D12_TEXTURE_COPY_LOCATION source = SubresourceLocation(texture->resource.Get(), subresource);
  context.list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  read.ticket = context.nextReadTicket++;
  context.reads.push_back(std::move(read));
  return context.reads.back().ticket;
}

bool DrawContext::TakeRead(std::uint64_t _ticket, std::vector<std::byte>& _outTexels)
{
  Native& context = *m_native;
  const auto read = std::ranges::find(context.reads, _ticket, &Native::PendingRead::ticket);
  if (read == context.reads.end())
  {
    context.core.Fail(std::format("Direct3D 12: TakeRead was given ticket {}, which is spent or was never given", _ticket));
    return false;
  }
  if (read->fenceValue == 0 || context.core.Completed() < read->fenceValue)
  {
    return false;
  }
  const Footprint& footprint = read->footprint;
  const D3D12_RANGE everything{0, static_cast<SIZE_T>(footprint.totalBytes)};
  void* mapped = nullptr;
  if (!context.core.Check(read->buffer->Map(0, &everything, &mapped), "ID3D12Resource::Map for a readback buffer"))
  {
    context.reads.erase(read);
    return false;
  }
  std::vector<std::byte> texels(static_cast<std::size_t>(footprint.rowBytes * read->heightPixels * read->depthPixels));
  CopyRows(texels.data(), footprint.rowBytes, read->heightPixels, static_cast<const std::byte*>(mapped),
           footprint.placed.Footprint.RowPitch, footprint.rows, footprint.rowBytes, read->heightPixels, read->depthPixels);
  const D3D12_RANGE nothingWritten{0, 0};
  read->buffer->Unmap(0, &nothingWritten);
  context.reads.erase(read);
  _outTexels = std::move(texels);
  return true;
}

void DrawContext::DrawIndexed(const Buffer& _vertices, const VertexLayout& _layout, const Buffer& _indices, IndexFormat _format,
                              std::uint32_t _firstIndex, std::uint32_t _indexCount)
{
  Native& context = *m_native;
  Buffer::Native* const vertices = _vertices.m_native.get();
  Buffer::Native* const indices = _indices.m_native.get();
  const std::uint64_t indexBytes = _format == IndexFormat::UInt16 ? 2 : 4;
  if (vertices == nullptr || indices == nullptr || _layout.strideBytes == 0 || _indexCount == 0 ||
      (std::uint64_t{_firstIndex} + _indexCount) * indexBytes > indices->desc.sizeBytes)
  {
    context.FailDraw(std::format("was given buffers without indices {} to {}", _firstIndex, std::uint64_t{_firstIndex} + _indexCount));
    return;
  }
  ID3D12PipelineState* pipeline = nullptr;
  if (!context.PrepareDraw(_layout, pipeline))
  {
    return;
  }
  // One buffer may hold both.
  if (vertices == indices)
  {
    context.Require(*vertices, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER);
  }
  else
  {
    context.Require(*vertices, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    context.Require(*indices, D3D12_RESOURCE_STATE_INDEX_BUFFER);
  }
  const D3D12_VERTEX_BUFFER_VIEW vertexView{vertices->resource->GetGPUVirtualAddress(), vertices->desc.sizeBytes, _layout.strideBytes};
  const D3D12_INDEX_BUFFER_VIEW indexView{indices->resource->GetGPUVirtualAddress(), indices->desc.sizeBytes,
                                          _format == IndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
  context.RecordDraw(pipeline, vertexView, indexView, _firstIndex, _indexCount);
}

void DrawContext::DrawTransient(std::span<const std::byte> _vertices, const VertexLayout& _layout, std::span<const std::byte> _indices,
                                IndexFormat _format)
{
  Native& context = *m_native;
  const std::size_t indexBytes = _format == IndexFormat::UInt16 ? 2 : 4;
  if (_vertices.empty() || _indices.empty() || _indices.size() % indexBytes != 0 || _layout.strideBytes == 0 ||
      _vertices.size() > std::numeric_limits<UINT>::max() || _indices.size() > std::numeric_limits<UINT>::max())
  {
    context.FailDraw(std::format("was given {} bytes of vertices and {} of indices", _vertices.size(), _indices.size()));
    return;
  }
  ID3D12PipelineState* pipeline = nullptr;
  if (!context.PrepareDraw(_layout, pipeline))
  {
    return;
  }
  // Uploaded after PrepareDraw, which may submit the open list.
  Upload vertexUpload;
  Upload indexUpload;
  if (!context.AllocateUpload(_vertices.size(), BUFFER_UPLOAD_ALIGNMENT_BYTES, vertexUpload) ||
      !context.AllocateUpload(_indices.size(), BUFFER_UPLOAD_ALIGNMENT_BYTES, indexUpload))
  {
    return;
  }
  std::memcpy(vertexUpload.cpu, _vertices.data(), _vertices.size());
  std::memcpy(indexUpload.cpu, _indices.data(), _indices.size());
  const D3D12_VERTEX_BUFFER_VIEW vertexView{vertexUpload.buffer->GetGPUVirtualAddress() + vertexUpload.offsetBytes,
                                            static_cast<UINT>(_vertices.size()), _layout.strideBytes};
  const D3D12_INDEX_BUFFER_VIEW indexView{indexUpload.buffer->GetGPUVirtualAddress() + indexUpload.offsetBytes,
                                          static_cast<UINT>(_indices.size()),
                                          _format == IndexFormat::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT};
  context.RecordDraw(pipeline, vertexView, indexView, 0, static_cast<std::uint32_t>(_indices.size() / indexBytes));
}

void DrawContext::Flush()
{
  m_native->Submit();
}

void DrawContext::BeginEvent(std::string_view _name)
{
  Native& context = *m_native;
  context.events.push_back(Utf8ToUtf16(_name));
  if (context.open)
  {
    context.BeginEventOnList(context.events.back());
  }
}

void DrawContext::EndEvent()
{
  Native& context = *m_native;
  if (context.events.empty())
  {
    context.core.Fail("Direct3D 12: EndEvent was called with no event begun");
    return;
  }
  context.events.pop_back();
  if (context.open)
  {
    context.list->EndEvent();
  }
}

} // namespace Neuron
