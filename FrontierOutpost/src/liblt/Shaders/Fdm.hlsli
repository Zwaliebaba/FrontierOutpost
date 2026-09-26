// FrontierOutpost/src/liblt/Shaders/Fdm.hlsli
//
// GameData/shader/common/fdm.jsl, in HLSL. It reads position and eye, which the shader that includes it
// declares.

#ifndef FDM_HLSLI
#define FDM_HLSLI

static const float kDefaultMaxFreq = 0.5;

float getFDMFrequency() {
  return kDefaultMaxFreq / pow(abs(length(position - eye)), 0.75);
}

float4 sampleFDMTexture(TEXTURE2D_PARAM(tex), float2 uv) {
  float frequency = getFDMFrequency();
  float freqHi = pow(2.0, ceil(log2(frequency)));
  float freqLo = freqHi * 0.5;
  return lerp(
    texture2D(tex, uv * freqLo),
    texture2D(tex, uv * freqHi),
    frequency / freqLo - 1.0);
}

float4 sampleFDM(TEXTURE2D_PARAM(tex), float3 pos) {
  float frequency = getFDMFrequency();
  float freqHi = pow(2.0, ceil(log2(frequency)));
  float freqLo = freqHi * 0.5;
  return lerp(
    sampleTriplanar(TEXTURE_ARG(tex), freqLo * pos),
    sampleTriplanar(TEXTURE_ARG(tex), freqHi * pos),
    frequency / freqLo - 1.0);
}

float3 sampleFDMBumpmap(TEXTURE2D_PARAM(tex), float3 pos) {
  float frequency = getFDMFrequency();
  float freqHi = pow(2.0, ceil(log2(frequency)));
  float freqLo = freqHi * 0.5;
  return lerp(
    sampleTriplanarBumpmap(TEXTURE_ARG(tex), freqLo * pos),
    sampleTriplanarBumpmap(TEXTURE_ARG(tex), freqHi * pos),
    frequency / freqLo - 1.0);
}

#endif
