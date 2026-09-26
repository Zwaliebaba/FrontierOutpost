#include "Texture2D.h"

#include "Array.h"
#include "AutoPtr.h"
#include "Location.h"
#include "Matrix.h"
#include "Pointer.h"
#include "LteProgram.h"
#include "ProgramLog.h"
#include "Renderer.h"
#include "RendererCore.h"
#include "Shader.h"
#include "StackFrame.h"
#include "Timer.h"
#include "LteWindow.h"

#include "ImageFile.h"

#include <cctype>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

TypeAlias(Reference<Texture2DT>, Texture);

namespace {
  void IncrementalGenerateFromShader(
    uint width, 
    uint height,
    ShaderT* shader,
    bool segmented,
    float maxJobTime)
  {
    RendererState rs(BlendMode::Disabled, CullMode::Backface, false, false);
    Renderer_SetShader(*shader);
    Renderer_PushScissorOff();

    if (segmented) {
      /* Variable job-size algorithm for making sure to achieve optimal GPU
         utilization without causing a timeout. In most cases, 1 second should
         be allowable (Windows default timeout is 2s) */
      uint jobSize = 1;
      uint x = 0;

      Timer timer;
      while (x < width) {
        timer.Reset();
        Renderer_PushScissorOn(V2((float)x, 0), V2((float)jobSize, (float)height));
        Renderer_DrawQuad();
        Renderer_PopScissor();
        Renderer_Finish();
        x += jobSize;

        /* NOTE : This is a bit scary...if the first job terminates really
                  quickly, it seems like we may overestimate our capacity. We
                  should probably implement something to make sure the jobSize
                  scales up slowly rather than all at once. */
        float elapsed = timer.GetElapsed();
        jobSize = Min(Max((uint)((float)jobSize * (maxJobTime / elapsed)), 1U), width);
      }
    } else {
      Renderer_DrawQuad();
    }

    Renderer_PopScissor();
  }

  struct Texture2DImpl : public Texture2DT {
    typedef Texture2DT BaseType;
    DERIVED_TYPE_EX(Texture2DImpl)

    /* The texture on the GPU (LTE/RendererCore.h), which a copy shares, as a
       copy shared GL's name. One without texels, 0 by 0, has none, as GL's had
       none to draw into. */
    std::shared_ptr<GpuTexture> gpu;
    TextureFormat::Enum format;
    uint width;
    uint height;
    uint guid;
    int attachmentIndex;
    bool created;

    Texture2DImpl() :
      gpu(std::make_shared<GpuTexture>()),
      format(TextureFormat::RGBA8),
      width(0),
      height(0),
      attachmentIndex(-1),
      created(false)
    {
      static uint nextGUID = 0;
      this->guid = nextGUID++;
    }

    ~Texture2DImpl() {
      /* Deleting a texture that is bound to the framebuffer is an error. */
      LTE_ASSERT(attachmentIndex == -1);
    }

    void Bind(uint bufferIndex) {
      if (attachmentIndex >= 0)
        Unbind();
      attachmentIndex = bufferIndex;
      Renderer_PushColorBuffer(attachmentIndex, gpu->id);
      Renderer_PushViewport(0, 0, width, height);
    }

    void Unbind() {
      LTE_ASSERT(attachmentIndex >= 0);
      Renderer_PopViewport();
      Renderer_PopColorBuffer(attachmentIndex);
      attachmentIndex = -1;
    }

    void Create(
      uint width,
      uint height,
      TextureFormat::Enum format,
      void const* data)
    {
      this->width = width;
      this->height = height;
      this->format = format;
      created = true;
      if (!width || !height)
        return;

      /* Colour textures have every mip, as GL gave them; depth is only drawn
         into, so it has one. */
      bool const depth = format == TextureFormat::Depth32F;
      Neuron::Texture::Desc desc;
      desc.dimension = Neuron::TextureDimension::Texture2D;
      desc.format = ToNeuron(format);
      desc.widthPixels = width;
      desc.heightPixels = height;
      desc.depthPixels = 1;
      desc.mipLevels = depth ? 1 : Renderer_FullMipLevels(width, height);
      desc.name = "liblt texture";
      gpu->texture = Renderer_Device().CreateTexture(desc);
      if (depth)
        return;

      /* GL gave every colour texture the most anisotropy it had. */
      gpu->sampler.maxAnisotropy = 16;

      /* GL made the mips of a texture made from data, which it may be sampled
         with. Made without, the texture is all 0, mips included, as GL's
         were. */
      if (data) {
        Renderer_Context().UpdateTexture(gpu->texture, 0, 0,
          std::span<std::byte const>((std::byte const*)data, GetMemory()));
        GenerateMipmap();
      }
    }

    void GenerateMipmap() {
      if (gpu->texture && format != TextureFormat::Depth32F)
        Renderer_Context().GenerateMips(gpu->texture);
    }

    void GetData(void* buffer) const {
      if (!gpu->texture)
        return;
      std::vector<std::byte> texels;
      if (Renderer_Context().ReadTexture(gpu->texture, 0, 0, texels))
        memcpy(buffer, texels.data(), texels.size());
    }

    TextureFormat::Enum GetFormat() const {
      return format;
    }

    size_t GetMemory() const {
      return TextureFormat::Size(format) * width * height;
    }

    uint GetHeight() const {
      return height;
    }

    uint GetWidth() const {
      return width;
    }

    void SaveTo(String const& path, bool flip) {
      /* RGBA8, as glGetTexImage converted it. */
      Array<uchar> imageData(4 * GetWidth() * GetHeight());
      std::vector<std::byte> texels;
      if (gpu->texture && Renderer_Context().ReadTexture(gpu->texture, 0, 0, texels))
        Texels_Read(texels.data(), format, (size_t)width * height,
          PixelFormat::RGBA, DataFormat::UnsignedByte, imageData.data());

      if (flip) {
        uint bpp = TextureFormat::Size(format);
        Array<uchar> buf(bpp * width);
        for (uint y = 0; y < height / 2; ++y) {
          memcpy(buf.data(), &imageData[bpp * width * y], bpp * width);
          memmove(&imageData[bpp * width * y],
                  &imageData[bpp * width * (height - y - 1)], bpp * width);
          memcpy(&imageData[bpp * width * (height - y - 1)], buf.data(), bpp * width);
        }
      }

      /* Always a PNG (ADR-011), so a name that says otherwise is refused. */
      String extension = path.size() >= 4 ? path.substr(path.size() - 4) : String();
      for (char& c : extension)
        c = (char)std::tolower((unsigned char)c);
      if (extension != ".png") {
        Log_Error("Texture2D: " + path + " is not a .png path, and only PNG is written");
        return;
      }

      std::vector<std::byte> file;
      std::string error;
      if (!Neuron::ImageFile::EncodePng(width, height,
            std::as_bytes(std::span<uchar const>(imageData.data(), imageData.size())),
            file, error))
      {
        Log_Error("Texture2D: " + path + ": " + error);
        return;
      }
      Array<uchar> bytes(file.size());
      memcpy(bytes.data(), file.data(), file.size());
      if (!Location_File(path)->Write(bytes))
        Log_Error("Texture2D: failed to write " + path);
    }

    void SetData(
      uint x,
      uint y,
      uint w,
      uint h,
      void const* buffer)
    {
      Upload(x, y, w, h, std::span<std::byte const>(
        (std::byte const*)buffer, TextureFormat::Size(format) * w * h));
    }

    void SetData(
      uint x,
      uint y,
      uint w,
      uint h,
      PixelFormat::Enum pixelFormat,
      DataFormat::Enum dataFormat,
      void const* buffer)
    {
      std::vector<std::byte> texels;
      Texels_Convert(buffer, pixelFormat, dataFormat, (size_t)w * h, format, texels);
      Upload(x, y, w, h, texels);
    }

    void Upload(
      uint x,
      uint y,
      uint w,
      uint h,
      std::span<std::byte const> texels)
    {
      if (!gpu->texture || !w || !h)
        return;
      if (format == TextureFormat::Depth32F) {
        Log_Error("Texture2D: a depth texture is only drawn into, not filled");
        return;
      }
      /* GL refused a region outside the texture, and set nothing. */
      if (x >= width || w > width - x || y >= height || h > height - y) {
        Log_Error(Stringize() | "Texture2D: " | w | " by " | h | " texels at (" |
          x | ", " | y | ") do not fit in " | width | " by " | height |
          "; nothing was set");
        return;
      }
      Neuron::TextureRegion region;
      region.xPixels = x;
      region.yPixels = y;
      region.zPixels = 0;
      region.widthPixels = w;
      region.heightPixels = h;
      region.depthPixels = 1;
      Renderer_Context().UpdateTexture(gpu->texture, 0, 0, region, texels);
    }

    void SetLodBias(float bias) {
      gpu->sampler.lodBias = bias;
    }

    void SetMagFilter(TextureFilter::Enum filter) {
      gpu->sampler.magFilter = filter == TextureFilter::Nearest
        ? Neuron::TextureFilter::Nearest
        : Neuron::TextureFilter::Linear;
    }

    void SetMaxLod(int maxLod) {
      gpu->sampler.maxLod = (float)maxLod;
    }

    void SetMinFilter(TextureFilterMip::Enum filter) {
      switch (filter) {
      case TextureFilterMip::Linear:
        gpu->sampler.minFilter = Neuron::TextureFilter::Linear;
        gpu->sampler.mipFilter = Neuron::MipFilter::None;
        break;
      case TextureFilterMip::LinearMipLinear:
        gpu->sampler.minFilter = Neuron::TextureFilter::Linear;
        gpu->sampler.mipFilter = Neuron::MipFilter::Linear;
        break;
      case TextureFilterMip::Nearest:
        gpu->sampler.minFilter = Neuron::TextureFilter::Nearest;
        gpu->sampler.mipFilter = Neuron::MipFilter::None;
        break;
      }
    }

    void SetMinLod(int minLod) {
      gpu->sampler.minLod = (float)minLod;
    }

    void SetWrapMode(TextureWrapMode::Enum mode) {
      Neuron::TextureWrap wrap = Neuron::TextureWrap::Repeat;
      if (mode == TextureWrapMode::ClampToBorder)
        wrap = Neuron::TextureWrap::ClampToBorder;
      else if (mode == TextureWrapMode::ClampToEdge)
        wrap = Neuron::TextureWrap::ClampToEdge;
      gpu->sampler.wrapU = wrap;
      gpu->sampler.wrapV = wrap;
    }

    FIELDS {
      Texture2DImpl* self = (Texture2DImpl*)addr;
      m(&self->width, "width", Type_Get(self->width), aux);
      m(&self->height, "height", Type_Get(self->height), aux);
      m(&self->guid, "guid", Type_Get(self->guid), aux);
      m(&self->format, "format", Type_Get(self->format), aux);

      Array<uchar> buffer(self->GetMemory());

      if (!self->created) {
        m(&buffer, "data", Type_Get(buffer), aux);
        self->Create(self->width, self->height, self->format, buffer.data());
      } else {
        self->GetData(buffer.data());
        m(&buffer, "data", Type_Get(buffer), aux);
      }
    }

    DefineMetadataInline(Texture2DImpl)
  };

  DERIVED_IMPLEMENT(Texture2DImpl)
}

GpuTexture* Texture2D_GetGpu(Texture2DT const& texture) {
  return static_cast<Texture2DImpl const&>(texture).gpu.get();
}

Texture2D Texture2D_Filter(Texture2D const& texture, Shader const& shader) {
  Texture2D self = Texture_Create(
    texture->GetWidth(),
    texture->GetHeight(),
    texture->GetFormat());
  (*shader)("texture", texture);
  Texture_Generate(self, shader);
  return self;
}

Texture2D Texture_Create(
  uint width,
  uint height,
  TextureFormat::Enum format,
  void const* data)
{
  Reference<Texture2DImpl> self = new Texture2DImpl;
  self->Create(width, height, format, data);
  return self;
}

void Texture_Generate(
  Texture2D const& self,
  Shader const& shader,
  bool generateMips,
  bool segmented,
  float maxJobTime)
{
  AUTO_FRAME;
  Renderer_PushAllBuffers();
  self->Bind(0);
  IncrementalGenerateFromShader(
    self->GetWidth(),
    self->GetHeight(),
    shader.t, segmented, maxJobTime);
  self->Unbind();
  Renderer_PopAllBuffers();

  if (generateMips)
    self->GenerateMipmap();
}

void Texture_Generate(
  Texture2D const& source1,
  Texture2D const& source2,
  Shader const& shader,
  bool generateMips,
  bool segmented,
  float maxJobTime)
{
  AUTO_FRAME;
  LTE_ASSERT(source1->GetWidth() == source2->GetWidth());
  LTE_ASSERT(source1->GetHeight() == source2->GetHeight());
  Renderer_PushAllBuffers();
  source1->Bind(0);
  source2->Bind(1);
  IncrementalGenerateFromShader(
    source1->GetWidth(),
    source2->GetHeight(),
    shader.t, segmented, maxJobTime);
  source2->Unbind();
  source1->Unbind();
  Renderer_PopAllBuffers();

  if (generateMips) {
    source1->GenerateMipmap();
    source2->GenerateMipmap();
  }
}

DefineFunction(Texture_LoadFrom) {
  AutoPtr< Array<uchar> > arr = args.source->Read();
  if (!arr)
    Log_Critical("Failed to load texture from " + args.source->ToString());

  Neuron::ImageFile image;
  std::string error;
  if (!Neuron::ImageFile::Decode(
        std::as_bytes(std::span<uchar const>(arr->data(), arr->size())), image, error))
    Log_Critical("Failed to decode texture " + args.source->ToString() + ": " + error);

  return Texture_Create(
    image.WidthPixels(),
    image.HeightPixels(),
    TextureFormat::RGBA8,
    image.Pixels().data());
}

DefineFunction(Texture_ScreenCapture) {
  struct char4 {
    uchar c[4];
  };

  /* The frame the window shows, GL's default framebuffer: RGBA8, its bottom
     row first, as glReadPixels read it. */
  Neuron::Texture const& frame = Renderer_GetFrame().texture;
  V2U size(frame.WidthPixels(), frame.HeightPixels());
  Array<char4> buf(size.x * size.y);
  std::vector<std::byte> texels;
  if (Renderer_Context().ReadTexture(frame, 0, 0, texels))
    memcpy(buf.data(), texels.data(), texels.size());

  /* Set alpha to 1. */
  for (uint i = 0; i < size.x * size.y; ++i)
    buf[i].c[3] = 0xff;

  /* Mirror vertically, since GL is retarded. */
  for (uint y = 0; y < size.y / 2; ++y)
  for (uint x = 0; x < size.x; ++x)
    Swap(buf[y * size.x + x], buf[(size.y - y - 1) * size.x + x]);

  return Texture_Create(size.x, size.y, TextureFormat::RGBA8, buf.data());
}
