// FrontierOutpost/src/liblt/Shaders/MaterialDebugPS.hlsl
//
// GameData/shader/fragment/material/debug.jsl, in HLSL. It writes two targets.

#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Math.hlsli"

int prepass;

static float3 normal = 0.0;
static float3 position = 0.0;
static float3 vertposscaled = 0.0;

TEXTURE2D(depthBuffer);

float Line(float scale, float x) {
  float d = max(0.0, abs(2.0 * frac(x / scale + 0.5) - 1.0) - 0.05) / scale;
  return
      1.0 * exp(-pow2(4.0 * d))
    + 0.5 * exp(-sqrt(8.0 * d));
}

float Grid(float scale, float3 p, float3 n) {
  float t = 0.0;
  t += pow2(1.0 - abs(n.x)) * Line(scale, p.x);
  t += pow2(1.0 - abs(n.y)) * Line(scale, p.y);
  t += pow2(1.0 - abs(n.z)) * Line(scale, p.z);
  return t;
}

void Shade() {
  PREPASS
  EARLY_Z

  float3 c = ((float3)(0.1));
  float3 n = normalize(normal);
  float g = 0.0;
  float r = 1.0;

  float l = 2.0 * pow(abs(length(position - eye)), 0.75);
  float lg = log2(l);
  float l1 = pow(2.0, (floor(lg) + 0.0));
  float l2 = pow(2.0, (floor(lg) + 1.0));
  float3 vps1 = vertposscaled / l1;
  float3 vps2 = vertposscaled / l2;
  g += lerp(Grid(0.1, vps1, n), Grid(0.1, vps2, n), frac(lg));
  c += 4.0 * g * float3(1.0, 0.3, 0.1);

  float dn = length(normal - normalize(normal));
  // c *= mix(0.2, 1.0, exp(-1024 * dn));
  c *= uv.x;
  c *= sqrt(uv.x);
  c = toLinear(c);

  outputAlbedo(c);
  outputAlpha(1.0);
  outputNormal(n);
  outputMaterial(MATERIAL_NOSHADE);
  outputRoughness(r);
}

TwoTargets main(Varyings input) {
  ReadVaryings(input);
  normal = input.normal;
  position = input.position;
  vertposscaled = input.vertposscaled;
  Shade();
  TwoTargets targets;
  targets.color0 = fragment_color0;
  targets.color1 = fragment_color1;
  return targets;
}
