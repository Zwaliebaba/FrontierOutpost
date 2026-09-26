#include "RendererCore.h"

#include "ProgramLog.h"
#include "Renderer.h"
#include "Window.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>

namespace {
  /* Never destroyed: textures that static objects hold go after every other
     static, and unregister themselves as they go. */
  std::unordered_map<uint64, GpuTexture*>& GetRegistry() {
    static std::unordered_map<uint64, GpuTexture*>* registry =
      new std::unordered_map<uint64, GpuTexture*>;
    return *registry;
  }

  uint64 gNextId = 1;

  struct Core {
    Neuron::GraphicsDevice device;
    bool offscreen;
    uint deviceErrors;
    uint messagesLogged;
    GpuTexture* frame;

    Core() :
      offscreen(false),
      deviceErrors(0),
      messagesLogged(0),
      frame(nullptr)
      {}
  } core;

  /* Enough to see what goes wrong without a log of every frame's repeats. */
  const uint kMaxMessagesLogged = 200;

  std::uint16_t FloatToHalf(float value) {
    std::uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    std::uint32_t const sign = (bits >> 16) & 0x8000u;
    std::uint32_t const exponent = (bits >> 23) & 0xFFu;
    std::uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent == 0xFFu)
      return (std::uint16_t)(sign | 0x7C00u | (mantissa ? 0x200u : 0u));

    int const halfExponent = (int)exponent - 127 + 15;
    if (halfExponent >= 31)
      return (std::uint16_t)(sign | 0x7C00u);

    /* Below the smallest normal half: a subnormal, or 0. */
    if (halfExponent <= 0) {
      if (halfExponent < -10)
        return (std::uint16_t)sign;
      mantissa |= 0x800000u;
      int const shift = 14 - halfExponent;
      std::uint32_t half = mantissa >> shift;
      std::uint32_t const rest = mantissa & ((1u << shift) - 1u);
      std::uint32_t const halfway = 1u << (shift - 1);
      if (rest > halfway || (rest == halfway && (half & 1u)))
        ++half;
      return (std::uint16_t)(sign | half);
    }

    /* To nearest, ties to even; a carry out of the mantissa moves the exponent
       up, into infinity at the top. */
    std::uint32_t half = ((std::uint32_t)halfExponent << 10) | (mantissa >> 13);
    std::uint32_t const rest = mantissa & 0x1FFFu;
    if (rest > 0x1000u || (rest == 0x1000u && (half & 1u)))
      ++half;
    return (std::uint16_t)(sign | half);
  }

  float HalfToFloat(std::uint16_t half) {
    std::uint32_t const sign = (std::uint32_t)(half & 0x8000u) << 16;
    std::uint32_t const exponent = (half >> 10) & 0x1Fu;
    std::uint32_t mantissa = half & 0x3FFu;
    std::uint32_t bits;
    if (exponent == 0) {
      if (mantissa == 0)
        bits = sign;
      else {
        int shifts = -1;
        do {
          ++shifts;
          mantissa <<= 1;
        } while (!(mantissa & 0x400u));
        bits = sign | ((std::uint32_t)(112 - shifts) << 23) | ((mantissa & 0x3FFu) << 13);
      }
    } else if (exponent == 0x1Fu)
      bits = sign | 0x7F800000u | (mantissa << 13);
    else
      bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  uint Channels(LTE::PixelFormat::Enum format) {
    switch (format) {
    case LTE::PixelFormat::Red:  return 1;
    case LTE::PixelFormat::RG:   return 2;
    case LTE::PixelFormat::RGB:  return 3;
    case LTE::PixelFormat::RGBA: return 4;
    }
    return 4;
  }

  uint Channels(LTE::TextureFormat::Enum format) {
    switch (format) {
    case LTE::TextureFormat::R8:
    case LTE::TextureFormat::R16F:
    case LTE::TextureFormat::R32F:
    case LTE::TextureFormat::Depth32F: return 1;
    case LTE::TextureFormat::RG8:      return 2;
    case LTE::TextureFormat::RGBA8:
    case LTE::TextureFormat::RGBA16F:
    case LTE::TextureFormat::RGBA32F:  return 4;
    }
    return 4;
  }

  LTE::DataFormat::Enum ChannelType(LTE::TextureFormat::Enum format) {
    switch (format) {
    case LTE::TextureFormat::R8:
    case LTE::TextureFormat::RG8:
    case LTE::TextureFormat::RGBA8:   return LTE::DataFormat::UnsignedByte;
    case LTE::TextureFormat::R16F:
    case LTE::TextureFormat::RGBA16F: return LTE::DataFormat::Half;
    default:                          return LTE::DataFormat::Float;
    }
  }

  size_t ChannelBytes(LTE::DataFormat::Enum type) {
    switch (type) {
    case LTE::DataFormat::UnsignedByte: return 1;
    case LTE::DataFormat::Half:         return 2;
    case LTE::DataFormat::Float:        return 4;
    }
    return 4;
  }

  float ReadChannel(std::byte const* at, LTE::DataFormat::Enum type) {
    switch (type) {
    case LTE::DataFormat::UnsignedByte:
      return (float)std::to_integer<uint>(*at) / 255.0f;
    case LTE::DataFormat::Half: {
      std::uint16_t half;
      std::memcpy(&half, at, sizeof(half));
      return HalfToFloat(half);
    }
    case LTE::DataFormat::Float: {
      float value;
      std::memcpy(&value, at, sizeof(value));
      return value;
    }
    }
    return 0.0f;
  }

  void WriteChannel(float value, LTE::DataFormat::Enum type, std::byte* at) {
    switch (type) {
    case LTE::DataFormat::UnsignedByte: {
      /* NaN clamps to 0, as the comparisons put it. */
      float const unit = value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
      *at = (std::byte)(uint)std::lround(unit * 255.0f);
      return;
    }
    case LTE::DataFormat::Half: {
      std::uint16_t const half = FloatToHalf(value);
      std::memcpy(at, &half, sizeof(half));
      return;
    }
    case LTE::DataFormat::Float:
      std::memcpy(at, &value, sizeof(value));
      return;
    }
  }

  /* _count texels of _channels channels of _type, from _in to _out, the channels
     a texel lacks read as GL's 0, 0, 0 and 1. */
  void ConvertTexels(
    std::byte const* in, uint inChannels, LTE::DataFormat::Enum inType,
    std::byte* out, uint outChannels, LTE::DataFormat::Enum outType,
    size_t count)
  {
    size_t const inChannelBytes = ChannelBytes(inType);
    size_t const outChannelBytes = ChannelBytes(outType);
    for (size_t texel = 0; texel < count; ++texel) {
      float value[4] = {0.0f, 0.0f, 0.0f, 1.0f};
      for (uint channel = 0; channel < inChannels; ++channel)
        value[channel] =
          ReadChannel(in + (texel * inChannels + channel) * inChannelBytes, inType);
      for (uint channel = 0; channel < outChannels; ++channel)
        WriteChannel(value[channel], outType,
          out + (texel * outChannels + channel) * outChannelBytes);
    }
  }
}

GpuTexture::GpuTexture() :
  id(gNextId++)
{
  sampler.magFilter = Neuron::TextureFilter::Linear;
  sampler.minFilter = Neuron::TextureFilter::Linear;
  sampler.mipFilter = Neuron::MipFilter::Linear;
  sampler.wrapU = Neuron::TextureWrap::Repeat;
  sampler.wrapV = Neuron::TextureWrap::Repeat;
  sampler.wrapW = Neuron::TextureWrap::Repeat;
  sampler.lodBias = 0.0f;
  sampler.minLod = -1000.0f;
  sampler.maxLod = 1000.0f;
  sampler.maxAnisotropy = 1;
  sampler.borderColor = {0.0f, 0.0f, 0.0f, 0.0f};
  GetRegistry()[id] = this;
}

GpuTexture::~GpuTexture() {
  GetRegistry().erase(id);
}

GpuTexture* GpuTexture_Find(uint64 id) {
  std::unordered_map<uint64, GpuTexture*>::iterator it = GetRegistry().find(id);
  return it != GetRegistry().end() ? it->second : nullptr;
}

Neuron::GraphicsDevice& Renderer_Device() {
  if (!core.device)
    Log_Critical("Renderer: the device is used before Renderer_Initialize made it");
  return core.device;
}

Neuron::DrawContext& Renderer_Context() {
  return Renderer_Device().Context();
}

bool Renderer_IsOffscreen() {
  return core.offscreen;
}

void Renderer_TakeDeviceMessages() {
  std::vector<std::string> const messages = core.device.TakeDebugMessages();
  for (size_t i = 0; i < messages.size(); ++i) {
    std::string const& message = messages[i];
    bool const error =
      message.rfind("error", 0) == 0 || message.rfind("corruption", 0) == 0;
    if (error)
      core.deviceErrors++;
    if (core.messagesLogged < kMaxMessagesLogged) {
      core.messagesLogged++;
      String const entry = String("Direct3D 12 ") + message.c_str();
      if (error)
        Log_Error(entry);
      else
        Log_Warning(entry);
      if (core.messagesLogged == kMaxMessagesLogged)
        Log_Warning("Direct3D 12: no more of its messages are logged; errors are still counted");
    }
  }
}

GpuTexture& Renderer_GetFrame() {
  if (!core.frame)
    core.frame = new GpuTexture;
  Window window = Window_Get();
  V2U size = window ? window->GetSize() : V2U(0);
  Neuron::Texture& texture = core.frame->texture;
  bool const unsized = size.x == 0 || size.y == 0;
  if (unsized && texture)
    return *core.frame;
  if (unsized)
    size = V2U(1);
  if (!texture || texture.WidthPixels() != size.x || texture.HeightPixels() != size.y) {
    Neuron::Texture::Desc desc;
    desc.dimension = Neuron::TextureDimension::Texture2D;
    desc.format = Neuron::TextureFormat::Rgba8;
    desc.widthPixels = size.x;
    desc.heightPixels = size.y;
    desc.depthPixels = 1;
    desc.mipLevels = 1;
    desc.name = "liblt frame";
    texture = Renderer_Device().CreateTexture(desc);
  }
  return *core.frame;
}

Neuron::TextureFormat ToNeuron(LTE::TextureFormat::Enum format) {
  switch (format) {
  case LTE::TextureFormat::R8:       return Neuron::TextureFormat::R8;
  case LTE::TextureFormat::RG8:      return Neuron::TextureFormat::Rg8;
  case LTE::TextureFormat::RGBA8:    return Neuron::TextureFormat::Rgba8;
  case LTE::TextureFormat::R16F:     return Neuron::TextureFormat::R16F;
  case LTE::TextureFormat::RGBA16F:  return Neuron::TextureFormat::Rgba16F;
  case LTE::TextureFormat::R32F:     return Neuron::TextureFormat::R32F;
  case LTE::TextureFormat::RGBA32F:  return Neuron::TextureFormat::Rgba32F;
  case LTE::TextureFormat::Depth32F: return Neuron::TextureFormat::Depth32F;
  }
  return Neuron::TextureFormat::Rgba8;
}

uint Renderer_FullMipLevels(uint width, uint height) {
  return Neuron::Texture::FullMipLevels(width, height, 1);
}

void Texels_Convert(
  void const* data,
  LTE::PixelFormat::Enum pixelFormat,
  LTE::DataFormat::Enum dataFormat,
  size_t count,
  LTE::TextureFormat::Enum format,
  std::vector<std::byte>& out)
{
  out.resize(count * LTE::TextureFormat::Size(format));
  ConvertTexels(
    (std::byte const*)data, Channels(pixelFormat), dataFormat,
    out.data(), Channels(format), ChannelType(format),
    count);
}

void Texels_Read(
  std::byte const* texels,
  LTE::TextureFormat::Enum format,
  size_t count,
  LTE::PixelFormat::Enum pixelFormat,
  LTE::DataFormat::Enum dataFormat,
  void* out)
{
  ConvertTexels(
    texels, Channels(format), ChannelType(format),
    (std::byte*)out, Channels(pixelFormat), dataFormat,
    count);
}

namespace LTE {
  void Renderer_Finish() {
    Renderer_Device().WaitIdle();
  }

  void Renderer_InitializeCore(bool warp, bool offscreen) {
    core.offscreen = offscreen;

    Neuron::GraphicsDevice::Desc desc;
    desc.warp = warp;
    /* The smoke mode fails on what the debug layer reports, so it must have it.
       A Debug build asks for it too, and runs without it where Windows' Graphics
       Tools are not installed. */
#ifdef _DEBUG
    desc.debugLayer = true;
#else
    desc.debugLayer = warp;
#endif
    desc.gpuValidation = false;
    /* A failed call or a removed device ends the program (ADR-007), after
       what the debug layer saw of it. */
    desc.onFailure = [](std::string const& message) {
      Renderer_TakeDeviceMessages();
      Log_Critical(String(message.c_str()));
    };

    std::string error;
    if (!Neuron::GraphicsDevice::Create(desc, core.device, error)) {
      if (!desc.debugLayer || warp)
        Log_Critical(String(error.c_str()));
      Log_Warning(String(error.c_str()) + "; running without it");
      desc.debugLayer = false;
      error.clear();
      if (!Neuron::GraphicsDevice::Create(desc, core.device, error))
        Log_Critical(String(error.c_str()));
    }

    Log_Message("Direct3D 12 on " + String(core.device.AdapterName().c_str()) +
      (core.device.IsDebugLayerOn() ? ", with the debug layer" : "") +
      (offscreen ? ", offscreen" : ""));
    core.device.BeginFrame();
  }

  uint Renderer_GetDeviceErrorCount() {
    return core.deviceErrors;
  }
}
