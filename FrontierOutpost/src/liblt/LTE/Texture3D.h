#ifndef LTE_Texture3D_h__
#define LTE_Texture3D_h__

#include "BaseType.h"
#include "GraphicsEnum.h"
#include "Reference.h"

struct Texture3DT : public RefCounted {
  BASE_TYPE(Texture3DT)

  virtual void AddressBorder(float r, float g, float b, float a) = 0;
  virtual void AddressClamp() = 0;
  virtual void AddressRepeat() = 0;

  virtual void GenerateMipmap() = 0;

  virtual uint GetWidth() const = 0;
  virtual uint GetHeight() const = 0;
  virtual uint GetDepth() const = 0;

  virtual void GetData(
    uchar* buffer,
    PixelFormat::Enum format = PixelFormat::RGBA,
    uint lod = 0) const = 0;

  virtual void GetData(
    float* buffer,
    PixelFormat::Enum format = PixelFormat::Red,
    uint lod = 0) const = 0;

  virtual void SetData(
    uint x, uint y, uint z,
    uint width, uint height, uint depth,
    PixelFormat::Enum pixelFormat,
    DataFormat::Enum dataFormat,
    void const* buffer) = 0;

  virtual void SetMagFilter(TextureFilter::Enum) = 0;
  virtual void SetMinFilter(TextureFilterMip::Enum) = 0;

  FIELDS {}
};

LT_API Texture3D Texture3D_Create(
  uint width,
  uint height,
  uint depth,
  TextureFormat::Enum internalFormat);

#endif
