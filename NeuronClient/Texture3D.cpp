#include "Texture3D.h"

#include "ProgramLog.h"
#include "RendererCore.h"

#include <cstring>
#include <memory>
#include <vector>

namespace {
  struct Texture3DImpl : public Texture3DT {
    /* The texture on the GPU (LTE/RendererCore.h), with one level: nothing
       asks for its mips. A copy shares it, as a copy shared GL's name. One
       without texels has none. */
    std::shared_ptr<GpuTexture> gpu;
    TextureFormat::Enum format;
    uint width;
    uint height;
    uint depth;
    DERIVED_TYPE_EX(Texture3DImpl)

    Texture3DImpl() :
      gpu(std::make_shared<GpuTexture>()),
      format(TextureFormat::R32F),
      width(0),
      height(0),
      depth(0)
      {}

    Texture3DImpl(
        uint width,
        uint height,
        uint depth,
        TextureFormat::Enum format) :
      gpu(std::make_shared<GpuTexture>()),
      format(format),
      width(width),
      height(height),
      depth(depth)
    {
      /* Linear, without mips, and repeating, as GL's were. */
      gpu->sampler.mipFilter = Neuron::MipFilter::None;
      if (!width || !height || !depth)
        return;
      Neuron::Texture::Desc desc;
      desc.dimension = Neuron::TextureDimension::Texture3D;
      desc.format = ToNeuron(format);
      desc.widthPixels = width;
      desc.heightPixels = height;
      desc.depthPixels = depth;
      desc.mipLevels = 1;
      desc.name = "liblt 3D texture";
      gpu->texture = Renderer_Device().CreateTexture(desc);
    }

    void SetWrap(Neuron::TextureWrap wrap) {
      gpu->sampler.wrapU = wrap;
      gpu->sampler.wrapV = wrap;
      gpu->sampler.wrapW = wrap;
    }

    void AddressBorder(float r, float g, float b, float a) {
      SetWrap(Neuron::TextureWrap::ClampToBorder);
      gpu->sampler.borderColor = {r, g, b, a};
    }

    void AddressClamp() {
      SetWrap(Neuron::TextureWrap::ClampToEdge);
    }

    void AddressRepeat() {
      SetWrap(Neuron::TextureWrap::Repeat);
    }

    void GenerateMipmap() {
      /* A 3D texture here has one level, and the core makes no 3D mips
         (plan section 5.3); nothing calls this. */
      Log_Warning("Texture3D: a 3D texture has no mips to make");
    }

    uint GetWidth() const {
      return width;
    }

    uint GetHeight() const {
      return height;
    }

    uint GetDepth() const {
      return depth;
    }

    /* Level lod, as glGetTexImage converted it to _pixelFormat channels of
       _dataFormat. A level that is not there leaves the buffer alone, as GL's
       error did. */
    void Read(void* buffer, PixelFormat::Enum pixelFormat, DataFormat::Enum dataFormat, uint lod) const {
      if (!gpu->texture || lod >= gpu->texture.MipLevels())
        return;
      std::vector<std::byte> texels;
      if (!Renderer_Context().ReadTexture(gpu->texture, lod, 0, texels))
        return;
      size_t const count = (size_t)gpu->texture.WidthPixels(lod) *
        gpu->texture.HeightPixels(lod) * gpu->texture.DepthPixels(lod);
      Texels_Read(texels.data(), format, count, pixelFormat, dataFormat, buffer);
    }

    void GetData(uchar* buffer, PixelFormat::Enum format, uint lod) const {
      Read(buffer, format, DataFormat::UnsignedByte, lod);
    }

    void GetData(float* buffer, PixelFormat::Enum format, uint lod) const {
      Read(buffer, format, DataFormat::Float, lod);
    }

    void SetData(
      uint x, uint y, uint z,
      uint width, uint height, uint depth,
      PixelFormat::Enum pixelFormat,
      DataFormat::Enum dataFormat,
      void const* buffer)
    {
      if (!gpu->texture || !width || !height || !depth)
        return;
      /* GL refused a region outside the texture, and set nothing. */
      if (x >= this->width || width > this->width - x ||
          y >= this->height || height > this->height - y ||
          z >= this->depth || depth > this->depth - z)
      {
        Log_Error("Texture3D: a region outside the texture; nothing was set");
        return;
      }
      std::vector<std::byte> texels;
      Texels_Convert(buffer, pixelFormat, dataFormat,
        (size_t)width * height * depth, format, texels);
      Neuron::TextureRegion region;
      region.xPixels = x;
      region.yPixels = y;
      region.zPixels = z;
      region.widthPixels = width;
      region.heightPixels = height;
      region.depthPixels = depth;
      Renderer_Context().UpdateTexture(gpu->texture, 0, 0, region, texels);
    }

    void SetMagFilter(TextureFilter::Enum filter) {
      gpu->sampler.magFilter = filter == TextureFilter::Nearest
        ? Neuron::TextureFilter::Nearest
        : Neuron::TextureFilter::Linear;
    }

    void SetMinFilter(TextureFilterMip::Enum filter) {
      gpu->sampler.minFilter = filter == TextureFilterMip::Nearest
        ? Neuron::TextureFilter::Nearest
        : Neuron::TextureFilter::Linear;
      gpu->sampler.mipFilter = filter == TextureFilterMip::LinearMipLinear
        ? Neuron::MipFilter::Linear
        : Neuron::MipFilter::None;
    }
  };
}

GpuTexture* Texture3D_GetGpu(Texture3DT const& texture) {
  return static_cast<Texture3DImpl const&>(texture).gpu.get();
}

Texture3D Texture3D_Create(
  uint width,
  uint height,
  uint depth,
  TextureFormat::Enum format)
{
  return new Texture3DImpl(width, height, depth, format);
}
