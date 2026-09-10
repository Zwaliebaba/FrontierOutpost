// StarfieldPS.hlsl -- the backdrop, computed rather than stored (ADR-010).
//
// There is no star texture and no list of star positions. For each texel of the 640x400 index
// target this hashes the texel's position in a layer's own coordinates and lights it if the hash
// comes up short. Space is unbounded (MVP-01 section 2), so a stored starfield would either run
// out or have to repeat; a hash never runs out and costs nothing to store, which is also the only
// answer compatible with R13's "the executable ships alone".
//
// WHY THIS DOES NOT SHIMMER. The offsets below are whole numbers of texels, computed on the CPU
// from a camera position that is itself snapped to whole pixels (ADR-003). A star is therefore
// the same hash of the same integer cell from one frame to the next, and either lit or not with
// nothing in between. Scroll the backdrop by a fraction of a pixel and every star resamples every
// frame, which is the shimmer ADR-003 predicted when it decided the camera would snap.

cbuffer StarfieldConstants : register(b0)
{
  // xy is the layer's scroll offset in texels; zw is padding, because a constant buffer pads an
  // array element to sixteen bytes whatever is in it.
  int4 g_layerOffsets[3];
};

static const uint LAYER_COUNT = 3;

// One texel in (mask + 1) is a star. Powers of two less one, so the test is an AND rather than a
// modulo. Sparser as the layers come nearer, which is what makes the near ones read as closer
// rather than as noise.
static const uint LAYER_MASK[3] = {2047, 4095, 8191};

// Dim far, bright near. All three are grays: a colored star competes with the ship, and the ship
// is the thing the eye has to find.
static const uint LAYER_PALETTE_INDEX[3] = {8, 7, 15};

/// A 2D integer hash. Any decent avalanche will do -- what matters is that it is a pure function
/// of the cell, so the same star is in the same place forever.
uint Hash(uint2 _cell)
{
  uint hash = _cell.x * 374761393u + _cell.y * 668265263u;
  hash = (hash ^ (hash >> 13)) * 1274126177u;
  return hash ^ (hash >> 16);
}

uint main(float4 _position : SV_Position) : SV_Target
{
  // SV_Position is in index-target texels, because this pass runs with the 640x400 viewport. One
  // star is therefore one virtual pixel, which at present scale 2 is an exact 2x2 block.
  int2 texel = int2(_position.xy);

  // Index 0 is empty space. The layers are walked near-last so that a near star wins the texel.
  uint index = 0;
  for (uint layer = 0; layer < LAYER_COUNT; ++layer)
  {
    uint2 cell = uint2(texel + g_layerOffsets[layer].xy);
    if ((Hash(cell) & LAYER_MASK[layer]) == 0)
    {
      index = LAYER_PALETTE_INDEX[layer];
    }
  }

  return index;
}
