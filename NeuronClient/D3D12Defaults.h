#pragma once

namespace Neuron
{

// The D3D12 descriptor structures this renderer builds, with every field stated.
//
// `D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {}` is the idiom every D3D12 sample uses, and it is
// wrong in a way that is easy to miss: several of the enums nested inside it have no enumerator
// equal to zero. D3D12_FILL_MODE starts at 2, D3D12_BLEND and D3D12_STENCIL_OP at 1,
// D3D12_COMPARISON_FUNC at 1. Zero-initializing them leaves a value the type does not have, which
// is what clang-tidy's bugprone-invalid-enum-default-initialization reports. It happens to work
// because the runtime ignores those fields while the matching Enable flag is FALSE -- which is a
// description of today's driver, not a guarantee.
//
// The usual answer is d3dx12.h's CD3DX12_ helpers. That header is not in the Windows SDK and
// R14 closes the door on fetching it, so the defaults are spelled out here instead. It is more
// lines and it is better lines: every value a pipeline is built on is visible and greppable.

[[nodiscard]] inline D3D12_RENDER_TARGET_BLEND_DESC DisabledBlend() noexcept
{
  return D3D12_RENDER_TARGET_BLEND_DESC{
    .BlendEnable = FALSE,
    .LogicOpEnable = FALSE,
    .SrcBlend = D3D12_BLEND_ONE,
    .DestBlend = D3D12_BLEND_ZERO,
    .BlendOp = D3D12_BLEND_OP_ADD,
    .SrcBlendAlpha = D3D12_BLEND_ONE,
    .DestBlendAlpha = D3D12_BLEND_ZERO,
    .BlendOpAlpha = D3D12_BLEND_OP_ADD,
    .LogicOp = D3D12_LOGIC_OP_NOOP,
    .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
  };
}

/// No blending in any pass this game has. Every surface it draws is opaque and the text pass
/// discards the pixels it does not want rather than blending them away, so blending would be a
/// state nothing sets and everything pays for.
///
/// It is a DEFAULT and no longer a prohibition. Until ADR-011 the render target held palette
/// indices, and blending two of them produced an unrelated third color -- so the restriction was
/// structural. With an R8G8B8A8 target a pass that genuinely wants to blend can set its own
/// D3D12_BLEND_DESC; it is a decision to record, not an impossibility.
[[nodiscard]] inline D3D12_BLEND_DESC OpaqueBlendState() noexcept
{
  return D3D12_BLEND_DESC{
    .AlphaToCoverageEnable = FALSE,
    .IndependentBlendEnable = FALSE,
    .RenderTarget = {DisabledBlend(), DisabledBlend(), DisabledBlend(), DisabledBlend(), DisabledBlend(), DisabledBlend(), DisabledBlend(),
                     DisabledBlend()},
  };
}

[[nodiscard]] inline D3D12_RENDER_TARGET_BLEND_DESC StraightAlphaBlend() noexcept
{
  return D3D12_RENDER_TARGET_BLEND_DESC{
    .BlendEnable = TRUE,
    .LogicOpEnable = FALSE,
    .SrcBlend = D3D12_BLEND_SRC_ALPHA,
    .DestBlend = D3D12_BLEND_INV_SRC_ALPHA,
    .BlendOp = D3D12_BLEND_OP_ADD,
    // The target is opaque and stays opaque: the destination alpha is whatever the swap chain
    // started with and nothing reads it, so the alpha channel is written as ONE rather than
    // composited. A back buffer that accumulated fractional alpha would present correctly today
    // and wrongly the first time anything sampled it.
    .SrcBlendAlpha = D3D12_BLEND_ONE,
    .DestBlendAlpha = D3D12_BLEND_ZERO,
    .BlendOpAlpha = D3D12_BLEND_OP_ADD,
    .LogicOp = D3D12_LOGIC_OP_NOOP,
    .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL,
  };
}

/// Straight (non-premultiplied) source-over blending, for the interface passes and nothing else.
///
/// THIS IS THE EXCEPTION R12 ASKS TO BE RECORDED, and ADR-014 is the record. The scene passes --
/// meshes, the starfield -- stay opaque: every surface in the world is one of two authored tones
/// and there is nothing there to blend (ADR-012). The interface is different in kind. Its tokens
/// are half a dozen translucent greys over known backgrounds (a card fill at 4% white, a muted
/// caption at 55%, a lane at 28%), and the alternative -- compositing each one against its
/// backdrop on the CPU -- stops working the moment a lane crosses the map's gradient, which is
/// the first thing the map does.
[[nodiscard]] inline D3D12_BLEND_DESC InterfaceBlendState() noexcept
{
  return D3D12_BLEND_DESC{
    .AlphaToCoverageEnable = FALSE,
    .IndependentBlendEnable = FALSE,
    .RenderTarget = {StraightAlphaBlend(), DisabledBlend(), DisabledBlend(), DisabledBlend(), DisabledBlend(), DisabledBlend(),
                     DisabledBlend(), DisabledBlend()},
  };
}

[[nodiscard]] inline D3D12_RASTERIZER_DESC SolidRasterizer(D3D12_CULL_MODE _cullMode) noexcept
{
  return D3D12_RASTERIZER_DESC{
    .FillMode = D3D12_FILL_MODE_SOLID,
    .CullMode = _cullMode,
    .FrontCounterClockwise = FALSE,
    .DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
    .DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
    .SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
    .DepthClipEnable = TRUE,
    // No multisampling and no line antialiasing. The picture is built out of flat faces meeting at
    // hard edges (ADR-012) and the whole path from vertex to pixel is free of resampling
    // (ADR-011); turning either of these on is a look to choose deliberately, in an ADR, rather
    // than a default to inherit.
    .MultisampleEnable = FALSE,
    .AntialiasedLineEnable = FALSE,
    .ForcedSampleCount = 0,
    .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF,
  };
}

[[nodiscard]] inline D3D12_DEPTH_STENCILOP_DESC KeepStencil() noexcept
{
  return D3D12_DEPTH_STENCILOP_DESC{
    .StencilFailOp = D3D12_STENCIL_OP_KEEP,
    .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
    .StencilPassOp = D3D12_STENCIL_OP_KEEP,
    .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS,
  };
}

/// _depthEnabled false is the 2D passes (the resolve, the text); true is the mesh pass. There is
/// no stencil in this game either way.
[[nodiscard]] inline D3D12_DEPTH_STENCIL_DESC DepthState(bool _depthEnabled) noexcept
{
  return D3D12_DEPTH_STENCIL_DESC{
    .DepthEnable = _depthEnabled ? TRUE : FALSE,
    .DepthWriteMask = _depthEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO,
    .DepthFunc = D3D12_COMPARISON_FUNC_LESS,
    .StencilEnable = FALSE,
    .StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
    .StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
    .FrontFace = KeepStencil(),
    .BackFace = KeepStencil(),
  };
}

/// A pipeline with every enum at a value its type actually has. Callers set the shaders, the root
/// signature, the render target format and whatever else their pass needs.
[[nodiscard]] inline D3D12_GRAPHICS_PIPELINE_STATE_DESC DefaultGraphicsPipeline() noexcept
{
  return D3D12_GRAPHICS_PIPELINE_STATE_DESC{
    .pRootSignature = nullptr,
    .VS = {},
    .PS = {},
    .DS = {},
    .HS = {},
    .GS = {},
    .StreamOutput = {},
    .BlendState = OpaqueBlendState(),
    .SampleMask = UINT_MAX,
    .RasterizerState = SolidRasterizer(D3D12_CULL_MODE_NONE),
    .DepthStencilState = DepthState(false),
    .InputLayout = {},
    .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .NumRenderTargets = 1,
    .RTVFormats = {},
    .DSVFormat = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .NodeMask = 0,
    .CachedPSO = {},
    .Flags = D3D12_PIPELINE_STATE_FLAG_NONE,
  };
}

/// D3D12_HEAP_TYPE has no zero enumerator either, so this cannot be an `= {}` on the caller side.
[[nodiscard]] inline D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE _type) noexcept
{
  return D3D12_HEAP_PROPERTIES{
    .Type = _type,
    .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
    .CreationNodeMask = 1,
    .VisibleNodeMask = 1,
  };
}

[[nodiscard]] inline D3D12_RESOURCE_DESC Texture2DDesc(DXGI_FORMAT _format, std::uint32_t _widthTexels, std::uint32_t _heightTexels,
                                                       D3D12_RESOURCE_FLAGS _flags) noexcept
{
  return D3D12_RESOURCE_DESC{
    .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
    .Alignment = 0,
    .Width = _widthTexels,
    .Height = _heightTexels,
    .DepthOrArraySize = 1,
    .MipLevels = 1,
    .Format = _format,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
    .Flags = _flags,
  };
}

[[nodiscard]] inline D3D12_RESOURCE_DESC BufferDesc(std::uint64_t _sizeBytes) noexcept
{
  return D3D12_RESOURCE_DESC{
    .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
    .Alignment = 0,
    .Width = _sizeBytes,
    .Height = 1,
    .DepthOrArraySize = 1,
    .MipLevels = 1,
    .Format = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    .Flags = D3D12_RESOURCE_FLAG_NONE,
  };
}

[[nodiscard]] inline D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* _resource, D3D12_RESOURCE_STATES _before,
                                                              D3D12_RESOURCE_STATES _after) noexcept
{
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = _resource;
  barrier.Transition.StateBefore = _before;
  barrier.Transition.StateAfter = _after;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  return barrier;
}

} // namespace Neuron
