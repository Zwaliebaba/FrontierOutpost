// FrontierOutpost/src/liblt/Shaders/Noise.hlsli
//
// GameData/shader/common/noise.jsl, in HLSL.

#ifndef NOISE_HLSLI
#define NOISE_HLSLI

float noise_(float p) {
  return frac(sin(p) * 4137.31315);
}

float noise_(float2 p){
  return noise_(p.x + noise_(p.y));
}

float noise_(float3 p) {
  return noise_(p.x + noise_(p.yz));
}

float noise_(float4 p) {
  return noise_(p.x + noise_(p.yzw));
}

float2 noise2(float t) {
  return frac(sin(t) * float2(4372.137, 8371.377));
}

float3 noise3(float t) {
  return frac(sin(t) * float3(4372.137, 8371.377, 1890.643));
}

float4 noise4(float t) {
  return frac(sin(t) * float4(4372.137, 8371.377, 1890.643, 7777.017));
}

float length2(float2 p) {
  return dot(p, p);
}

float cnoise(float2 p, float s) {
  float2 f = floor(p);
  float d = 1e30;
  for (int xo = -1; xo <= 1; xo++) {
  for (int yo = -1; yo <= 1; yo++) {
    float2 np = f + float2(int(xo), int(yo));
    np += noise2(noise_(float3(np, s)));
    d = min(d, length2(p - np));
  }}
  return sqrt(max(0.0, d));
}

float cnoise(float3 p, float s) {
  float3 f = floor(p);
  float d = 1e30;
  for (int xo = -1; xo <= 1; xo++) {
  for (int yo = -1; yo <= 1; yo++) {
  for (int zo = -1; zo <= 1; zo++) {
    float3 np = f + float3(int(xo), int(yo), int(zo));
    np += noise3(noise_(float4(np, s)));
    d = min(d, dot(p - np, p - np));
  }}}
  return sqrt(d);
}

float fcnoise(float2 p, float s, int octaves, float lac) {
  float a = 0.0, tw = 0.0, w = 1.0;
  for (int i = 0; i < octaves; i++) {
    a += w * cnoise(p, s + float(i));
    tw += w;
    w /= lac;
    p *= 2.0;
  }
  return a / tw;
}

float frcnoise(float2 p, float s, int octaves, float lac) {
  float a = 0.0, tw = 0.0, w = 1.0;
  for (int i = 0; i < octaves; i++) {
    a += w * abs(2.0 * cnoise(p, s) - 1.0);
    tw += w;
    w /= lac;
    p *= 2.0;
  }
  return a / tw;
}

float fcnoise(float3 p, float s, int octaves, float lac) {
  p *= 0.5;
  float a = 0.0, tw = 0.0, w = 1.0;
  for (int i = 0; i < octaves; i++) {
    a += w * cnoise(p, s);
    tw += w;
    w /= lac;
    p *= 2.0;
  }
  return a / tw;
}

float frcnoise(float3 p, float s, int octaves, float lac) {
  float a = 0.0, tw = 0.0, w = 1.0;
  for (int i = 0; i < octaves; i++) {
    a += w * abs(2.0 * cnoise(p, s) - 1.0);
    tw += w;
    w /= lac;
    p *= 2.0;
  }
  return a / tw;
}

float vnoise(float p) {
  float f = floor(p), i = frac(p);
  return lerp(noise_(f), noise_(f + 1.0), i);
}

float vnoise(float2 p) {
  float2 f = floor(p), i = frac(p);
  return lerp(lerp(noise_(f + float2(0.0, 0.0)), noise_(f + float2(1.0, 0.0)), i.x),
             lerp(noise_(f + float2(0.0, 1.0)), noise_(f + float2(1.0, 1.0)), i.x), i.y);
}

float vnoise(float3 p) {
  float3 f = floor(p), i = frac(p);
  return lerp(lerp(lerp(noise_(f + float3(0.0, 0.0, 0.0)), noise_(f + float3(1.0, 0.0, 0.0)), i.x),
                 lerp(noise_(f + float3(0.0, 1.0, 0.0)), noise_(f + float3(1.0, 1.0, 0.0)), i.x), i.y),
             lerp(lerp(noise_(f + float3(0.0, 0.0, 1.0)), noise_(f + float3(1.0, 0.0, 1.0)), i.x),
                 lerp(noise_(f + float3(0.0, 1.0, 1.0)), noise_(f + float3(1.0, 1.0, 1.0)), i.x), i.y), i.z);
}

float snoise(float p) {
  float f = floor(p), i = frac(p);
  return lerp(noise_(f), noise_(f + 1.0), i * i * (3.0 - 2.0*i));
}

float snoise(float2 p) {
  float2 f = floor(p), i = frac(p);
  i = i * i * (3.0 - 2.0*i);
  return lerp(lerp(noise_(f + float2(0.0, 0.0)), noise_(f + float2(1.0, 0.0)), i.x),
             lerp(noise_(f + float2(0.0, 1.0)), noise_(f + float2(1.0, 1.0)), i.x), i.y);
}

float snoise(float3 p) {
  float3 f = floor(p), i = frac(p);
  i = i * i * (3.0 - 2.0*i);
  return lerp(lerp(lerp(noise_(f + float3(0.0, 0.0, 0.0)), noise_(f + float3(1.0, 0.0, 0.0)), i.x),
                 lerp(noise_(f + float3(0.0, 1.0, 0.0)), noise_(f + float3(1.0, 1.0, 0.0)), i.x), i.y),
             lerp(lerp(noise_(f + float3(0.0, 0.0, 1.0)), noise_(f + float3(1.0, 0.0, 1.0)), i.x),
                 lerp(noise_(f + float3(0.0, 1.0, 1.0)), noise_(f + float3(1.0, 1.0, 1.0)), i.x), i.y), i.z);
}

float fsnoise(float p, int octaves, float lac) {
  float a = 0.0, tw = 0.0, w = 1.0;
  for (int i = 0; i < octaves; i++) {
    a += w * snoise(p);
    tw += w;
    p *= 2.0;
    w /= lac;
  }
  return a / tw;
}

float fsnoise(float2 p, int octaves, float lac) {
  float a = 0.0, tw = 0.0, w = 1.0;
  for (int i = 0; i < octaves; i++) {
    a += w * snoise(p);
    tw += w;
    p *= 2.0;
    w /= lac;
  }
  return a / tw;
}

float fsnoise(float3 p, int octaves, float lac) {
  float a = 0.0, tw = 0.0, w = 1.0;
  for (int i = 0; i < octaves; i++) {
    a += w * snoise(p);
    tw += w;
    p *= 2.0;
    w /= lac;
  }
  return a / tw;
}

#endif
