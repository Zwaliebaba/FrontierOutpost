// StarfieldPS.hlsl -- the backdrop, computed rather than stored (ADR-010).
//
// There is no star texture and no list of star positions. For each pixel of the 1280x720 back
// buffer this hashes the pixel's position in a layer's own coordinates and lights it if the hash
// comes up short. Space is unbounded (MVP-01 section 2), so a stored starfield would either run
// out or have to repeat; a hash never runs out and costs nothing to store, which is also the only
// answer compatible with R13's "the executable ships alone".
//
// The densities and the colors arrive as root constants rather than sitting in this file as
// literals. That is ADR-011's doing: a layer's brightness used to be a palette index, which is a
// small number a shader can reasonably name, and it is now a color -- and Color.h is where this
// game's colors are named.
//
// WHY THIS DOES NOT SHIMMER. The offsets below are whole numbers of pixels, computed on the CPU
// from a camera position that is itself snapped to whole pixels (ADR-003). A star is therefore
// the same hash of the same integer cell from one frame to the next, and either lit or not with
// nothing in between. Scroll the backdrop by a fraction of a pixel and every star resamples every
// frame, which is the shimmer ADR-003 predicted when it decided the camera would snap.

cbuffer StarfieldConstants : register(b0)
{
  // Per layer: xy is the scroll offset in pixels, z is the density mask, w is the packed color.
  int4 g_layers[3];
  // x is the packed color of empty space; yzw are padding, because a constant buffer pads an
  // array element to sixteen bytes whatever is in it.
  int4 g_space;
};

static const uint LAYER_COUNT = 3;

/// A 2D integer hash. Any decent avalanche will do -- what matters is that it is a pure function
/// of the cell, so the same star is in the same place forever.
uint Hash(uint2 _cell)
{
  uint hash = _cell.x * 374761393u + _cell.y * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177u;
  return hash ^ (hash >> 16);
}

/// The four bytes Neuron::Pack wrote, back into a color. Red in the LOW byte, matching
/// DXGI_FORMAT_R8G8B8A8_UNORM and therefore Color.h.
float4 Unpack(uint _packed)
{
  return float4(_packed & 0xFF, (_packed >> 8) & 0xFF, (_packed >> 16) & 0xFF, (_packed >> 24) & 0xFF) / 255.0;
}

float4 main(float4 _position : SV_Position) : SV_Target
{
  // SV_Position is in back-buffer pixels, and this pass runs with the full 1280x720 viewport. One
  // star is therefore exactly one pixel.
  int2 pixel = int2(_position.xy);

  // The layers are walked near-last so that a near star wins the pixel.
  uint color = uint(g_space.x);
  for (uint layer = 0; layer < LAYER_COUNT; ++layer)
  {
    uint2 cell = uint2(pixel + g_layers[layer].xy);
    if ((Hash(cell) & uint(g_layers[layer].z)) == 0)
    {
      color = uint(g_layers[layer].w);
    }
  }

  return Unpack(color);
}
