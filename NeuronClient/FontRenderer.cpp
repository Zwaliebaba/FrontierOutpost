// FontRenderer.cpp -- Font.h's 768 bytes turned into an atlas, and strings turned into quads.

#include "pch.h"
#include "FontRenderer.h"

#include "D3D12Defaults.h"
#include "SceneTarget.h"

#include "CompiledShaders/TextVS.h"
#include "CompiledShaders/TextPS.h"

namespace Neuron
{

namespace
{

// One texel per glyph pixel, all 96 glyphs in a single row. A row is 768 texels, which is exactly
// three times D3D12's 256-byte row pitch alignment, so the upload needs no padding -- pleasant,
// but GetCopyableFootprints is still what decides the layout below rather than that arithmetic.
constexpr std::uint32_t ATLAS_WIDTH_TEXELS = FontRenderer::GLYPH_COUNT * FontRenderer::GLYPH_WIDTH_TEXELS;
constexpr std::uint32_t ATLAS_HEIGHT_TEXELS = FontRenderer::GLYPH_HEIGHT_TEXELS;
constexpr DXGI_FORMAT ATLAS_FORMAT = DXGI_FORMAT_R8_UINT;

// Two floats for the screen size in pixels.
constexpr std::uint32_t TEXT_CONSTANT_COUNT = 2;

} // namespace

void FontRenderer::Create(Device& _device, DescriptorHeap& _shaderVisibleHeap)
{
  m_shaderVisibleHeap = &_shaderVisibleHeap;

  CreateAtlas(_device, _shaderVisibleHeap);
  CreateVertexBuffer(_device.Handle());
  CreatePipeline(_device.Handle());
}

void FontRenderer::CreateAtlas(Device& _device, DescriptorHeap& _shaderVisibleHeap)
{
  ID3D12Device* device = _device.Handle();

  // Unpack Font.h: byte b of glyph g is row b, most significant bit leftmost. One texel per
  // pixel, 0 or 1; the pixel shader writes the string's color where the bit is 1 and discards the
  // pixel where it is 0.
  std::vector<std::uint8_t> texels(static_cast<std::size_t>(ATLAS_WIDTH_TEXELS) * ATLAS_HEIGHT_TEXELS, 0);
  for (std::uint32_t glyph = 0; glyph < GLYPH_COUNT; ++glyph)
  {
    const auto character = static_cast<char>(FIRST_CHARACTER + glyph);
    for (std::uint32_t row = 0; row < GLYPH_HEIGHT_TEXELS; ++row)
    {
      const std::uint8_t bits = GlyphRow(character, row);
      for (std::uint32_t column = 0; column < GLYPH_WIDTH_TEXELS; ++column)
      {
        const std::uint32_t shift = GLYPH_WIDTH_TEXELS - 1 - column;
        const std::size_t texel =
          static_cast<std::size_t>(row) * ATLAS_WIDTH_TEXELS + static_cast<std::size_t>(glyph) * GLYPH_WIDTH_TEXELS + column;
        texels[texel] = static_cast<std::uint8_t>((bits >> shift) & 1U);
      }
    }
  }

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

void FontRenderer::CreateVertexBuffer(ID3D12Device* _device)
{
  const std::uint64_t sizeBytes = static_cast<std::uint64_t>(Device::FRAME_COUNT) * MAX_VERTICES_PER_FRAME * sizeof(TextVertex);

  const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_RESOURCE_DESC desc = BufferDesc(sizeBytes);
  winrt::check_hresult(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(m_vertices.put())));

  const D3D12_RANGE readNothing = {0, 0};
  winrt::check_hresult(m_vertices->Map(0, &readNothing, reinterpret_cast<void**>(&m_mappedVertices)));
}

void FontRenderer::CreatePipeline(ID3D12Device* _device)
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
  parameters[1].Constants.Num32BitValues = TEXT_CONSTANT_COUNT;

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
    D3D12_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(TextVertex, positionXPixels),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(TextVertex, glyphXTexels),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(TextVertex, color),
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

std::vector<std::string> FontRenderer::Wrap(std::string_view _text, std::size_t _maxCharacters)
{
  std::vector<std::string> lines;
  if (_maxCharacters == 0)
  {
    return lines;
  }

  std::string current;
  std::size_t start = 0;
  while (start <= _text.size())
  {
    const std::size_t space = _text.find(' ', start);
    const std::size_t end = (space == std::string_view::npos) ? _text.size() : space;
    std::string_view word = _text.substr(start, end - start);

    // A word longer than the line is hard-broken rather than allowed to overflow. Nothing in the
    // reference copy is, but a system name from a server is not something this screen gets to
    // assume anything about.
    while (word.size() > _maxCharacters)
    {
      if (!current.empty())
      {
        lines.push_back(current);
        current.clear();
      }
      lines.emplace_back(word.substr(0, _maxCharacters));
      word = word.substr(_maxCharacters);
    }

    const std::size_t needed = current.empty() ? word.size() : current.size() + 1 + word.size();
    if (needed > _maxCharacters && !current.empty())
    {
      lines.push_back(current);
      current.assign(word);
    }
    else
    {
      if (!current.empty())
      {
        current.push_back(' ');
      }
      current.append(word);
    }

    if (space == std::string_view::npos)
    {
      break;
    }
    start = space + 1;
  }

  if (!current.empty())
  {
    lines.push_back(current);
  }
  return lines;
}

void FontRenderer::BeginFrame(std::uint32_t _frameIndex) noexcept
{
  m_frameIndex = _frameIndex;
  m_usedThisFrame = 0;
  m_flushedThisFrame = 0;
  ClearClipRect();
}

void FontRenderer::SetClipRect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels) noexcept
{
  m_clipLeftPixels = _xPixels;
  m_clipTopPixels = _yPixels;
  m_clipRightPixels = _xPixels + _widthPixels;
  m_clipBottomPixels = _yPixels + _heightPixels;
  m_clipping = true;
}

void FontRenderer::ClearClipRect() noexcept
{
  m_clipping = false;
}

void FontRenderer::DrawText(std::int32_t _xPixels, std::int32_t _yPixels, std::string_view _text, const Color& _color, std::uint32_t _scale)
{
  ASSERT_TEXT(_scale > 0, L"A glyph scale of zero would draw nothing and is a caller mistake, not a way to hide text.");

  TextVertex* slice = m_mappedVertices + static_cast<std::size_t>(m_frameIndex) * MAX_VERTICES_PER_FRAME;

  const std::uint32_t advance = AdvancePixels(_scale);
  const std::uint32_t lineHeight = GlyphHeightPixels(_scale);

  for (std::size_t character = 0; character < _text.size(); ++character)
  {
    ASSERT_TEXT(m_usedThisFrame + VERTICES_PER_GLYPH <= MAX_VERTICES_PER_FRAME,
                L"More text in one frame than FontRenderer::MAX_CHARACTERS_PER_FRAME allows.");

    const std::uint32_t glyph = GlyphIndex(_text[character]);

    // The quad spans _scale screen pixels per glyph texel. The atlas coordinates below still span
    // exactly eight texels, so the interpolator hands the pixel shader a fractional texel and its
    // truncation is what turns one texel into a _scale-square block of pixels -- the same integer
    // divide the resolve pass did for the whole screen before ADR-011 removed it.
    const auto left = static_cast<float>(_xPixels + static_cast<std::int32_t>(character * advance));
    const auto top = static_cast<float>(_yPixels);
    const float right = left + static_cast<float>(advance);
    const float bottom = top + static_cast<float>(lineHeight);

    const auto atlasLeft = static_cast<float>(glyph * GLYPH_WIDTH_TEXELS);
    const float atlasRight = atlasLeft + static_cast<float>(GLYPH_WIDTH_TEXELS);
    constexpr float ATLAS_TOP = 0.0F;
    constexpr float ATLAS_BOTTOM = static_cast<float>(GLYPH_HEIGHT_TEXELS);

    if (m_clipping && (left < m_clipLeftPixels || right > m_clipRightPixels || top < m_clipTopPixels || bottom > m_clipBottomPixels))
    {
      // Outside, or straddling the edge. The cursor still advances, so the rest of the string
      // stays where it would have been -- a clipped label loses letters, it does not shuffle up.
      continue;
    }

    const std::uint32_t color = Pack(_color);
    TextVertex* quad = slice + m_usedThisFrame;
    quad[0] = {left, top, atlasLeft, ATLAS_TOP, color};
    quad[1] = {right, top, atlasRight, ATLAS_TOP, color};
    quad[2] = {left, bottom, atlasLeft, ATLAS_BOTTOM, color};
    quad[3] = {right, top, atlasRight, ATLAS_TOP, color};
    quad[4] = {right, bottom, atlasRight, ATLAS_BOTTOM, color};
    quad[5] = {left, bottom, atlasLeft, ATLAS_BOTTOM, color};

    m_usedThisFrame += VERTICES_PER_GLYPH;
  }
}

void FontRenderer::Flush(ID3D12GraphicsCommandList* _commandList)
{
  // Only what has been recorded since the last flush, so that a second layer's glyphs can sit over
  // a second layer's shapes rather than over the whole frame. See the header.
  if (m_usedThisFrame == m_flushedThisFrame)
  {
    return;
  }

  const std::uint64_t sliceOffsetBytes = static_cast<std::uint64_t>(m_frameIndex) * MAX_VERTICES_PER_FRAME * sizeof(TextVertex);

  D3D12_VERTEX_BUFFER_VIEW vertexView = {};
  vertexView.BufferLocation = m_vertices->GetGPUVirtualAddress() + sliceOffsetBytes;
  vertexView.SizeInBytes = m_usedThisFrame * sizeof(TextVertex);
  vertexView.StrideInBytes = sizeof(TextVertex);

  ID3D12DescriptorHeap* heaps[] = {m_shaderVisibleHeap->Handle()};
  _commandList->SetDescriptorHeaps(1, heaps);
  _commandList->SetGraphicsRootSignature(m_rootSignature.get());
  _commandList->SetPipelineState(m_pipeline.get());
  _commandList->SetGraphicsRootDescriptorTable(0, m_shaderVisibleHeap->GpuHandle(m_atlasSlot));

  const std::array<float, TEXT_CONSTANT_COUNT> screenPixels = {static_cast<float>(SceneTarget::WIDTH_PIXELS),
                                                               static_cast<float>(SceneTarget::HEIGHT_PIXELS)};
  _commandList->SetGraphicsRoot32BitConstants(1, TEXT_CONSTANT_COUNT, screenPixels.data(), 0);

  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->IASetVertexBuffers(0, 1, &vertexView);
  _commandList->DrawInstanced(m_usedThisFrame - m_flushedThisFrame, 1, m_flushedThisFrame, 0);
  m_flushedThisFrame = m_usedThisFrame;
}

} // namespace Neuron
