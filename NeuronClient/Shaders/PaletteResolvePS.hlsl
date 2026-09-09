// PaletteResolvePS.hlsl -- the pixel half of the pass that turns palette indices into pixels.
//
// This is the ONLY place in the renderer where a color exists. Every other pass writes an index
// into a 640x400 R8_UINT target; this one reads that index, looks it up, and writes the
// 1280x800 back buffer.
//
// There is no sampler in this shader, and that is the point rather than an economy. Load() takes
// integer texel coordinates and cannot filter, so the bilinear tap that would turn a crisp
// legacy screen into mush is not something a later edit can switch on by accident
// (Design/README.md section 1, AGENTS.md R12).

Texture2D<uint> g_indexTarget : register(t0);

cbuffer ResolveConstants : register(b0)
{
  // Sixteen 0x00RRGGBB entries as four uint4s. An array of scalars in a constant buffer is padded
  // to one float4 per element, which would turn 64 bytes into 256 and stop the palette fitting in
  // the root signature at all.
  uint4 g_palette[4];
  uint g_presentScale;
};

float4 main(float4 _position : SV_Position) : SV_Target
{
  // Integer divide: at scale 2 each 2x2 block of back-buffer pixels reads exactly one index
  // texel, with no boundary case and no rounding to argue about.
  int2 texel = int2(_position.xy) / int(g_presentScale);
  uint index = g_indexTarget.Load(int3(texel, 0));

  uint packed = g_palette[index >> 2][index & 3];
  float3 color = float3((packed >> 16) & 0xFF, (packed >> 8) & 0xFF, packed & 0xFF) / 255.0;

  // The back buffer is R8G8B8A8_UNORM and NOT _SRGB, so these bytes land in the swap chain
  // unchanged: a screenshot of index 1 reads 0000AA exactly.
  return float4(color, 1.0);
}
