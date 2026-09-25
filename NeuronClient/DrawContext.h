// NeuronClient/DrawContext.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Neuron
{

class Buffer;
class Texture;
struct GraphicsCore;

/// Records a device's work for the GPU, as a Direct3D 11 immediate context does
/// (Design/ADR/ADR-007). It knows the state each texture and buffer was left in and inserts the
/// barriers, and it takes what it uploads from a ring of pages that are reused once the GPU has
/// finished with them. What it records goes to the GPU at GraphicsDevice::EndFrame, or sooner at
/// Flush or a readback. A device has one, GraphicsDevice::Context, and it is not thread-safe.
class DrawContext
{
public:
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

  /// Submits what was recorded, without waiting for the GPU.
  void Flush();

private:
  friend class GraphicsDevice;
  struct Native;

  explicit DrawContext(GraphicsCore& _core);

  std::unique_ptr<Native> m_native;
};

} // namespace Neuron
