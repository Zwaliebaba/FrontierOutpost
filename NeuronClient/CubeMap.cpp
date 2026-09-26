#include "CubeMap.h"

#include "Array.h"
#include "AutoPtr.h"
#include "Location.h"
#include "LteMath.h"
#include "Matrix.h"
#include "Renderer.h"
#include "RendererCore.h"
#include "Shader.h"
#include "StackFrame.h"
#include "Texture2D.h"
#include "Timer.h"
#include "Transform.h"
#include "V3.h"

#include <cstring>
#include <memory>
#include <span>
#include <vector>


TypeAlias(Reference<CubeMapT>, CubeMap);

namespace {
  inline uint GetLevelResolution(uint resolution, uint level) {
    for (uint i = 0; i < level; ++i)
      resolution /= 2;
    return resolution;
  }

  struct CubeMapImpl : public CubeMapT {
    typedef CubeMapT BaseType;
    DERIVED_TYPE_EX(CubeMapImpl)

    /* The texture on the GPU (LTE/RendererCore.h), with every mip, which
       generation and the IR map fill. A copy shares it, as a copy shared GL's
       name. */
    std::shared_ptr<GpuTexture> gpu;
    uint resolution;
    uint guid;
    TextureFormat::Enum format;
    bool created;

    CubeMapImpl() :
      gpu(std::make_shared<GpuTexture>()),
      resolution(0),
      format(TextureFormat::RGBA8),
      created(false)
    {
      static uint nextGUID = 0;
      this->guid = nextGUID++;
    }

    void BeginRender() {
      Renderer_PushBlendMode(BlendMode::Disabled);
      Renderer_PushCullMode(CullMode::Backface);
      Renderer_PushAllBuffers();
      Renderer_PushViewport(0, 0, resolution, resolution);
      Renderer_PushScissorOff();
      Renderer_PushZBuffer(false);
    }

    void Create(uint res, TextureFormat::Enum format) {
      this->resolution = res;
      this->format = format;
      created = true;

      /* Linear between mips, and clamped at the edges, as GL's were. */
      gpu->sampler.wrapU = Neuron::TextureWrap::ClampToEdge;
      gpu->sampler.wrapV = Neuron::TextureWrap::ClampToEdge;
      gpu->sampler.wrapW = Neuron::TextureWrap::ClampToEdge;
      if (!res)
        return;
      Neuron::Texture::Desc desc;
      desc.dimension = Neuron::TextureDimension::TextureCube;
      desc.format = ToNeuron(format);
      desc.widthPixels = res;
      desc.heightPixels = res;
      desc.depthPixels = 1;
      desc.mipLevels = Renderer_FullMipLevels(res, res);
      desc.name = "liblt cube map";
      gpu->texture = Renderer_Device().CreateTexture(desc);
    }

    void EndRender() {
      Renderer_PopZBuffer();
      Renderer_PopScissor();
      Renderer_PopViewport();
      Renderer_PopAllBuffers();
      Renderer_PopCullMode();
      Renderer_PopBlendMode();
    }

    void GenerateFromShader(
      Shader const& shader,
      bool generateMips,
      float maxJobTime)
    {
      SFRAME("Generate CubeMap");
      Renderer_SetShader(*shader);
      BeginRender();
      (*shader)
        ("halfTexel", 0.5f / (float)resolution)
        ("texelScale", (float)resolution / (float)(resolution - 1));

      for (uint i = 0; i < CubeFace::SIZE; ++i) {
        SetFace((CubeFace::Enum)i);

        Matrix invProjView =
          (Renderer_GetProjMatrix() * Renderer_GetViewMatrix()).Inverse();
        V3 upperL = invProjView.TransformV3Norm(V3(-1.0f, 1.0f, 0.5f), 1);
        V3 upperR = invProjView.TransformV3Norm(V3( 1.0f, 1.0f, 0.5f), 1);
        V3 lowerL = invProjView.TransformV3Norm(V3(-1.0f,-1.0f, 0.5f), 1);
        (*shader)
          ("origin", upperL)
          ("du", upperR - upperL)
          ("dv", lowerL - upperL);

        /* Variable job-size algorithm for making sure to achieve optimal GPU
           utilization without causing a timeout. In most cases, 1 second should
           be allowable (Windows default timeout is 2s I believe) */
        uint jobSize = 1;
        uint x = 0;

        Timer timer;
        while (x < resolution) {
          timer.Reset();
          Renderer_PushScissorOn(
            V2((float)x, 0),
            V2((float)jobSize, (float)resolution));
          Renderer_DrawQuad();
          Renderer_PopScissor();

          Renderer_Finish();
          x += jobSize;

          /* NOTE : This is a bit scary...if the first job terminates really
                    quickly, it seems like we may overestimate our capacity. We
                    should probably implement something to make sure the jobSize
                    scales up slowly rather than all at once. */
          float elapsed = timer.GetElapsed();
          jobSize = Min(Max((uint)((float)jobSize * (maxJobTime / elapsed)), 1U), resolution);
        }

        Renderer_PopColorBuffer(0);
      }

      EndRender();
      if (generateMips)
        GenerateMipmap();
    }

    void GenerateMipmap() {
      if (gpu->texture)
        Renderer_Context().GenerateMips(gpu->texture);
    }

    void GetData(
      CubeFace::Enum face,
      uint level,
      void* buffer) const
    {
      if (!gpu->texture || level >= gpu->texture.MipLevels())
        return;
      std::vector<std::byte> texels;
      if (Renderer_Context().ReadTexture(gpu->texture, level, (uint)face, texels))
        memcpy(buffer, texels.data(), texels.size());
    }

    TextureFormat::Enum GetFormat() const {
      return format;
    }

    uint GetResolution() const {
      return resolution;
    }

    void SetData(
      CubeFace::Enum face,
      uint level,
      void const* buffer) const
    {
      if (!gpu->texture || level >= gpu->texture.MipLevels())
        return;
      uint res = GetLevelResolution(resolution, level);
      Renderer_Context().UpdateTexture(
        const_cast<Neuron::Texture&>(gpu->texture), level, (uint)face,
        std::span<std::byte const>((std::byte const*)buffer,
          TextureFormat::Size(format) * res * res));
    }

    void SetFace(CubeFace::Enum face) {
      /* The face is the target's layer (plan section 5.5). */
      Renderer_PushColorBuffer(0, gpu->id, (uint)face);

      /* Set the view and projection matrices accordingly for this face. */
      V3 look = 0;
      V3 up = 0;
      if (face == CubeFace::PositiveX) {
        look = V3(1, 0, 0);
        up = V3(0, 1, 0);
      } else if (face == CubeFace::NegativeX) {
        look = V3(-1, 0, 0);
        up = V3(0, 1, 0);
      } else if (face == CubeFace::PositiveY) {
        look = V3(0, 1, 0);
        up = V3(0, 0, -1);
      } else if (face == CubeFace::NegativeY) {
        look = V3(0, -1, 0);
        up = V3(0, 0, 1);
      } else if (face == CubeFace::PositiveZ) {
        look = V3(0, 0, 1);
        up = V3(0, 1, 0);
      } else if (face == CubeFace::NegativeZ) {
        look = V3(0, 0, -1);
        up = V3(0, 1, 0);
      }

      /* NOTE : Need to have a way to change the origin. */
      /* TODO : Better yet, re-frame this in terms of a camera. Give the
                cubemap a camera from which to render. */
      V3 origin = 0;
      Renderer_SetViewTransform(Transform_LookUp(origin, look, up));
      Renderer_SetProjMatrix(Matrix::Perspective(kPi2, 1.0f, 0.1f, 100000.0f));
    }

    FIELDS {
      CubeMapImpl* self = (CubeMapImpl*)addr;

      m(&self->format, "format", Type_Get(self->format), aux);
      m(&self->resolution, "resolution", Type_Get(self->resolution), aux);

      size_t totalSize = TextureFormat::Size(self->format) *
        self->resolution * self->resolution;

      Array<uchar> buf(totalSize);

      if (self->created) {
        for (uint i = 0; i < CubeFace::SIZE; ++i) {
          self->GetData((CubeFace::Enum)i, 0, buf.data());
          m(&buf, "data", Type_Get(buf), aux);
        }
      }

      else {
        self->Create(self->resolution, self->format);
        for (uint i = 0; i < CubeFace::SIZE; ++i) {
          m(&buf, "data", Type_Get(buf), aux);
          self->SetData((CubeFace::Enum)i, 0, buf.data());
        }
        self->GenerateMipmap();
      }
    }

    DefineMetadataInline(CubeMapImpl)
  };

  DERIVED_IMPLEMENT(CubeMapImpl)
}

GpuTexture* CubeMap_GetGpu(CubeMapT const& cubeMap) {
  return static_cast<CubeMapImpl const&>(cubeMap).gpu.get();
}

CubeMap CubeMap_Create(uint resolution, TextureFormat::Enum format) {
  Reference<CubeMapImpl> self = new CubeMapImpl;
  self->Create(resolution, format);
  return self;
}
