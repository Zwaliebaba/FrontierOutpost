// NeuronClient/Buffer.h
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace Neuron
{

struct GraphicsCore;

/// A buffer on the GPU that lasts: a mesh's vertices or indices, or elements a compute shader
/// reads. GraphicsDevice makes it, and DrawContext fills, reads and draws with it. What changes
/// every frame goes through the context's upload ring instead.
class Buffer
{
public:
  struct Desc
  {
    std::uint32_t sizeBytes;
    std::uint32_t strideBytes; // a vertex's or an element's; 0 when there is none
    std::string_view name;     // for the debug layer's messages, and PIX
  };

  Buffer() noexcept;
  /// Hands the buffer to its device, which releases it once the GPU has finished with it.
  ~Buffer();
  Buffer(Buffer&& _other) noexcept;
  Buffer& operator=(Buffer&& _other) noexcept;
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;

  /// False for a buffer that was never made, or that the device failed to make.
  explicit operator bool() const noexcept;

  [[nodiscard]] std::uint32_t SizeBytes() const noexcept;
  [[nodiscard]] std::uint32_t StrideBytes() const noexcept;

private:
  friend class DrawContext;
  friend class GraphicsDevice;
  struct Native;

  /// Makes the resource, or reports why not and returns an empty buffer.
  [[nodiscard]] static Buffer Make(const std::shared_ptr<GraphicsCore>& _core, const Desc& _desc);

  /// Hands the buffer to its device, as the destructor does.
  void Release() noexcept;

  std::shared_ptr<GraphicsCore> m_core; // the device's, which the buffer keeps alive
  std::unique_ptr<Native> m_native;
};

} // namespace Neuron
