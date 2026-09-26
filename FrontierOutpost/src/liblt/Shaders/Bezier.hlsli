// FrontierOutpost/src/liblt/Shaders/Bezier.hlsli
//
// GameData/shader/common/bezier.jsl, in HLSL.

#ifndef BEZIER_HLSLI
#define BEZIER_HLSLI

float bezier(float t, float p1, float p2, float p3) {
  float p12 = lerp(p1, p2, t);
  float p23 = lerp(p2, p3, t);
  return lerp(p12, p23, t);
}

float bezier(float t, float p1, float p2, float p3, float p4) {
  float p12 = lerp(p1, p2, t);
  float p23 = lerp(p2, p3, t);
  float p34 = lerp(p3, p4, t);
  float p123 = lerp(p12, p23, t);
  float p234 = lerp(p23, p34, t);
  return lerp(p123, p234, t);
}

float bezier(float t, float p1, float p2, float p3, float p4, float p5) {
  float p12 = lerp(p1, p2, t);
  float p23 = lerp(p2, p3, t);
  float p34 = lerp(p3, p4, t);
  float p45 = lerp(p4, p5, t);
  float p123 = lerp(p12, p23, t);
  float p234 = lerp(p23, p34, t);
  float p345 = lerp(p34, p45, t);
  float p1234 = lerp(p123, p234, t);
  float p2345 = lerp(p234, p345, t);
  return lerp(p1234, p2345, t);
}

#endif
