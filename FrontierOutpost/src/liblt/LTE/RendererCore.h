#ifndef LTE_RendererCore_h__
#define LTE_RendererCore_h__

/* The renderer's own plumbing on NeuronClient's Direct3D 12 core, which
 * Renderer.cpp, RendererCore.cpp, Shader.cpp, Texture2D.cpp, Texture3D.cpp,
 * CubeMap.cpp, Mesh.cpp and Window.cpp share and nothing else includes
 * (Design/Plan/NeuronClient-migration.md, section 5.4). The public headers
 * speak GraphicsEnum.h; this turns it into NeuronClient's. */

#include "Common.h"
#include "GraphicsEnum.h"

/* NeuronClient's own headers. The angle brackets keep this folder's Program.h
   and Window.h, which share their names with two of NeuronClient's, out of the
   search. */
#include <DrawContext.h>
#include <GraphicsDevice.h>

#include <cstddef>
#include <span>
#include <vector>

/* A texture as programs read it and targets hold it: the GPU's texture, and how
   GL would sample it, which liblt keeps with the texture as GL did (plan
   section 5.5). Programs and the target stacks hold it by its id, which does not
   keep it alive: once it goes, its id finds nothing, as a deleted GL texture
   left its units and attachments empty. */
struct GpuTexture {
  Neuron::Texture texture;
  Neuron::SamplerDesc sampler;
  uint64 id;

  /* Linear, with mips and repeating, as GL's texture defaults were; the
     textures change what they set differently. */
  GpuTexture();
  ~GpuTexture();

  GpuTexture(GpuTexture const&) = delete;
  GpuTexture& operator=(GpuTexture const&) = delete;
};

/* The texture that has _id, or null for one that has gone and for 0. */
GpuTexture* GpuTexture_Find(uint64 id);

/* The device and its context, which Renderer_Initialize makes. */
Neuron::GraphicsDevice& Renderer_Device();
Neuron::DrawContext& Renderer_Context();

/* Whether frames stay offscreen, for the smoke mode: then no swap chain is made. */
bool Renderer_IsOffscreen();

/* Takes what the debug layer stored and logs it, counting its errors. */
void Renderer_TakeDeviceMessages();

/* The texture liblt draws into when no target is pushed, GL's default
   framebuffer: RGBA8, at the size of the window on top of the stack, which
   Window's swap chain shows. Made, and made again, at that size. */
GpuTexture& Renderer_GetFrame();

Neuron::TextureFormat ToNeuron(LTE::TextureFormat::Enum format);

/* Mip levels down to one texel, for textures that take mips. */
uint Renderer_FullMipLevels(uint width, uint height);

/* Converts _count texels of _pixelFormat channels of _dataFormat to _format's own
   texels, as GL converted them on upload: channels the data lacks are 0, 0 and 1,
   floats are clamped to [0, 1] and rounded for an 8-bit format, and a half-float
   format takes floats rounded to the nearest half. */
void Texels_Convert(
  void const* data,
  LTE::PixelFormat::Enum pixelFormat,
  LTE::DataFormat::Enum dataFormat,
  size_t count,
  LTE::TextureFormat::Enum format,
  std::vector<std::byte>& out);

/* The reverse, as glGetTexImage gave it: _count texels of _format as
   _pixelFormat channels of _dataFormat. */
void Texels_Read(
  std::byte const* texels,
  LTE::TextureFormat::Enum format,
  size_t count,
  LTE::PixelFormat::Enum pixelFormat,
  LTE::DataFormat::Enum dataFormat,
  void* out);

/* A mesh's vertices and indices on the GPU, which Renderer.cpp makes when the
   mesh is drawn and makes again when it has changed (MeshT::bufferVersion). */
struct MeshBuffers {
  Neuron::Buffer vertices;
  Neuron::Buffer indices;
  Neuron::IndexFormat indexFormat;
  uint indexCount;
};

namespace LTE {
  /* Makes the device, the start of Renderer_Initialize. */
  void Renderer_InitializeCore(bool warp, bool offscreen);

  /* Pushes layer _layer (a cube's face) of the texture _id as colour target
     _index, over whatever was there, until Renderer_PopColorBuffer. An id of 0
     pushes none. */
  void Renderer_PushColorBuffer(uint index, uint64 id, uint layer = 0);
}

/* The program Shader.cpp made current, with its constants, textures and
   samplers, onto the context: what a GL draw took from the current program.
   A texture that is also one of _targets is not bound, since Direct3D 12
   cannot read what it draws into, and GL's result there was undefined. False
   when no program is current. */
bool Shader_BindActive(
  Neuron::DrawContext& context,
  std::span<Neuron::Texture const* const> targets);

/* The textures behind liblt's, for the shaders that read them and the targets
   that draw into them. */
GpuTexture* Texture2D_GetGpu(Texture2DT const& texture);
GpuTexture* Texture3D_GetGpu(Texture3DT const& texture);
GpuTexture* CubeMap_GetGpu(CubeMapT const& cubeMap);

#endif
