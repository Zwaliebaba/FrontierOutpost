#ifndef LTE_RendererGL_h__
#define LTE_RendererGL_h__

/* The OpenGL layer's own plumbing, which Renderer.cpp, Texture2D.cpp,
 * Texture3D.cpp, CubeMap.cpp and Mesh.cpp share and nothing else includes. The
 * public headers speak GraphicsEnum.h; this turns it into OpenGL. It goes when
 * the layer moves onto NeuronClient (Design/Plan/NeuronClient-migration.md,
 * Phase 4 step 3). */

#include "Common.h"
#include "GL.h"
#include "GraphicsEnum.h"
#include "Texture2D.h"

namespace LTE {
  void Renderer_BindIndexBuffer(GL_Buffer buffer, bool force = false);
  void Renderer_BindVertexBuffer(GL_Buffer buffer, bool force = false);

  void Renderer_PushColorBuffer(
    uint index,
    GL_Texture texture,
    uint guid,
    GL_TextureTarget::Enum target = GL_TextureTarget::T2D);
}

/* The GL texture behind a 2D texture, for the renderer to attach. */
GL_Texture Texture2D_GetGLTexture(Texture2DT const& texture);

inline GL_TextureFormat::Enum ToGL(LTE::TextureFormat::Enum format) {
  switch (format) {
  case LTE::TextureFormat::R8:       return GL_TextureFormat::R8;
  case LTE::TextureFormat::RG8:      return GL_TextureFormat::RG8;
  case LTE::TextureFormat::RGBA8:    return GL_TextureFormat::RGBA8;
  case LTE::TextureFormat::R16F:     return GL_TextureFormat::R16F;
  case LTE::TextureFormat::RGBA16F:  return GL_TextureFormat::RGBA16F;
  case LTE::TextureFormat::R32F:     return GL_TextureFormat::R32F;
  case LTE::TextureFormat::RGBA32F:  return GL_TextureFormat::RGBA32F;
  case LTE::TextureFormat::Depth32F: return GL_TextureFormat::DepthComponent32F;
  }
  return GL_TextureFormat::RGBA8;
}

/* The channels of a format's own texel data, as GL takes it. */
inline GL_PixelFormat::Enum ToGLPixelFormat(LTE::TextureFormat::Enum format) {
  switch (format) {
  case LTE::TextureFormat::R8:
  case LTE::TextureFormat::R16F:
  case LTE::TextureFormat::R32F:     return GL_PixelFormat::Red;
  case LTE::TextureFormat::RG8:      return GL_PixelFormat::RG;
  case LTE::TextureFormat::RGBA8:
  case LTE::TextureFormat::RGBA16F:
  case LTE::TextureFormat::RGBA32F:  return GL_PixelFormat::RGBA;
  case LTE::TextureFormat::Depth32F: return GL_PixelFormat::DepthComponent;
  }
  return GL_PixelFormat::RGBA;
}

/* The type of each channel of that data. */
inline GL_DataFormat::Enum ToGLDataFormat(LTE::TextureFormat::Enum format) {
  switch (format) {
  case LTE::TextureFormat::R8:
  case LTE::TextureFormat::RG8:
  case LTE::TextureFormat::RGBA8:    return GL_DataFormat::UnsignedByte;
  case LTE::TextureFormat::R16F:
  case LTE::TextureFormat::RGBA16F:  return GL_DataFormat::Half;
  case LTE::TextureFormat::R32F:
  case LTE::TextureFormat::RGBA32F:
  case LTE::TextureFormat::Depth32F: return GL_DataFormat::Float;
  }
  return GL_DataFormat::UnsignedByte;
}

inline GL_PixelFormat::Enum ToGL(LTE::PixelFormat::Enum format) {
  switch (format) {
  case LTE::PixelFormat::Red:  return GL_PixelFormat::Red;
  case LTE::PixelFormat::RG:   return GL_PixelFormat::RG;
  case LTE::PixelFormat::RGB:  return GL_PixelFormat::RGB;
  case LTE::PixelFormat::RGBA: return GL_PixelFormat::RGBA;
  }
  return GL_PixelFormat::RGBA;
}

inline GL_DataFormat::Enum ToGL(LTE::DataFormat::Enum format) {
  switch (format) {
  case LTE::DataFormat::UnsignedByte: return GL_DataFormat::UnsignedByte;
  case LTE::DataFormat::Half:         return GL_DataFormat::Half;
  case LTE::DataFormat::Float:        return GL_DataFormat::Float;
  }
  return GL_DataFormat::UnsignedByte;
}

inline GL_TextureFilter::Enum ToGL(LTE::TextureFilter::Enum filter) {
  return filter == LTE::TextureFilter::Nearest ? GL_TextureFilter::Nearest : GL_TextureFilter::Linear;
}

inline GL_TextureFilterMip::Enum ToGL(LTE::TextureFilterMip::Enum filter) {
  switch (filter) {
  case LTE::TextureFilterMip::Linear:          return GL_TextureFilterMip::Linear;
  case LTE::TextureFilterMip::LinearMipLinear: return GL_TextureFilterMip::LinearMipLinear;
  case LTE::TextureFilterMip::Nearest:         return GL_TextureFilterMip::Nearest;
  }
  return GL_TextureFilterMip::Linear;
}

inline GL_TextureWrapMode::Enum ToGL(LTE::TextureWrapMode::Enum mode) {
  switch (mode) {
  case LTE::TextureWrapMode::ClampToBorder: return GL_TextureWrapMode::ClampToBorder;
  case LTE::TextureWrapMode::ClampToEdge:   return GL_TextureWrapMode::ClampToEdge;
  case LTE::TextureWrapMode::Repeat:        return GL_TextureWrapMode::Repeat;
  }
  return GL_TextureWrapMode::ClampToEdge;
}

inline GL_IndexFormat::Enum ToGL(LTE::IndexFormat::Enum format) {
  switch (format) {
  case LTE::IndexFormat::Byte:  return GL_IndexFormat::Byte;
  case LTE::IndexFormat::Short: return GL_IndexFormat::Short;
  case LTE::IndexFormat::Int:   return GL_IndexFormat::Int;
  }
  return GL_IndexFormat::Int;
}

#endif
