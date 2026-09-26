// FrontierOutpost/src/liblt/Shaders/Common.hlsli
//
// What every liblt shader includes: global.jsl, vert.jsl and frag.jsl in one file (ADR-008). It
// keeps GLSL's names, so that each shader's body reads as its .jsl did.
//
// Varyings. Every vertex shader returns, and every pixel shader takes, the one Varyings struct, so
// that any vertex shader links with any pixel shader, as GL linked them by name. The five that
// vert.jsl and frag.jsl declared for every shader are static globals here: a vertex shader writes
// them and gl_Position, and WriteVaryings returns them; a pixel shader's ReadVaryings fills them and
// gl_FragCoord. A shader declares its other varyings as statics of its own, as its .jsl declared
// them, and copies them itself.
//
// Clip space. WriteVaryings is the one place GL's clip space becomes Direct3D's (ADR-007; plan
// section 5.5): y is negated, and z maps from [-w, w] to [0, w]. Images keep GL's rows, bottom
// first, so gl_FragCoord is SV_Position, with GL's 1 / w.
//
// Textures. GL sampled a texture with its own filter and wrap state, so each texture here has a
// sampler of its own, named for it: TEXTURE2D(image) declares image and imageSampler, and liblt
// binds the sampler its texture's state asks for. texture2D and the other GLSL calls take such a
// texture by name.

#ifndef COMMON_HLSLI
#define COMMON_HLSLI

// GL_EXT_gpu_shader4, which the .jsl files tested for, is always there at Shader Model 5.1.
#define HIGHQ

static const float farPlane = 1.0e6;
static const float nearPlane = 0.05;

static const float kGamma = 2.2;

// pow is undefined below zero in GLSL too; abs keeps FXC from warning that it is.
float toGamma(float t) { return pow(abs(t), 1.0 / kGamma); }
float2 toGamma(float2 t) { return pow(abs(t), 1.0 / kGamma); }
float3 toGamma(float3 t) { return pow(abs(t), 1.0 / kGamma); }
float4 toGamma(float4 t) { return pow(abs(t), 1.0 / kGamma); }
float toLinear(float t) { return pow(abs(t), kGamma); }
float2 toLinear(float2 t) { return pow(abs(t), kGamma); }
float3 toLinear(float3 t) { return pow(abs(t), kGamma); }
float4 toLinear(float4 t) { return pow(abs(t), kGamma); }

// GLSL's mod floors, where HLSL's fmod truncates.
float mod(float x, float y) { return x - (y * floor(x / y)); }
float2 mod(float2 x, float2 y) { return x - (y * floor(x / y)); }
float3 mod(float3 x, float3 y) { return x - (y * floor(x / y)); }
float4 mod(float4 x, float4 y) { return x - (y * floor(x / y)); }
float2 mod(float2 x, float y) { return x - (y * floor(x / y)); }
float3 mod(float3 x, float y) { return x - (y * floor(x / y)); }
float4 mod(float4 x, float y) { return x - (y * floor(x / y)); }

// A texture, and the sampler of its own that GL's texture state becomes.
#define TEXTURE2D(name) Texture2D name; SamplerState name##Sampler
#define TEXTURE3D(name) Texture3D name; SamplerState name##Sampler
#define TEXTURECUBE(name) TextureCube name; SamplerState name##Sampler

// The same, as a parameter of a function that took a sampler2D, and as the argument for it.
#define TEXTURE2D_PARAM(name) Texture2D name, SamplerState name##Sampler
#define TEXTURECUBE_PARAM(name) TextureCube name, SamplerState name##Sampler
#define TEXTURE_ARG(name) name, name##Sampler

// GLSL's texture calls. A call in a loop that runs a varying number of times takes the Lod or Grad
// form, since FXC can compute no derivatives there.
#define texture2D(name, p) name.Sample(name##Sampler, p)
#define texture2DLod(name, p, lod) name.SampleLevel(name##Sampler, p, lod)
#define texture2DGrad(name, p, dx, dy) name.SampleGrad(name##Sampler, p, dx, dy)
#define texture3D(name, p) name.Sample(name##Sampler, p)
#define texture3DLod(name, p, lod) name.SampleLevel(name##Sampler, p, lod)
#define textureCube(name, p) name.Sample(name##Sampler, p)
#define textureCubeLod(name, p, lod) name.SampleLevel(name##Sampler, p, lod)

// Every varying any .jsl declared, in one layout.
struct Varyings
{
  float4 clipPosition : SV_Position;
  float linearDepth : TEXCOORD0;
  float alpha : TEXCOORD1;
  float opacity : TEXCOORD2;
  float opacityMult : TEXCOORD3;
  float2 uv : TEXCOORD4;
  float2 pixcoord : TEXCOORD5;
  float3 position : TEXCOORD6;
  float3 normal : TEXCOORD7;
  float3 vertpos : TEXCOORD8;
  float3 vertnormal : TEXCOORD9;
  float3 vertcolor : TEXCOORD10;
  float3 vertposscaled : TEXCOORD11;
  float3 origin : TEXCOORD12;
  float3 scale : TEXCOORD13;
  float3 attrib : TEXCOORD14;
  float3 worldRayO : TEXCOORD15;
  float3 worldRayD : TEXCOORD16;
  float4 ndcPos : TEXCOORD17;
  float4 attrib1 : TEXCOORD18;
  float4 attrib2 : TEXCOORD19;
  float4 attrib3 : TEXCOORD20;
  float4 attrib4 : TEXCOORD21;
  float4 offset[3] : TEXCOORD22;
};

static float linearDepth = 0.0;
static float2 uv = 0.0;
static float3 vertpos = 0.0;
static float3 vertnormal = 0.0;
static float3 vertcolor = 0.0;

// ----------------------------------------------------------------------------------------------
// Vertex shaders

static float4 gl_Position = 0.0;

#define VS_PROLOGUE                                                                                \
  float4 vp = float4(vertex_position, 1.0);                                                       \
  float4 vn = float4(vertex_normal, 0.0);                                                         \
  uv = vertex_uv;                                                                                 \
  float4 worldPos = mul(WORLD, vp);                                                               \
  float u = vertex_uv.x;                                                                          \
  float v = vertex_uv.y;

float LogDepth(float z, float w)
{
  z /= w;
  return (2.0 * log(max(1.0e-6, z / nearPlane)) / log(farPlane / nearPlane)) - 1.0;
}

// The varyings the vertex shader wrote, with gl_Position in Direct3D's clip space. Every other
// varying is zero until the shader sets it.
Varyings WriteVaryings()
{
  Varyings varyings = (Varyings)0;
  varyings.clipPosition = float4(gl_Position.x, -gl_Position.y, (gl_Position.z + gl_Position.w) * 0.5, gl_Position.w);
  varyings.linearDepth = linearDepth;
  varyings.uv = uv;
  varyings.vertpos = vertpos;
  varyings.vertnormal = vertnormal;
  varyings.vertcolor = vertcolor;
  return varyings;
}

// ----------------------------------------------------------------------------------------------
// Pixel shaders

static float4 gl_FragCoord = 0.0;
static float4 fragment_color0 = 0.0;
static float4 fragment_color1 = 0.0;

// A pixel shader that writes a second target returns both.
struct TwoTargets
{
  float4 color0 : SV_Target0;
  float4 color1 : SV_Target1;
};

void ReadVaryings(Varyings varyings)
{
  gl_FragCoord = float4(varyings.clipPosition.xyz, 1.0 / varyings.clipPosition.w);
  linearDepth = varyings.linearDepth;
  uv = varyings.uv;
  vertpos = varyings.vertpos;
  vertnormal = varyings.vertnormal;
  vertcolor = varyings.vertcolor;
}

// Only in a function that returns nothing: the shading function each pixel shader's body is.
#define RETURN(x)                                                                                  \
  {                                                                                                \
    fragment_color0 = (x);                                                                         \
    return;                                                                                        \
  }

static const int MATERIAL_PHONG = 0;
static const int MATERIAL_COOKT = 1;
static const int MATERIAL_ICE = 2;
static const int MATERIAL_NOSHADE = 3;
static const float kMaterialCount = 4.0;

static const float kPI = 3.1415926536;

float2 encodeNormal(float3 n)
{
  return 0.5 * (float2(atan2(n.y, n.x) / kPI, n.z) + 1.0);
}

float3 decodeNormal(float2 n)
{
  float2 ang = (2.0 * n) - 1.0;
  float2 scth = float2(sin(ang.x * kPI), cos(ang.x * kPI));
  float2 scphi = float2(sqrt(1.0 - (ang.y * ang.y)), ang.y);
  return float3(scth.y * scphi.x, scth.x * scphi.x, scphi.y);
}

void outputAlbedo(float3 a)
{
  fragment_color0.xyz = a;
}

void outputAlpha(float a)
{
  fragment_color0.w = a;
}

void outputNormal(float3 n)
{
  fragment_color1.xy = encodeNormal(n);
}

void outputRoughness(float roughness)
{
  fragment_color1.z = roughness;
}

void outputMaterial(int m)
{
  fragment_color1.w = float(m) / kMaterialCount;
}

// A shader that uses these declares what they read: depthBuffer and rcpFrame, or prepass.
#define EARLY_Z                                                                                    \
  if (linearDepth > texture2D(depthBuffer, gl_FragCoord.xy * rcpFrame).x)                          \
    discard;

#define PREPASS                                                                                    \
  if (prepass == 1)                                                                                \
  {                                                                                                \
    RETURN(float4(linearDepth, linearDepth, linearDepth, linearDepth));                           \
  }

#endif
