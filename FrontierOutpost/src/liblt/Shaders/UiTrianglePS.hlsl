// FrontierOutpost/src/liblt/Shaders/UiTrianglePS.hlsl
//
// GameData/shader/fragment/ui/triangle.jsl, in HLSL.

#include "Common.hlsli"
#include "Ui.hlsli"

static float4 attrib1 = 0.0;
static float4 attrib2 = 0.0;
static float4 attrib3 = 0.0;
static float3 position = 0.0;

static const float kRadius = 1.5;

void Shade() {
  float2 p1 = attrib1.xy;
  float2 p2 = attrib1.zw;
  float4 color = attrib2;
  float2 uv = attrib3.xy;
  float2 p3 = attrib3.zw;
  float2 p = position.xy;

  float2 center = (p1 + p2 + p3) / 3.0;
  p1 += kRadius * normalize(center - p1);
  p2 += kRadius * normalize(center - p2);
  p3 += kRadius * normalize(center - p3);

  float2 d1 = normalize(p2 - p1);
  float2 d2 = normalize(p3 - p2);
  float2 d3 = normalize(p1 - p3);
  float ed1 = length(p - (p1 + d1 * clamp(dot(d1, p - p1), 0.0, length(p2 - p1))));
  float ed2 = length(p - (p2 + d2 * clamp(dot(d2, p - p2), 0.0, length(p3 - p2))));
  float ed3 = length(p - (p3 + d3 * clamp(dot(d3, p - p3), 0.0, length(p1 - p3))));
  float edm = min(ed1, min(ed2, ed3));

  float2 n1 = float2(-d1.y, d1.x);
  float2 n2 = float2(-d2.y, d2.x);
  float2 n3 = float2(-d3.y, d3.x);
  n1 *= sign(dot(n1, p3 - p1));
  n2 *= sign(dot(n2, p1 - p2));
  n3 *= sign(dot(n3, p2 - p3));
  float dist1 = -dot(n1, p - p1);
  float dist2 = -dot(n2, p - p2);
  float dist3 = -dot(n3, p - p3);
  float dist = max(dist1, max(dist2, dist3));
  float idm = step(0.0, dist) * edm;

  float2 uvp = (uv * frame - 0.5) / (frame - 1.0);
  uvp = (2.0 * uvp - 1.0);

  // float dist = length(max(vec2(0.0), size * abs(uvp) - (realSize - r))) - r;
  float mult = 0.0;
  float fill = exp(-1.5 * max(0.0, idm - kRadius));
  float outline = exp(-1.5 * max(0.0, abs(idm - kRadius) - 0.50));
  float glow = exp(-pow(abs(0.25 * max(0.0, edm - kRadius)), 0.75));
  mult += 1.0 * outline;
  mult += 0.2 * glow;
  RETURN(color * mult);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  attrib1 = input.attrib1;
  attrib2 = input.attrib2;
  attrib3 = input.attrib3;
  position = input.position;
  Shade();
  return fragment_color0;
}
