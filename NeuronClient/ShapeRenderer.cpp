// ShapeRenderer.cpp -- rectangles, lines and ellipses, tessellated on the CPU into one triangle
// list. See ShapeRenderer.h for why there is no signed-distance shader here.

#include "pch.h"
#include "ShapeRenderer.h"

#include "D3D12Defaults.h"
#include "SceneTarget.h"

#include "CompiledShaders/ShapeVS.h"
#include "CompiledShaders/ShapePS.h"

namespace Neuron
{

namespace
{

constexpr float TWO_PI = 6.28318530717958647692F;

} // namespace

void ShapeRenderer::Create(ID3D12Device* _device)
{
  CreateVertexBuffer(_device);
  CreatePipeline(_device);
}

void ShapeRenderer::CreateHeadless()
{
  m_headlessVertices.assign(static_cast<std::size_t>(Device::FRAME_COUNT) * MAX_VERTICES_PER_FRAME, ShapeVertex{});
  m_mappedVertices = m_headlessVertices.data();
  m_headless = true;
}

void ShapeRenderer::CreateVertexBuffer(ID3D12Device* _device)
{
  const std::uint64_t sizeBytes = static_cast<std::uint64_t>(Device::FRAME_COUNT) * MAX_VERTICES_PER_FRAME * sizeof(ShapeVertex);

  const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_RESOURCE_DESC desc = BufferDesc(sizeBytes);
  winrt::check_hresult(_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr, IID_PPV_ARGS(m_vertices.put())));

  const D3D12_RANGE readNothing = {0, 0};
  winrt::check_hresult(m_vertices->Map(0, &readNothing, reinterpret_cast<void**>(&m_mappedVertices)));
}

void ShapeRenderer::CreatePipeline(ID3D12Device* _device)
{
  // One root parameter and no descriptor table: this pass reads no texture at all, which is what
  // separates it from the text pass it otherwise resembles.
  std::array<D3D12_ROOT_PARAMETER1, 1> parameters = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[0].Constants.ShaderRegister = 0;
  parameters[0].Constants.RegisterSpace = 0;
  parameters[0].Constants.Num32BitValues = CONSTANT_COUNT;

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
    Fatal("Shape root signature: {}", static_cast<const char*>(errors->GetBufferPointer()));
  }
  winrt::check_hresult(serializeResult);
  winrt::check_hresult(
    _device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(m_rootSignature.put())));

  const std::array<D3D12_INPUT_ELEMENT_DESC, 2> inputLayout = {
    D3D12_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ShapeVertex, positionXPixels),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    D3D12_INPUT_ELEMENT_DESC{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(ShapeVertex, color),
                             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc = DefaultGraphicsPipeline();
  pipelineDesc.pRootSignature = m_rootSignature.get();
  pipelineDesc.VS = {g_ShapeVS, sizeof(g_ShapeVS)};
  pipelineDesc.PS = {g_ShapePS, sizeof(g_ShapePS)};
  pipelineDesc.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
  // This is interface, not scene: it is drawn in the order the caller asked for and neither tests
  // nor writes depth. Painter's order is the whole of its occlusion model, and blending is what
  // makes a 4%-white card fill mean what the design tokens say it means (ADR-014).
  pipelineDesc.BlendState = InterfaceBlendState();
  pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  winrt::check_hresult(_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(m_pipeline.put())));
}

void ShapeRenderer::BeginFrame(std::uint32_t _frameIndex) noexcept
{
  m_frameIndex = _frameIndex;
  m_usedThisFrame = 0;
  m_flushedThisFrame = 0;
}

void ShapeRenderer::AppendShadedTriangle(float _axPixels, float _ayPixels, std::uint32_t _aColor, float _bxPixels, float _byPixels,
                                         std::uint32_t _bColor, float _cxPixels, float _cyPixels, std::uint32_t _cColor)
{
  ASSERT_TEXT(m_usedThisFrame + 3 <= MAX_VERTICES_PER_FRAME,
              L"More interface geometry in one frame than ShapeRenderer::MAX_VERTICES_PER_FRAME allows.");

  ShapeVertex* slice = m_mappedVertices + static_cast<std::size_t>(m_frameIndex) * MAX_VERTICES_PER_FRAME;
  ShapeVertex* triangle = slice + m_usedThisFrame;
  triangle[0] = {_axPixels, _ayPixels, _aColor};
  triangle[1] = {_bxPixels, _byPixels, _bColor};
  triangle[2] = {_cxPixels, _cyPixels, _cColor};
  m_usedThisFrame += 3;
}

void ShapeRenderer::AppendTriangle(float _axPixels, float _ayPixels, float _bxPixels, float _byPixels, float _cxPixels, float _cyPixels,
                                   std::uint32_t _packedColor)
{
  AppendShadedTriangle(_axPixels, _ayPixels, _packedColor, _bxPixels, _byPixels, _packedColor, _cxPixels, _cyPixels, _packedColor);
}

void ShapeRenderer::AppendQuad(float _axPixels, float _ayPixels, float _bxPixels, float _byPixels, float _cxPixels, float _cyPixels,
                               float _dxPixels, float _dyPixels, std::uint32_t _packedColor)
{
  AppendTriangle(_axPixels, _ayPixels, _bxPixels, _byPixels, _cxPixels, _cyPixels, _packedColor);
  AppendTriangle(_axPixels, _ayPixels, _cxPixels, _cyPixels, _dxPixels, _dyPixels, _packedColor);
}

void ShapeRenderer::FillRect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, const Color& _color)
{
  if (_widthPixels <= 0.0F || _heightPixels <= 0.0F)
  {
    return;
  }

  const float right = _xPixels + _widthPixels;
  const float bottom = _yPixels + _heightPixels;
  AppendQuad(_xPixels, _yPixels, right, _yPixels, right, bottom, _xPixels, bottom, Pack(_color));
}

void ShapeRenderer::StrokeRect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, const Color& _color,
                               float _thicknessPixels)
{
  if (_widthPixels <= 0.0F || _heightPixels <= 0.0F)
  {
    return;
  }

  const float t = std::min(_thicknessPixels, std::min(_widthPixels, _heightPixels) * 0.5F);
  FillRect(_xPixels, _yPixels, _widthPixels, t, _color);
  FillRect(_xPixels, _yPixels + _heightPixels - t, _widthPixels, t, _color);
  // The left and right edges stop short of the top and bottom ones rather than overlapping them.
  // With no blending an overlap is invisible, but it doubles the triangles at every corner.
  FillRect(_xPixels, _yPixels + t, t, _heightPixels - 2.0F * t, _color);
  FillRect(_xPixels + _widthPixels - t, _yPixels + t, t, _heightPixels - 2.0F * t, _color);
}

void ShapeRenderer::Line(float _x0Pixels, float _y0Pixels, float _x1Pixels, float _y1Pixels, const Color& _color, float _thicknessPixels)
{
  const float deltaX = _x1Pixels - _x0Pixels;
  const float deltaY = _y1Pixels - _y0Pixels;
  const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
  if (length <= 0.0F || _thicknessPixels <= 0.0F)
  {
    return;
  }

  // The normal, scaled to half the thickness: the quad is the segment swept sideways by it.
  const float halfX = (-deltaY / length) * _thicknessPixels * 0.5F;
  const float halfY = (deltaX / length) * _thicknessPixels * 0.5F;

  AppendQuad(_x0Pixels + halfX, _y0Pixels + halfY, _x1Pixels + halfX, _y1Pixels + halfY, _x1Pixels - halfX, _y1Pixels - halfY,
             _x0Pixels - halfX, _y0Pixels - halfY, Pack(_color));
}

void ShapeRenderer::DashedLine(float _x0Pixels, float _y0Pixels, float _x1Pixels, float _y1Pixels, const Color& _color,
                               float _thicknessPixels, float _dashPixels, float _gapPixels, float _offsetPixels)
{
  const float deltaX = _x1Pixels - _x0Pixels;
  const float deltaY = _y1Pixels - _y0Pixels;
  const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
  const float period = _dashPixels + _gapPixels;
  if (length <= 0.0F || period <= 0.0F)
  {
    return;
  }

  const float stepX = deltaX / length;
  const float stepY = deltaY / length;

  // The offset is taken modulo the period, so an animation driven by a clock that has been running
  // for an hour is the same arithmetic as one that started a moment ago. Reduced first and made
  // positive, because `fmod` keeps the sign of its left operand and a negative phase would start
  // the walk past the first endpoint.
  float phase = std::fmod(_offsetPixels, period);
  if (phase < 0.0F)
  {
    phase += period;
  }

  // Counted by dash INDEX rather than by accumulating a float, so the last dash of a long line
  // lands where arithmetic says it should rather than where the accumulated error left it. The
  // walk starts one whole period behind the first endpoint: the dash that the phase has pushed
  // only partly onto the line is the one that makes the pattern appear to enter it.
  const auto dashes = static_cast<std::uint32_t>(std::ceil((length + period) / period));
  for (std::uint32_t dash = 0; dash < dashes; ++dash)
  {
    const float travelled = static_cast<float>(dash) * period - period + phase;
    const float dashStart = std::max(travelled, 0.0F);
    const float dashEnd = std::min(travelled + _dashPixels, length);
    if (dashEnd <= dashStart)
    {
      continue;
    }
    Line(_x0Pixels + stepX * dashStart, _y0Pixels + stepY * dashStart, _x0Pixels + stepX * dashEnd, _y0Pixels + stepY * dashEnd, _color,
         _thicknessPixels);
  }
}

void ShapeRenderer::FillEllipse(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels, const Color& _color)
{
  if (_radiusXPixels <= 0.0F || _radiusYPixels <= 0.0F)
  {
    return;
  }

  const std::uint32_t segments = SegmentsForRadius(std::max(_radiusXPixels, _radiusYPixels));
  const std::uint32_t packed = Pack(_color);

  // A fan from the center. Every triangle is degenerate-free because the radii are positive and
  // the step is a whole fraction of a turn.
  float previousX = _centerXPixels + _radiusXPixels;
  float previousY = _centerYPixels;
  for (std::uint32_t segment = 1; segment <= segments; ++segment)
  {
    const float angle = TWO_PI * static_cast<float>(segment) / static_cast<float>(segments);
    const float x = _centerXPixels + _radiusXPixels * std::cos(angle);
    const float y = _centerYPixels + _radiusYPixels * std::sin(angle);
    AppendTriangle(_centerXPixels, _centerYPixels, previousX, previousY, x, y, packed);
    previousX = x;
    previousY = y;
  }
}

void ShapeRenderer::StrokeEllipse(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels,
                                  const Color& _color, float _thicknessPixels)
{
  if (_radiusXPixels <= 0.0F || _radiusYPixels <= 0.0F)
  {
    return;
  }

  const std::uint32_t segments = SegmentsForRadius(std::max(_radiusXPixels, _radiusYPixels));
  float previousX = _centerXPixels + _radiusXPixels;
  float previousY = _centerYPixels;
  for (std::uint32_t segment = 1; segment <= segments; ++segment)
  {
    const float angle = TWO_PI * static_cast<float>(segment) / static_cast<float>(segments);
    const float x = _centerXPixels + _radiusXPixels * std::cos(angle);
    const float y = _centerYPixels + _radiusYPixels * std::sin(angle);
    Line(previousX, previousY, x, y, _color, _thicknessPixels);
    previousX = x;
    previousY = y;
  }
}

void ShapeRenderer::DashedEllipse(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels,
                                  const Color& _color, float _thicknessPixels, float _dashPixels, float _gapPixels)
{
  if (_radiusXPixels <= 0.0F || _radiusYPixels <= 0.0F)
  {
    return;
  }

  // The dash pattern is walked in CHORD length around the outline. An ellipse's arc length has no
  // closed form, but the chord of one segment is within a fraction of a pixel of its arc at the
  // segment counts SegmentsForRadius returns -- and a dashed outline is a texture, not a
  // measurement, so paying for a numerical arc length here would buy nothing anybody can see.
  const std::uint32_t segments = SegmentsForRadius(std::max(_radiusXPixels, _radiusYPixels));
  const float period = _dashPixels + _gapPixels;
  if (period <= 0.0F)
  {
    return;
  }

  float previousX = _centerXPixels + _radiusXPixels;
  float previousY = _centerYPixels;
  float travelled = 0.0F;

  for (std::uint32_t segment = 1; segment <= segments; ++segment)
  {
    const float angle = TWO_PI * static_cast<float>(segment) / static_cast<float>(segments);
    const float x = _centerXPixels + _radiusXPixels * std::cos(angle);
    const float y = _centerYPixels + _radiusYPixels * std::sin(angle);

    // A segment is drawn when its MIDPOINT falls in the "on" part of the pattern. Subdividing the
    // segment at the dash boundary would be more exact and would also make the dashes uneven,
    // because the segments are not equal length on an ellipse.
    const float chordX = x - previousX;
    const float chordY = y - previousY;
    const float chord = std::sqrt(chordX * chordX + chordY * chordY);
    if (std::fmod(travelled + chord * 0.5F, period) < _dashPixels)
    {
      Line(previousX, previousY, x, y, _color, _thicknessPixels);
    }

    travelled += chord;
    previousX = x;
    previousY = y;
  }
}

void ShapeRenderer::FillTriangle(float _axPixels, float _ayPixels, float _bxPixels, float _byPixels, float _cxPixels, float _cyPixels,
                                 const Color& _color)
{
  AppendTriangle(_axPixels, _ayPixels, _bxPixels, _byPixels, _cxPixels, _cyPixels, Pack(_color));
}

void ShapeRenderer::FillVerticalGradient(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, const Color& _top,
                                         const Color& _middle, float _middleFraction, const Color& _bottom)
{
  if (_widthPixels <= 0.0F || _heightPixels <= 0.0F)
  {
    return;
  }

  const float right = _xPixels + _widthPixels;
  const float split = _yPixels + _heightPixels * std::clamp(_middleFraction, 0.0F, 1.0F);
  const float bottom = _yPixels + _heightPixels;

  const std::uint32_t top = Pack(_top);
  const std::uint32_t middle = Pack(_middle);
  const std::uint32_t low = Pack(_bottom);

  // Two bands, each two triangles. The interpolator does the rest.
  //
  // **It interpolates in full precision and then quantizes to eight bits, which is where banding
  // comes from rather than where it is avoided.** Measured down the map's own gradient, the steps
  // are one level at a time -- red every 40 pixels, blue every 15 -- which is as smooth as R8G8B8A8
  // can be and is still a step somebody with a good display may see on an area this large. Removing
  // it needs a dither in the pixel shader, which nobody has asked for.
  AppendShadedTriangle(_xPixels, _yPixels, top, right, _yPixels, top, right, split, middle);
  AppendShadedTriangle(_xPixels, _yPixels, top, right, split, middle, _xPixels, split, middle);
  AppendShadedTriangle(_xPixels, split, middle, right, split, middle, right, bottom, low);
  AppendShadedTriangle(_xPixels, split, middle, right, bottom, low, _xPixels, bottom, low);
}

void ShapeRenderer::FillRadialGradient(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels,
                                       const Color& _center, const Color& _rim)
{
  if (_radiusXPixels <= 0.0F || _radiusYPixels <= 0.0F)
  {
    return;
  }

  const std::uint32_t segments = SegmentsForRadius(std::max(_radiusXPixels, _radiusYPixels));
  const std::uint32_t center = Pack(_center);
  const std::uint32_t rim = Pack(_rim);

  float previousX = _centerXPixels + _radiusXPixels;
  float previousY = _centerYPixels;
  for (std::uint32_t segment = 1; segment <= segments; ++segment)
  {
    const float angle = TWO_PI * static_cast<float>(segment) / static_cast<float>(segments);
    const float x = _centerXPixels + _radiusXPixels * std::cos(angle);
    const float y = _centerYPixels + _radiusYPixels * std::sin(angle);
    AppendShadedTriangle(_centerXPixels, _centerYPixels, center, previousX, previousY, rim, x, y, rim);
    previousX = x;
    previousY = y;
  }
}

void ShapeRenderer::Flush(ID3D12GraphicsCommandList* _commandList)
{
  // A headless renderer has no pipeline, no root signature and no buffer the GPU can read. This
  // is fatal rather than a silent return, because a caller that reached here is a caller that
  // believes it is drawing to a screen.
  ASSERT_TEXT(!m_headless, L"Flushing a headless renderer. CreateHeadless is for layout, not for drawing.");

  // Only what has been recorded since the last flush. See the header: this is what lets a caller
  // put the interface over the world instead of having every glyph land on top of everything.
  if (m_usedThisFrame == m_flushedThisFrame)
  {
    return;
  }

  const std::uint64_t sliceOffsetBytes = static_cast<std::uint64_t>(m_frameIndex) * MAX_VERTICES_PER_FRAME * sizeof(ShapeVertex);

  D3D12_VERTEX_BUFFER_VIEW vertexView = {};
  vertexView.BufferLocation = m_vertices->GetGPUVirtualAddress() + sliceOffsetBytes;
  vertexView.SizeInBytes = m_usedThisFrame * sizeof(ShapeVertex);
  vertexView.StrideInBytes = sizeof(ShapeVertex);

  _commandList->SetGraphicsRootSignature(m_rootSignature.get());
  _commandList->SetPipelineState(m_pipeline.get());

  const std::array<float, CONSTANT_COUNT> screenPixels = {static_cast<float>(SceneTarget::WIDTH_PIXELS),
                                                          static_cast<float>(SceneTarget::HEIGHT_PIXELS)};
  _commandList->SetGraphicsRoot32BitConstants(0, CONSTANT_COUNT, screenPixels.data(), 0);

  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->IASetVertexBuffers(0, 1, &vertexView);
  _commandList->DrawInstanced(m_usedThisFrame - m_flushedThisFrame, 1, m_flushedThisFrame, 0);
  m_flushedThisFrame = m_usedThisFrame;
}

} // namespace Neuron
