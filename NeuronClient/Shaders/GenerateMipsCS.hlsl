// NeuronClient/Shaders/GenerateMipsCS.hlsl
//
// One mip of a 2D texture, or of one face of a cube, made from the mip above it: each texel is the
// average of the 2 by 2 texels above it, and of 3 in a direction where the mip above is odd and this
// is its last column or row, so that no texel of the mip above is left out (plan §5.3). The core
// binds one slice of each mip, so that 2D textures and cube faces are one case.

uint2 sourceSize;      // the mip above's
uint2 destinationSize; // this mip's

Texture2DArray<float4> source : register(t0);
RWTexture2DArray<float4> destination : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (id.x >= destinationSize.x || id.y >= destinationSize.y)
  {
    return;
  }
  const uint2 first = id.xy * 2;
  uint2 last = min(first + 1, sourceSize - 1);
  if (id.x == destinationSize.x - 1 && (sourceSize.x & 1) == 1)
  {
    last.x = sourceSize.x - 1;
  }
  if (id.y == destinationSize.y - 1 && (sourceSize.y & 1) == 1)
  {
    last.y = sourceSize.y - 1;
  }
  float4 sum = 0.0;
  for (uint y = first.y; y <= last.y; ++y)
  {
    for (uint x = first.x; x <= last.x; ++x)
    {
      sum += source.Load(int4(int(x), int(y), 0, 0));
    }
  }
  const uint count = (last.x - first.x + 1) * (last.y - first.y + 1);
  destination[uint3(id.xy, 0)] = sum / (float)count;
}
