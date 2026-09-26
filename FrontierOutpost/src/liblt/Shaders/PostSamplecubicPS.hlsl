// FrontierOutpost/src/liblt/Shaders/PostSamplecubicPS.hlsl
//
// GameData/shader/fragment/post/samplecubic.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Color.hlsli"

TEXTURE2D(texture_);
float2 frame;

float4 cubic(float v) {
  float4 n = float4(1.0, 2.0, 3.0, 4.0) - v;
  float4 s = n * n * n;
  float x = s.x;
  float y = s.y - 4.0 * s.x;
  float z = s.z - 4.0 * s.y + 6.0 * s.x;
  float w = 6.0 - x - y - z;
  return float4(x, y, z, w);
}

float4 sampleCubic(TEXTURE2D_PARAM(texture_), float2 uv) {
  float2 coord = uv * frame;
  float fx = frac(coord.x);
  float fy = frac(coord.y);
  coord.x -= fx;
  coord.y -= fy;

  float4 xcubic = cubic(fx);
  float4 ycubic = cubic(fy);

  float4 c = float4(coord.x - 0.5, coord.x + 1.5, coord.y - 0.5, coord.y + 1.5);
  float4 s = float4(xcubic.x + xcubic.y, xcubic.z + xcubic.w, ycubic.x + ycubic.y, ycubic.z + ycubic.w);
  float4 offset = c + float4(xcubic.y, xcubic.w, ycubic.y, ycubic.w) / s;

  float4 sample0 = texture2D(texture_, float2(offset.x, offset.z) / frame);
  float4 sample1 = texture2D(texture_, float2(offset.y, offset.z) / frame);
  float4 sample2 = texture2D(texture_, float2(offset.x, offset.w) / frame);
  float4 sample3 = texture2D(texture_, float2(offset.y, offset.w) / frame);

  float sx = s.x / (s.x + s.y);
  float sy = s.z / (s.z + s.w);

  return lerp(
    lerp(sample3, sample2, sx),
    lerp(sample1, sample0, sx), sy);
}

void Shade() {
  RETURN(sampleCubic(TEXTURE_ARG(texture_), uv));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
