// FrontierOutpost/src/liblt/Shaders/Texturing.hlsli
//
// GameData/shader/common/texturing.jsl, in HLSL: its HIGHQ path, which Shader Model 5.1 always
// takes. Each function takes a texture and its sampler, TEXTURE_ARG(name), where GLSL took a
// sampler2D or samplerCube.

#ifndef TEXTURING_HLSLI
#define TEXTURING_HLSLI

float4 texLod(TEXTURECUBE_PARAM(s), float3 uvw, float lod) {
  return textureCubeLod(s, uvw, lod);
}

float4 texLod(TEXTURE2D_PARAM(s), float2 uv, float lod) {
  return texture2DLod(s, uv, lod);
}

float4 sampleTriplanar(TEXTURE2D_PARAM(s), float3 pos) {
  float3 n = normalize(vertnormal);
  float3 blend = n * n;
  return
    blend.x * texture2D(s, pos.yz) +
    blend.y * texture2D(s, pos.xz) +
    blend.z * texture2D(s, pos.xy);
}

float3 sampleTriplanarBumpmap(TEXTURE2D_PARAM(s), float3 pos) {
  float3 texXY = 2.0 * texture2D(s, pos.xy).xyz - 1.0;
  float3 texXZ = 2.0 * texture2D(s, pos.xz).xyz - 1.0;
  float3 texYZ = 2.0 * texture2D(s, pos.yz).xyz - 1.0;
  float3 n = normalize(vertnormal);
  float3 blend = abs(n);
  float3 tanX = normalize(float3( n.x, -n.z,  n.y));
  float3 tanY = normalize(float3( n.z,  n.y, -n.x));
  float3 tanZ = normalize(float3(-n.y,  n.x,  n.z));

  return blend.z * normalize(texXY.z * n + texXY.x * tanX + texXY.y * tanY) +
         blend.y * normalize(texXZ.z * n + texXZ.x * tanX + texXZ.y * tanZ) +
         blend.x * normalize(texYZ.z * n + texYZ.x * tanY + texYZ.y * tanZ);
}

#endif
