// MeshPS.hlsl -- three tones, decided per pixel, nothing in between.
//
// THIS IS THE LIGHTING AND IT IS ALL OF IT (ADR-103, ADR-104). The GPU decides WHICH of the tones
// a pixel gets and never invents another: there is no Lambert ramp, no ambient term, no product of
// a colour and a number anywhere in this file, because every face on this screen is one of a few
// colours somebody chose (ADR-002, ADR-012) and a value between them is a colour nobody did
// (ADR-011, ADR-014). What moved from those ADRs is only WHERE the choice is made -- per pixel
// rather than per face -- and HOW MANY it chooses between, which was two because EGA paired its
// indices and is now three because a silhouette is worth a tone of its own.

cbuffer MeshConstants : register(b0)
{
  row_major float4x4 g_viewProjection;
  float3 g_lightDirection;
  float g_lightPadding;
  float3 g_eyePosition;
  float g_eyePadding;
};

struct VertexOut
{
  float4 position : SV_Position;
  float3 normal : NORMAL;
  float3 worldPosition : POSITION;
  nointerpolation float4 litColor : COLOR0;
  nointerpolation float4 darkColor : COLOR1;
  nointerpolation float4 rimColor : COLOR2;
};

// Where the lit side ends. Not zero: at grazing incidence a normal turning slowly through the
// boundary would flicker between the tones frame to frame, and a little way past it the lit side
// reads as a cap rather than a hemisphere, which is what makes a ball read as a ball (ADR-002's
// threshold, kept for its reason).
static const float TERMINATOR = 0.15;

// How far round the limb the rim tone reaches. On a sphere a threshold t lights the outer
// sqrt(1 - (1 - t)^2) of the radius, so 0.35 is the outer fifth -- about two pixels on a near ball
// and one on a far one, which is what a rim has to be to read at this size.
static const float RIM_TERMINATOR = 0.35;

float4 main(VertexOut _input) : SV_Target
{
  float3 normal = normalize(_input.normal);

  // THE RIM IS MEASURED FROM THE EYE AND THE TERMINATOR FROM THE LIGHT, and that is the whole
  // difference between them: one turns with the camera and the other does not. A surface facing
  // away from the viewer is at the silhouette whatever the light is doing.
  float toViewer = dot(normal, normalize(g_eyePosition - _input.worldPosition));
  float facing = dot(normal, g_lightDirection);

  // The rim only ever repaints what the light misses. On the lit side it would be an outline
  // around a shape the light has already described, which is a sticker with a border rather than a
  // solid; on the dark side it is the light that got past the object, and it is what stops a ball
  // dying into the background where its dark half meets it.
  if (facing <= TERMINATOR && toViewer < 1.0 - RIM_TERMINATOR)
  {
    return _input.rimColor;
  }

  return facing > TERMINATOR ? _input.litColor : _input.darkColor;
}
