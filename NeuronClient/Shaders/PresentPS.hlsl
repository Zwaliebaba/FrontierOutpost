// NeuronClient/Shaders/PresentPS.hlsl
//
// The present pass: the frame's image into the back buffer, one pixel to one pixel, flipped once.
// The image keeps GL's layout, with row 0 at the bottom, where the back buffer's row 0 is the top of
// the window (plan §5.5). Past the image's edges, the window is black.

Texture2D<float4> image : register(t0);

float4 main(float4 position : SV_Position) : SV_Target
{
  uint width;
  uint height;
  image.GetDimensions(width, height);
  const int2 pixel = int2(position.xy);
  return image.Load(int3(pixel.x, int(height) - 1 - pixel.y, 0));
}
