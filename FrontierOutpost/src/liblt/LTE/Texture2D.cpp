#include "Texture2D.h"

#include "Array.h"
#include "AutoPtr.h"
#include "Location.h"
#include "Matrix.h"
#include "Pointer.h"
#include "Program.h"
#include "ProgramLog.h"
#include "Renderer.h"
#include "RendererGL.h"
#include "Shader.h"
#include "StackFrame.h"
#include "Timer.h"
#include "Window.h"

#include "ImageFile.h"

#include <cctype>
#include <cstring>
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
      GL_Enable(GL_Capability::ScissorTest);
      uint jobSize = 1;
      uint x = 0;

      Timer timer;
      while (x < width) {
        timer.Reset();
        GL_Scissor(x, 0, jobSize, height);
        Renderer_DrawQuad();
        GL_Finish();
        x += jobSize;

        /* NOTE : This is a bit scary...if the first job terminates really
                  quickly, it seems like we may overestimate our capacity. We
                  should probably implement something to make sure the jobSize
                  scales up slowly rather than all at once. */
        float elapsed = timer.GetElapsed();
        jobSize = Min(Max((uint)((float)jobSize * (maxJobTime / elapsed)), 1U), width);
      }
      GL_Disable(GL_Capability::ScissorTest);
    } else {
      Renderer_DrawQuad();
    }

    Renderer_PopScissor();
  }

  struct Texture2DImpl : public Texture2DT {
    typedef Texture2DT BaseType;
    DERIVED_TYPE_EX(Texture2DImpl)

    GL_Texture glBuffer;
    TextureFormat::Enum format;
    uint width;
    uint height;
    uint guid;
    int attachmentIndex;

    Texture2DImpl() :
      glBuffer(GL_NullTexture),
      width(0),
      height(0),
      attachmentIndex(-1)
    {
      static uint nextGUID = 0;
      this->guid = nextGUID++;
    }

    ~Texture2DImpl() {
      /* Deleting a texture that is bound to the framebuffer is an error. */
      LTE_ASSERT(attachmentIndex == -1);
      if (!Program_InStaticSection())
        GL_DeleteTexture(glBuffer);
    }

    void Bind(uint bufferIndex) {
      if (attachmentIndex >= 0)
        Unbind();
      attachmentIndex = bufferIndex;
      Renderer_PushColorBuffer(attachmentIndex, glBuffer, guid);
      Renderer_PushViewport(0, 0, width, height);
    }

    void Unbind() {
      LTE_ASSERT(attachmentIndex >= 0);
      Renderer_PopViewport();
      Renderer_PopColorBuffer(attachmentIndex);
      attachmentIndex = -1;
    }

    void BindInput(uint unitIndex) const {
      GL_ActiveTexture(unitIndex);
      GL_BindTexture(GL_TextureTargetBindable::T2D, glBuffer);
      GL_ActiveTexture(0);
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

      glBuffer = GLU::CreateTexture2D(width, height, ToGL(format));
      GL_TexImage2D(
        GL_TextureTarget::T2D, 0, ToGL(format), width, height,
        ToGLPixelFormat(format),
        ToGLDataFormat(format), data);

      /* A depth buffer is only drawn into, so it has no mips. */
      if (format == TextureFormat::Depth32F)
        return;
      GL_GenerateMipmap(GL_TextureTarget::T2D);

      GLfloat fLargest;
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &fLargest);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, fLargest);
    }

    void GenerateMipmap() {
      GL_BindTexture(GL_TextureTargetBindable::T2D, glBuffer);
      GL_GenerateMipmap(GL_TextureTarget::T2D);
    }

    void GetData(void* buffer) const {
      GL_BindTexture(GL_TextureTargetBindable::T2D, glBuffer);
      GL_GetTexImage(GL_TextureTarget::T2D, 0,
                     ToGLPixelFormat(format),
                     ToGLDataFormat(format), buffer);
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
      Array<uchar> imageData(4 * GetWidth() * GetHeight());
      GL_BindTexture(GL_TextureTargetBindable::T2D, glBuffer);
      GL_GetTexImage(
        GL_TextureTarget::T2D,
        0,
        GL_PixelFormat::RGBA,
        GL_DataFormat::UnsignedByte,
        imageData.data());

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
      Upload(x, y, w, h, ToGLPixelFormat(format), ToGLDataFormat(format), buffer);
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
      Upload(x, y, w, h, ToGL(pixelFormat), ToGL(dataFormat), buffer);
    }

    void Upload(
      uint x,
      uint y,
      uint w,
      uint h,
      GL_PixelFormat::Enum pixelFormat,
      GL_DataFormat::Enum dataFormat,
      void const* buffer)
    {
      BindInput(0);
      GL_TexSubImage2D(
        GL_TextureTarget::T2D, 0, x, y, w, h,
        pixelFormat, dataFormat, buffer);
    }

    void SetLodBias(float bias) {
      BindInput(0);
      GL_TexParameter(
        GL_TextureTarget::T2D,
        GL_TextureParameter::LODBias,
        bias);
    }

    void SetMagFilter(TextureFilter::Enum filter) {
      BindInput(0);
      GL_TexMagFilter(GL_TextureTarget::T2D, ToGL(filter));
    }

    void SetMaxLod(int maxLod) {
      BindInput(0);
      GL_TexParameter(
        GL_TextureTarget::T2D,
        GL_TextureParameter::MaxLOD,
        maxLod);
    }

    void SetMinFilter(TextureFilterMip::Enum filter) {
      BindInput(0);
      GL_TexMinFilter(GL_TextureTarget::T2D, ToGL(filter));
    }

    void SetMinLod(int minLod) {
      BindInput(0);
      GL_TexParameter(
        GL_TextureTarget::T2D,
        GL_TextureParameter::MinLOD,
        minLod);
    }

    void SetWrapMode(TextureWrapMode::Enum mode) {
      BindInput(0);
      GL_TexWrapMode(GL_TextureTarget::T2D, GL_TextureCoordinate::S, ToGL(mode));
      GL_TexWrapMode(GL_TextureTarget::T2D, GL_TextureCoordinate::T, ToGL(mode));
    }

    FIELDS {
      Texture2DImpl* self = (Texture2DImpl*)addr;
      m(&self->width, "width", Type_Get(self->width), aux);
      m(&self->height, "height", Type_Get(self->height), aux);
      m(&self->guid, "guid", Type_Get(self->guid), aux);
      m(&self->format, "format", Type_Get(self->format), aux);

      Array<uchar> buffer(self->GetMemory());

      if (self->glBuffer == GL_NullTexture) {
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

GL_Texture Texture2D_GetGLTexture(Texture2DT const& texture) {
  return static_cast<Texture2DImpl const&>(texture).glBuffer;
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

  V2U size = Window_Get()->GetSize();
  Array<char4> buf(size.x * size.y);
  GL_ReadPixels(
    0, 0, size.x, size.y,
    GL_PixelFormat::RGBA,
    GL_DataFormat::UnsignedByte,
    buf.data());

  /* Set alpha to 1. */
  for (uint i = 0; i < size.x * size.y; ++i)
    buf[i].c[3] = 0xff;

  /* Mirror vertically, since GL is retarded. */
  for (uint y = 0; y < size.y / 2; ++y)
  for (uint x = 0; x < size.x; ++x)
    Swap(buf[y * size.x + x], buf[(size.y - y - 1) * size.x + x]);

  return Texture_Create(size.x, size.y, TextureFormat::RGBA8, buf.data());
}
