// FrontierOutpost/src/liblt/Shaders/Math.hlsli
//
// GameData/shader/common/math.jsl, in HLSL.

#ifndef MATH_HLSLI
#define MATH_HLSLI

static const float TAU = 6.28319;

float avg(float2 x) { return (x.x + x.y) / 2.0; }
float avg(float3 x) { return (x.x + x.y + x.z) / 3.0; }
float avg(float4 x) { return (x.x + x.y + x.z + x.w) / 4.0; }

float compress(float t, float p) {
  float m = 2. * abs(t - .5);
  float s = sign(t - .5);
  m = pow(abs(m), p);
  return .5 + .5 * s * m;
}

float contrast(float x, float c) {
  return saturate((x - 0.5) * c + 0.5);
}

float3 contrast(float3 x, float c) {
  return float3(contrast(x.x, c), contrast(x.y, c), contrast(x.z, c));
}

float4 contrast(float4 x, float c) {
  return float4(contrast(x.x, c), contrast(x.y, c), contrast(x.z, c), contrast(x.w, c));
}

float emix(float a, float b, float t) {
  return exp((1.0 - t) * log(a) + t * log(b));
}

float2 emix(float2 a, float2 b, float t) {
  return exp((1.0 - t) * log(a) + t * log(b));
}

float3 emix(float3 a, float3 b, float t) {
  return exp((1.0 - t) * log(a) + t * log(b));
}

float4 emix(float4 a, float4 b, float t) {
  return exp((1.0 - t) * log(a) + t * log(b));
}

float gain(float t, float p) {
  if (t < 0.5)
    return pow(abs(2.0 * t), p) / 2.0;
  else
    return 1.0 - pow(abs(1.0 - 2.0 * (t - 0.5)), p) / 2.0;
}

float mixInv(float a, float b, float value) {
  return (value - a) / (b - a);
}

float3 ortho(float3 v) {
  return lerp(float3(v.z, 0.0, -v.x), float3(v.y, -v.x, 0.0), abs(v.y));
}

float pow2(float x) { return x * x; }
float2 pow2(float2 x) { return x * x; }
float3 pow2(float3 x) { return x * x; }
float4 pow2(float4 x) { return x * x; }

float pow3(float x) { return x * x * x; }
float2 pow3(float2 x) { return x * x * x; }
float3 pow3(float3 x) { return x * x * x; }
float4 pow3(float4 x) { return x * x * x; }

float pow4(float x) { return pow2(pow2(x)); }
float2 pow4(float2 x) { return pow2(pow2(x)); }
float3 pow4(float3 x) { return pow2(pow2(x)); }
float4 pow4(float4 x) { return pow2(pow2(x)); }

float pow8(float x) { return pow2(pow2(pow2(x))); }
float2 pow8(float2 x) { return pow2(pow2(pow2(x))); }
float3 pow8(float3 x) { return pow2(pow2(pow2(x))); }
float4 pow8(float4 x) { return pow2(pow2(pow2(x))); }

float threshold(float f, float t) {
  return saturate((f - t) / (1.0 - t));
}

float cosp(float t) { return 0.5 + 0.5 * cos(t); }
float sinp(float t) { return 0.5 + 0.5 * sin(t); }

#endif
