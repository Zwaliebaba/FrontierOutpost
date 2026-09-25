// NeuronClient/DrawContext.h
#pragma once

#include "Program.h"
#include "Texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Neuron
{

class Buffer;
struct GraphicsCore;

/// How a draw's colour meets what its target holds: liblt's modes, one state for all targets
/// (plan §5.5).
enum class BlendMode : std::uint8_t
{
  Opaque,  // replaces it
  Alpha,   // colour weighed by source alpha, (SRC_ALPHA, INV_SRC_ALPHA); alpha summed, (ONE, ONE)
  Additive // both summed, (ONE, ONE)
};

/// The faces a draw skips, named as liblt's GL build names them: its front faces wind
/// counter-clockwise in GL's clip space, as OpenGL's do by default.
enum class CullMode : std::uint8_t
{
  None,
  Back,
  Front
};

/// A draw's fixed-function state: liblt's pushable state, with GL's meaning.
struct DrawState
{
  BlendMode blend;
  CullMode cull;
  bool depthTest;  // GL's GL_LESS, against the depth target; none bound passes, as in GL
  bool depthWrite; // only where depthTest is on, as in GL
  bool wireframe;
};

/// A vertex attribute's format: liblt's attributes are one to four floats.
enum class VertexFormat : std::uint8_t
{
  Float1,
  Float2,
  Float3,
  Float4
};

/// One attribute of a vertex, which the vertex shader's input of the same semantic reads.
struct VertexAttribute
{
  std::string_view semantic; // POSITION, TEXCOORD and so on
  std::uint32_t index;       // TEXCOORD3's 3
  VertexFormat format;
  std::uint32_t offsetBytes; // from the start of the vertex
};

/// How the vertices of a draw are laid out. Attributes the vertex shader does not read are left
/// alone.
struct VertexLayout
{
  std::span<const VertexAttribute> attributes;
  std::uint32_t strideBytes;
};

enum class IndexFormat : std::uint8_t
{
  UInt16,
  UInt32
};

/// Where a draw's colour goes: mip `mip` of a texture, and the face of a cube or the slice of a 3D
/// texture.
struct ColorTarget
{
  Texture* texture;
  std::uint32_t mip;
  std::uint32_t layer;
};

/// Records a device's work for the GPU, as a Direct3D 11 immediate context does
/// (Design/ADR/ADR-007). It knows the state each texture and buffer was left in and inserts the
/// barriers, and it takes what it uploads from a ring of pages that are reused once the GPU has
/// finished with them. What it records goes to the GPU at GraphicsDevice::EndFrame, or sooner at
/// Flush or a readback. A device has one, GraphicsDevice::Context, and it is not thread-safe.
///
/// Draws follow OpenGL's conventions, as liblt's shaders do once they end in the clip-space macro
/// (plan §5.5): images keep row 0 at the bottom, viewports and scissors take liblt's numbers, and
/// front faces wind counter-clockwise. The targets, viewport, scissor, state and program stay set
/// from one draw to the next, as GL's did.
class DrawContext
{
public:
  static constexpr std::uint32_t MAX_COLOR_TARGETS = 8;

  ~DrawContext();
  DrawContext(const DrawContext&) = delete;
  DrawContext& operator=(const DrawContext&) = delete;
  DrawContext(DrawContext&&) = delete;
  DrawContext& operator=(DrawContext&&) = delete;

  /// Replaces mip _mip of face _face, which is 0 unless the texture is a cube, with _texels: the
  /// level's texels, tightly packed, row 0 first, and for a 3D texture slice 0 first. A depth
  /// texture is not filled this way. A texture, level or size that does not fit is reported, and
  /// nothing is recorded.
  void UpdateTexture(Texture& _texture, std::uint32_t _mip, std::uint32_t _face, std::span<const std::byte> _texels);

  /// Reads mip _mip of face _face back, packed as UpdateTexture takes it. Submits what was recorded
  /// and waits for the GPU. Returns false, and leaves _outTexels alone, for a level that is not
  /// there or when the device failed.
  [[nodiscard]] bool ReadTexture(const Texture& _texture, std::uint32_t _mip, std::uint32_t _face, std::vector<std::byte>& _outTexels);

  /// Replaces _bytes.size() bytes of _buffer from _offsetBytes. A range that does not fit is
  /// reported, and nothing is recorded.
  void UpdateBuffer(Buffer& _buffer, std::uint32_t _offsetBytes, std::span<const std::byte> _bytes);

  /// Reads the whole of _buffer back. Submits what was recorded and waits for the GPU.
  [[nodiscard]] bool ReadBuffer(const Buffer& _buffer, std::vector<std::byte>& _outBytes);

  /// Clears mip _mip of a colour texture to _color, red first, as its format stores it. _layer is
  /// the face of a cube, the slice of that mip of a 3D texture, and 0 otherwise. A texture or level
  /// that is not there is reported, and nothing is recorded.
  void ClearColor(Texture& _texture, std::uint32_t _mip, std::uint32_t _layer, const std::array<float, 4>& _color);

  /// Clears a depth texture to _depth, from 0 to 1. Anything else is reported, and nothing is
  /// recorded.
  void ClearDepth(Texture& _texture, float _depth);

  /// Draws into _colors, SV_Target0 first, and tests against _depth, which may be null. The colour
  /// targets are all one size, and the viewport and scissor become the whole of it. A target that
  /// is not there, is of the wrong kind or is another size is reported, and no target is set. A
  /// texture that goes while it is a target stops being one.
  void SetTargets(std::span<const ColorTarget> _colors, Texture* _depth);

  /// Where a draw's clip space lands, in pixels from column 0 and row 0, which is the bottom row.
  void SetViewport(std::int32_t _x, std::int32_t _y, std::int32_t _width, std::int32_t _height);

  /// Draws only inside the rectangle, in the viewport's pixels, until DisableScissor or
  /// SetTargets.
  void SetScissor(std::int32_t _x, std::int32_t _y, std::int32_t _width, std::int32_t _height);

  void DisableScissor();

  void SetState(const DrawState& _state);

  /// The program the next draws run, with its constants as they are at each draw. It stays set
  /// until another is, or until it goes.
  void SetProgram(Program& _program);

  /// Draws the triangles _indexCount indices of _indices give, from _firstIndex, into the vertices
  /// of _vertices, which are laid out as _layout says. What cannot be drawn is reported, and
  /// nothing is recorded.
  void DrawIndexed(const Buffer& _vertices, const VertexLayout& _layout, const Buffer& _indices, IndexFormat _format,
                   std::uint32_t _firstIndex, std::uint32_t _indexCount);

  /// Draws the triangles _indices give, into _vertices, which are laid out as _layout says. Both
  /// go through the upload ring and last for this draw alone.
  void DrawTransient(std::span<const std::byte> _vertices, const VertexLayout& _layout, std::span<const std::byte> _indices,
                     IndexFormat _format);

  /// Submits what was recorded, without waiting for the GPU.
  void Flush();

private:
  friend class GraphicsDevice;
  friend struct GraphicsCore;
  struct Native;

  explicit DrawContext(GraphicsCore& _core);

  /// Makes the root signature and the shader-visible heaps. On failure returns false and says why
  /// in _error.
  [[nodiscard]] bool Initialize(std::string& _error);

  /// Stops a texture that goes from being a target.
  void Forget(const Texture::Native& _texture) noexcept;

  /// Unsets a program that goes, and hands its pipeline states to the device to release.
  void Forget(const Program::Native& _program);

  /// Pipeline states made so far: one for each program and state a draw has used.
  [[nodiscard]] std::size_t PipelineStates() const noexcept;

  std::unique_ptr<Native> m_native;
};

} // namespace Neuron
