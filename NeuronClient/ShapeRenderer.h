#pragma once

#include "Color.h"
#include "Device.h"

namespace Neuron
{

/// Filled and stroked 2D primitives in screen pixels, for the parts of the interface that are not
/// text.
///
/// Everything this draws is triangles with a color per vertex, and every shape is tessellated on
/// the CPU: a rectangle is two triangles, a line is a quad, an ellipse is a fan. That is the whole
/// design. A shader that evaluated a signed-distance field would give smooth edges, which is
/// exactly what this interface must not have -- the screen is a pixel grid and a half-covered
/// pixel is a color nobody chose (ADR-011, ADR-014).
///
/// Coordinates are screen pixels with the origin at the top-left, matching FontRenderer, so a
/// caption at (16, 16) and a rule under it at (16, 28) are written in the same numbers.
///
/// AXIS-ALIGNED EDGES LAND ON THE GRID AND DIAGONALS DO NOT. A rectangle at whole-pixel
/// coordinates covers whole pixels exactly. A diagonal line is a rotated quad and the rasterizer
/// resolves it by pixel centers, so it comes out as a hard staircase. Both are what this interface
/// wants; neither is anti-aliased, because nothing in this renderer is.
class ShapeRenderer
{
public:
  /// One frame's worth of geometry. The map is the heavy consumer -- the ground grid alone is 26
  /// lines, and every node is a shadow ellipse, a stem and a disc -- so this is sized for the
  /// screen rather than for a widget. Overrunning it is a broken invariant rather than a case to
  /// grow into (Debug.h).
  static constexpr std::uint32_t MAX_VERTICES_PER_FRAME = 32768;

  /// How many segments an ellipse is drawn with, by radius. A node disc is 5 px and a sealed
  /// region 63; one constant would either facet the big one or waste triangles on the small one.
  [[nodiscard]] static constexpr std::uint32_t SegmentsForRadius(float _radiusPixels) noexcept
  {
    const auto wanted = static_cast<std::uint32_t>(_radiusPixels * 2.0F);
    return std::clamp(wanted, MIN_ELLIPSE_SEGMENTS, MAX_ELLIPSE_SEGMENTS);
  }

  void Create(ID3D12Device* _device);

  /// Creates the renderer with NO DEVICE BEHIND IT: appended geometry lands in ordinary memory
  /// and `Flush` is refused.
  ///
  /// **This is the seam that makes a screen's LAYOUT testable.** Every page in this game builds its
  /// hit list while it draws -- `AddHit` sits beside the `FillRect` that put the button there, which
  /// is what stops the two drifting apart -- so a test that wants to press a button has to be able
  /// to run the draw. It could not: an append writes through a pointer into an upload heap, and
  /// without a device that pointer is null. Whoever wanted to test a tap had the choice of standing
  /// up D3D12 in a test DLL that CI runs on a machine with no GPU, or writing the layout out a
  /// second time in the test and asserting against a copy of the thing under test.
  ///
  /// A headless renderer records the same geometry into a vector instead. Nothing about the append
  /// path changes -- it is the same code writing to a different address -- so what a test drives is
  /// what ships.
  void CreateHeadless();

  /// Resets this frame's slice. Every frame writes its own, so the CPU never overwrites vertices
  /// the GPU is still reading -- the same arrangement FontRenderer uses.
  void BeginFrame(std::uint32_t _frameIndex) noexcept;

  void FillRect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, const Color& _color);

  /// A rectangle drawn as four filled edges, inset so the border lies INSIDE the given bounds.
  /// That is the CSS box the reference was designed in, and it is what keeps a 1px card border
  /// from bleeding into the pixel its neighbour owns.
  void StrokeRect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, const Color& _color,
                  float _thicknessPixels = 1.0F);

  void Line(float _x0Pixels, float _y0Pixels, float _x1Pixels, float _y1Pixels, const Color& _color, float _thicknessPixels = 1.0F);

  /// _dashPixels on, _gapPixels off, starting with a dash at the first endpoint. The pattern is
  /// walked in pixels rather than in a fraction of the length, so two dashed lanes of different
  /// lengths read as the same material.
  ///
  /// `_offsetPixels` slides the pattern along the line, toward the second endpoint. It is what
  /// makes a dashed line ROLL: pass a distance that grows with time and the dashes travel while
  /// the line itself stays put. The line is unchanged in every other way, and an offset of a whole
  /// period draws exactly what an offset of zero draws, so the animation never accumulates.
  void DashedLine(float _x0Pixels, float _y0Pixels, float _x1Pixels, float _y1Pixels, const Color& _color, float _thicknessPixels,
                  float _dashPixels, float _gapPixels, float _offsetPixels = 0.0F);

  void FillEllipse(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels, const Color& _color);

  void StrokeEllipse(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels, const Color& _color,
                     float _thicknessPixels = 1.0F);

  void DashedEllipse(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels, const Color& _color,
                     float _thicknessPixels, float _dashPixels, float _gapPixels);

  void FillTriangle(float _axPixels, float _ayPixels, float _bxPixels, float _byPixels, float _cxPixels, float _cyPixels,
                    const Color& _color);

  /// A rectangle whose colour runs top to bottom through three stops. Three rather than two
  /// because that is what the map pane is: near-black at the top, a lift at 45%, back down at the
  /// bottom (Design/Screens/README.md "Design tokens").
  ///
  /// This and FillRadialGradient are the ONLY two things on the screen whose colour varies across
  /// a shape. Everything else is flat, and the interpolator is a no-op for it.
  void FillVerticalGradient(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, const Color& _top,
                            const Color& _middle, float _middleFraction, const Color& _bottom);

  /// A fan from a centre colour out to a rim colour. The map's horizon glow: the rim is the same
  /// colour at zero alpha, so it fades into whatever is behind it rather than into black.
  void FillRadialGradient(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels, const Color& _center,
                          const Color& _rim);

  /// Issues everything appended since BeginFrame as a single draw call.
  /// Draws everything recorded SINCE THE LAST FLUSH, and remembers where it stopped.
  ///
  /// **Called more than once a frame, it is what puts one layer over another.** The interface is
  /// two renderers (ADR-014), and each is one batch: every shape, then every glyph. Flushed once at
  /// the end of a frame that meant all text landed on top of all shapes whatever order they were
  /// recorded in -- so a panel drawn over the map covered the map's dots and lanes and left its
  /// LABELS floating on top of the panel, which is what a modal is not allowed to do.
  ///
  /// Draining rather than redrawing is the whole of the fix: the caller flushes both renderers
  /// between the world and the interface, and each flush draws only what is new.
  void Flush(ID3D12GraphicsCommandList* _commandList);

private:
  /// Position in screen pixels and a packed color. R8: a vertex is a public aggregate handed to
  /// the GPU, so plain fields -- but it is private to this class because nothing outside builds
  /// one.
  struct ShapeVertex
  {
    float positionXPixels;
    float positionYPixels;
    std::uint32_t color;
  };

  static constexpr std::uint32_t MIN_ELLIPSE_SEGMENTS = 12;
  static constexpr std::uint32_t MAX_ELLIPSE_SEGMENTS = 64;

  /// Two floats for the screen size in pixels, the same constant the text pass takes.
  static constexpr std::uint32_t CONSTANT_COUNT = 2;

  void CreateVertexBuffer(ID3D12Device* _device);
  void CreatePipeline(ID3D12Device* _device);

  /// The one place vertices are appended. Every primitive above reduces to some number of calls
  /// to this, which is what makes the overrun check a single assertion rather than one per shape.
  void AppendTriangle(float _axPixels, float _ayPixels, float _bxPixels, float _byPixels, float _cxPixels, float _cyPixels,
                      std::uint32_t _packedColor);

  /// The same, with a colour per corner. Only the two gradients use it.
  void AppendShadedTriangle(float _axPixels, float _ayPixels, std::uint32_t _aColor, float _bxPixels, float _byPixels,
                            std::uint32_t _bColor, float _cxPixels, float _cyPixels, std::uint32_t _cColor);

  /// A quad as two triangles, corners in order.
  void AppendQuad(float _axPixels, float _ayPixels, float _bxPixels, float _byPixels, float _cxPixels, float _cyPixels, float _dxPixels,
                  float _dyPixels, std::uint32_t _packedColor);

  winrt::com_ptr<ID3D12Resource> m_vertices;
  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;

  /// The whole vertex buffer, mapped for the life of the renderer (FontRenderer.h says why).
  /// Where a headless renderer's geometry goes. Empty in the shipped path, where the vertices
  /// live in an upload heap the GPU reads directly.
  std::vector<ShapeVertex> m_headlessVertices;
  bool m_headless = false;

  ShapeVertex* m_mappedVertices = nullptr;
  std::uint32_t m_frameIndex = 0;
  std::uint32_t m_usedThisFrame = 0;
  /// How much of `m_usedThisFrame` has already been drawn this frame. See `Flush`.
  std::uint32_t m_flushedThisFrame = 0;
};

} // namespace Neuron
