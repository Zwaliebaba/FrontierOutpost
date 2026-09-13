// FontBackend.cpp -- the baked atlas uploaded once, and the Direct3D 12 pipeline a FontRenderer's
// quads are drawn through (ADR-075). The glyphs, the metrics and the wrapping are FontRenderer's,
// and that file names no graphics API.

#include "pch.h"
#include "FontBackend.h"

#include "D3D12Defaults.h"
#include "DescriptorHeap.h"
#include "Device.h"
#include "SceneTarget.h"

#include "CompiledShaders/TextPS.h"
#include "CompiledShaders/TextVS.h"

namespace Neuron
{

namespace
{

// One texel per glyph pixel, every face shelf-packed into one texture by Build/BakeFont.py. One
// texture rather than one per face, because a face is chosen per DRAW CALL and a texture swap
// between two words would break the single batch the text pass is (ADR-014).
constexpr std::uint32_t ATLAS_WIDTH_TEXELS = FONT_ATLAS_WIDTH;
constexpr std::uint32_t ATLAS_HEIGHT_TEXELS = FONT_ATLAS_HEIGHT;
// R8_UNORM rather than R8_UINT: the byte is COVERAGE, and the pixel shader wants it as 0..1 to
// multiply into the string's alpha (ADR-074).
constexpr DXGI_FORMAT ATLAS_FORMAT = DXGI_FORMAT_R8_UNORM;

} // namespace

void FontBackend::Create(Device& _device, DescriptorHeap& _shaderVisibleHeap)
{
  m_shaderVisibleHeap = &_shaderVisibleHeap;

  CreateAtlas(_device, _shaderVisibleHeap);
  CreateVertexBuffer(_device.Handle());
  CreatePipeline(_device.Handle());
}

void FontBackend::CreateAtlas(Device& _device, DescriptorHeap& _shaderVisibleHeap)
{
  ID3D12Device* device = _device.Handle();

  // Font.h IS the atlas now: Build/BakeFont.py packed it and wrote the texels out in the layout the
  // glyph table's atlasX/atlasY already point into, so there is nothing to unpack here. The old
  // loop that turned 768 bytes of bit-packed rows into texels lived here because the hand-typed
  // font stored one BIT a pixel; a baked face stores one byte, and the copy below is the whole job.
  const std::vector<std::uint8_t> texels(FONT_ATLAS.begin(), FONT_ATLAS.end());

  const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC atlasDesc = Texture2DDesc(ATLAS_FORMAT, ATLAS_WIDTH_TEXELS, ATLAS_HEIGHT_TEXELS, D3D12_RESOURCE_FLAG_NONE);
  winrt::check_hresult(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &atlasDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                       nullptr, IID_PPV_ARGS(m_atlas.put())));

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
  UINT rowCount = 0;
  UINT64 rowSizeBytes = 0;
  UINT64 uploadSizeBytes = 0;
  device->GetCopyableFootprints(&atlasDesc, 0, 1, 0, &footprint, &rowCount, &rowSizeBytes, &uploadSizeBytes);

  const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_RESOURCE_DESC stagingDesc = BufferDesc(uploadSizeBytes);
  winrt::com_ptr<ID3D12Resource> staging;
  winrt::check_hresult(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &stagingDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                       nullptr, IID_PPV_ARGS(staging.put())));

  std::uint8_t* mapped = nullptr;
  const D3D12_RANGE readNothing = {0, 0};
  winrt::check_hresult(staging->Map(0, &readNothing, reinterpret_cast<void**>(&mapped)));
  for (UINT row = 0; row < rowCount; ++row)
  {
    std::memcpy(mapped + footprint.Offset + static_cast<std::size_t>(row) * footprint.Footprint.RowPitch,
                texels.data() + static_cast<std::size_t>(row) * ATLAS_WIDTH_TEXELS, static_cast<std::size_t>(rowSizeBytes));
  }
  staging->Unmap(0, nullptr);

  // A one-shot command list and a fence wait, all inside Create. This is the only upload the
  // client does, it happens once before the first frame, and a few milliseconds of startup is
  // cheaper than the machinery for tracking a copy that is still in flight. R2's "add the layer
  // when a second thing needs it" -- when the mesh needs a default-heap copy too, this becomes a
  // type.
  winrt::com_ptr<ID3D12CommandAllocator> allocator;
  winrt::com_ptr<ID3D12GraphicsCommandList> commandList;
  winrt::check_hresult(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.put())));
  winrt::check_hresult(
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.get(), nullptr, IID_PPV_ARGS(commandList.put())));

  D3D12_TEXTURE_COPY_LOCATION destination = {};
  destination.pResource = m_atlas.get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  destination.SubresourceIndex = 0;

  D3D12_TEXTURE_COPY_LOCATION source = {};
  source.pResource = staging.get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = footprint;

  commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

  const D3D12_RESOURCE_BARRIER toShaderResource =
    TransitionBarrier(m_atlas.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  commandList->ResourceBarrier(1, &toShaderResource);
  winrt::check_hresult(commandList->Close());

  ID3D12CommandList* lists[] = {commandList.get()};
  _device.Queue()->ExecuteCommandLists(1, lists);

  winrt::com_ptr<ID3D12Fence> fence;
  winrt::check_hresult(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put())));
  const winrt::handle uploaded{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
  if (!uploaded)
  {
    winrt::throw_last_error();
  }
  winrt::check_hresult(_device.Queue()->Signal(fence.get(), 1));
  winrt::check_hresult(fence->SetEventOnCompletion(1, uploaded.get()));
  WaitForSingleObjectEx(uploaded.get(), INFINITE, FALSE);
  // staging is released here, after the copy has demonstrably finished.

  m_atlasSlot = _shaderVisibleHeap.Allocate();
  D3D12_SHADER_RESOURCE_VIEW_DESC atlasView = {};
  atlasView.Format = ATLAS_FORMAT;
  atlasView.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  atlasView.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  atlasView.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(m_atlas.get(), &atlasView, _shaderVisibleHeap.CpuHandle(m_atlasSlot));
}

void FontBackend::CreateVertexBuffer(ID3D12Device* _device)
{
  const std::uint64_t sizeBytes =
    static_cast<std::uint64_t>(Device::FRAME_COUNT) * FontRenderer::MAX_VERTICES_PER_FRAME * sizeof(FontRenderer::TextVertex);

  const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_RESOURCE_DESC desc = BufferDesc(sizeBytes);
  winrt::check_hresult(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(m_vertexBuffer.put())));

  const D3D12_RANGE readNothing = {0, 0};
  winrt::check_hresult(m_vertexBuffer->Map(0, &readNothing, reinterpret_cast<void**>(&m_mappedVertices)));
}

void FontBackend::CreatePipeline(ID3D12Device* _device)
{
  D3D12_DESCRIPTOR_RANGE1 atlasRange = {};
  atlasRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  atlasRange.NumDescriptors = 1;
  atlasRange.BaseShaderRegister = 0;
  atlasRange.RegisterSpace = 0;
  // The atlas really is static: it is written once during Create and never again, so the driver
  // is welcome to assume it.
  atlasRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
  atlasRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  std::array<D3D12_ROOT_PARAMETER1, 2> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &atlasRange;

  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.RegisterSpace = 0;
  parameters[1].Constants.Num32BitValues = CONSTANT_COUNT;

  const D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc = {
    .Version = D3D_ROOT_SIGNATURE_VERSION_1_1,
    .Desc_1_1 = {.NumParameters = static_cast<UINT>(parameters.size()),
                 .pParameters = parameters.data(),
                 .NumStaticSamplers = 0,
                 .pStaticSamplers = nullptr,
                 .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT},
  };

  winrt::com_ptr<ID3DBlob> serialized;
  winrt::com_ptr<ID3DBlob> errors;
  const HRESULT serializeResult = D3D12SerializeVersionedRootSignature(&rootSignatureDesc, serialized.put(), errors.put());
  if (FAILED(serializeResult) && errors)
  {
    Fatal("Text root signature: {}", static_cast<const char*>(errors->GetBufferPointer()));
  }
  winrt::check_hresult(serializeResult);
  winrt::check_hresult(
    _device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  const std::array<D3D12_INPUT_ELEMENT_DESC, 3> inputLayout = {
    D3D12_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(FontRenderer::TextVertex, positionXPixels),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(FontRenderer::TextVertex, glyphXTexels),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(FontRenderer::TextVertex, color),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = DefaultGraphicsPipeline();
  pipelineDesc.pRootSignature = m_rootSignature.get();
  pipelineDesc.VS = {g_TextVS, sizeof(g_TextVS)};
  pipelineDesc.PS = {g_TextPS, sizeof(g_TextPS)};
  pipelineDesc.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
  // Text is the last thing drawn and sits on top of the scene, so it neither tests nor writes
  // depth. It writes straight into the back buffer, which is R8G8B8A8_UNORM (ADR-011).
  //
  // Blending is on, and it is NOT anti-aliasing: a glyph pixel is lit or discarded, never
  // partially covered. What it buys is the muted greys the interface is built from -- a caption
  // at 55% white over a rail is one draw rather than a colour precomputed against whatever
  // happens to be behind it (ADR-014).
  pipelineDesc.BlendState = InterfaceBlendState();
  pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  winrt::check_hresult(_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(m_pipeline.put())));
}

void FontBackend::Draw(ID3D12GraphicsCommandList* _commandList, std::uint32_t _frameIndex, FontRenderer& _recorder)
{
  // A backend that was never Created has no pipeline, no root signature and no buffer the GPU can
  // read. This is fatal rather than a silent return, because a caller that reached here is a caller
  // that believes it is drawing to a screen.
  ASSERT_TEXT(m_pipeline != nullptr, L"Drawing through a FontBackend that was never created.");

  const FontRenderer::Batch batch = _recorder.TakeUnflushed();
  if (batch.vertices.empty())
  {
    return;
  }

  ASSERT_TEXT(batch.firstVertex + batch.vertices.size() <= FontRenderer::MAX_VERTICES_PER_FRAME,
              L"A text batch runs past the end of its frame's slice.");

  // Into this frame's slice, at the offset the recorder says this batch occupies. Only the new
  // vertices are copied, and never over ones an earlier draw in this same command list has been
  // told to read.
  FontRenderer::TextVertex* slice = m_mappedVertices + static_cast<std::size_t>(_frameIndex) * FontRenderer::MAX_VERTICES_PER_FRAME;
  std::ranges::copy(batch.vertices, slice + batch.firstVertex);

  const std::uint64_t sliceOffsetBytes =
    static_cast<std::uint64_t>(_frameIndex) * FontRenderer::MAX_VERTICES_PER_FRAME * sizeof(FontRenderer::TextVertex);
  const auto pastThisBatch = static_cast<std::uint32_t>(batch.firstVertex + batch.vertices.size());

  D3D12_VERTEX_BUFFER_VIEW vertexView = {};
  vertexView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress() + sliceOffsetBytes;
  vertexView.SizeInBytes = pastThisBatch * sizeof(FontRenderer::TextVertex);
  vertexView.StrideInBytes = sizeof(FontRenderer::TextVertex);

  ID3D12DescriptorHeap* heaps[] = {m_shaderVisibleHeap->Handle()};
  _commandList->SetDescriptorHeaps(1, heaps);
  _commandList->SetGraphicsRootSignature(m_rootSignature.get());
  _commandList->SetPipelineState(m_pipeline.get());
  _commandList->SetGraphicsRootDescriptorTable(0, m_shaderVisibleHeap->GpuHandle(m_atlasSlot));

  const std::array<float, CONSTANT_COUNT> canvasPixels = {static_cast<float>(SceneTarget::WIDTH_PIXELS),
                                                          static_cast<float>(SceneTarget::HEIGHT_PIXELS)};
  _commandList->SetGraphicsRoot32BitConstants(1, CONSTANT_COUNT, canvasPixels.data(), 0);

  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->IASetVertexBuffers(0, 1, &vertexView);
  _commandList->DrawInstanced(static_cast<std::uint32_t>(batch.vertices.size()), 1, batch.firstVertex, 0);
}

} // namespace Neuron
