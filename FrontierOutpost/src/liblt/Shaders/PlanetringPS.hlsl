// FrontierOutpost/src/liblt/Shaders/PlanetringPS.hlsl
//
// GameData/shader/fragment/planetring.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"
#include "Lighting.hlsli"
#include "Scattering.hlsli"

static float3 normal = 0.0;
static float3 position = 0.0;
static float3 origin = 0.0;

TEXTURE2D(rings);

void Shade() {
  float r = length(2.0 * (uv - 0.5));
  float d = texture2D(rings, float2(r, 0.5)).x;
  float3 V = eye - position;

  /* Alpha. */
  float alpha = 1.0;
  alpha *= 0.5;
  alpha *= 1.0 - exp(-64.0 * max(0.0, r - 0.5));
  alpha *= d * exp(-64.0 * max(0.0, r - 0.8));
  alpha *= 1.0 - exp(-pow2(1.5 * length(V) / scale.x));
  alpha = saturate(alpha);
  alpha *= 1.0 - getFoginess(length(V));

  float3 c = lerp(float3(0.5, 0.8, 0.9), float3(1.0, 1.0, 1.0), d);

  /* Lighting. */
  float3 L = normalize(starPos);
  float3 n = normalize(normal);
  n *= sign(dot(n, V));
  float l = 10000.0 * cookTorrance(L, position, n, 0.10, 1.0);
  c *= 1.0 + l;

  /* Shadow. */
  float3 toOrigin = (origin - position);
  float3 toStar = normalize(starPos - position);
  float OS = dot(toOrigin, toStar);
  float rshadow = max(0.0, length(toOrigin - toStar * OS) / scale.x - 1.0);
  if (OS > 0.0)
    c *= 1.0 - exp(-32.0 / max(0.0, length(toOrigin) / scale.x - 1.0) * pow2(rshadow));
  alpha *= sqrt(abs(dot(normal, normalize(V))));

  RETURN(float4(c, alpha));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  ReadScatteringVaryings(input);
  normal = input.normal;
  position = input.position;
  origin = input.origin;
  Shade();
  return fragment_color0;
}
