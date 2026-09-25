// NeuronClient/Texture.h
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

namespace Neuron
{

struct GraphicsCore;

/// The eight formats liblt's textures take: its seven colour formats, and depth (Design/ADR/ADR-007).
/// There is no sRGB: the game does its own gamma.
enum class TextureFormat : std::uint8_t
{
  R8,      // R8_UNORM
  Rg8,     // R8G8_UNORM
  Rgba8,   // R8G8B8A8_UNORM
  R16F,    // R16_FLOAT
  Rgba16F, // R16G16B16A16_FLOAT
  R32F,    // R32_FLOAT
  Rgba32F, // R32G32B32A32_FLOAT
  Depth32F // D32_FLOAT
};

enum class TextureDimension : std::uint8_t
{
  Texture2D,
  TextureCube, // six square faces
  Texture3D
};

/// The bytes one texel of _format takes.
[[nodiscard]] std::uint32_t TexelBytes(TextureFormat _format) noexcept;

/// A texture on the GPU, with its mips. GraphicsDevice makes it, and DrawContext fills, reads and
/// draws with it. Its rows are stored in the order its data gives them, row 0 first, which liblt
/// takes as the bottom of the image, as OpenGL did (plan §5.5). Any colour texture can be a render
/// target, and one with mips, or any 3D texture, can be written by a compute shader too.
class Texture
{
public:
  struct Desc
  {
    TextureDimension dimension;
    TextureFormat format; // a depth texture is 2D, with one mip level
    std::uint32_t widthPixels;
    std::uint32_t heightPixels; // a cube's faces are square, so its height is its width
    std::uint32_t depthPixels;  // a 3D texture's slices; 1 for the others
    std::uint32_t mipLevels;    // 1 for none, FullMipLevels for the whole chain
    std::string_view name;      // for the debug layer's messages, and PIX
  };

  /// The mips down to one pixel in each direction.
  [[nodiscard]] static std::uint32_t FullMipLevels(std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                                                   std::uint32_t _depthPixels) noexcept;

  Texture() noexcept;
  /// Hands the texture to its device, which releases it once the GPU has finished with it.
  ~Texture();
  Texture(Texture&& _other) noexcept;
  Texture& operator=(Texture&& _other) noexcept;
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;

  /// False for a texture that was never made, or that the device failed to make.
  explicit operator bool() const noexcept;

  [[nodiscard]] TextureDimension Dimension() const noexcept;
  [[nodiscard]] TextureFormat Format() const noexcept;
  [[nodiscard]] std::uint32_t MipLevels() const noexcept;

  /// Six for a cube, one otherwise.
  [[nodiscard]] std::uint32_t Faces() const noexcept;

  /// A mip's size, halved for each level and never below one pixel.
  [[nodiscard]] std::uint32_t WidthPixels(std::uint32_t _mip = 0) const noexcept;
  [[nodiscard]] std::uint32_t HeightPixels(std::uint32_t _mip = 0) const noexcept;
  [[nodiscard]] std::uint32_t DepthPixels(std::uint32_t _mip = 0) const noexcept;

private:
  friend class DrawContext;
  friend class GraphicsDevice;
  struct Native;

  /// Makes the resource, or reports why not and returns an empty texture.
  [[nodiscard]] static Texture Make(const std::shared_ptr<GraphicsCore>& _core, const Desc& _desc);

  /// Hands the texture to its device, as the destructor does.
  void Release() noexcept;

  std::shared_ptr<GraphicsCore> m_core; // the device's, which the texture keeps alive
  std::unique_ptr<Native> m_native;
};

} // namespace Neuron
