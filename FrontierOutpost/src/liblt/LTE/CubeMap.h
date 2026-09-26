#ifndef LTE_CubeMap_h__
#define LTE_CubeMap_h__

#include "BaseType.h"
#include "GraphicsEnum.h"
#include "Reference.h"
#include "String.h"

struct CubeMapT : public RefCounted {
  BASE_TYPE(CubeMapT)

  virtual void BeginRender() = 0;
  virtual void EndRender() = 0;

  virtual void GenerateFromShader(
    Shader const& shader,
    bool generateMips = true,
    float maxJobTime = 1) = 0;

  virtual void GenerateMipmap() = 0;

  virtual void GetData(
    CubeFace::Enum face,
    uint mipLevel,
    void* buffer) const = 0;

  virtual TextureFormat::Enum GetFormat() const = 0;

  virtual uint GetResolution() const = 0;


  virtual void SetData(
    CubeFace::Enum face,
    uint mipLevel,
    void const* buffer) const = 0;

  FIELDS {}
};

LT_API CubeMap CubeMap_Create(
  uint resolution,
  TextureFormat::Enum format = TextureFormat::RGBA8);

#endif
