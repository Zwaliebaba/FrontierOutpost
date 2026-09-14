// MeshPS.hlsl -- six tones, decided per pixel, nothing in between.
//
// THIS IS THE LIGHTING AND IT IS ALL OF IT (ADR-103, ADR-104, ADR-105). The GPU decides WHICH of
// the tones a pixel gets and never invents another: there is no Lambert ramp, no ambient term, no
// product of a colour and a number anywhere in this file, because every face on this screen is one
// of a few colours somebody chose (ADR-002, ADR-012) and a value between them is a colour nobody
// did (ADR-011, ADR-014). What has moved across three ADRs is only WHERE the choice is made -- per
// pixel rather than per face -- and HOW MANY it chooses between, which was two because EGA paired
// its indices and is now FOUR DIFFUSE BANDS plus a silhouette and a glint, because a ball the size
// this framing gives has the pixels to carve (ADR-108).

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
  nointerpolation float4 halfLitColor : COLOR1;
  nointerpolation float4 darkColor : COLOR2;
  nointerpolation float4 rimColor : COLOR3;
  nointerpolation float4 glintColor : COLOR4;
  nointerpolation float4 quarterLitColor : COLOR5;
};

// The two steps down the light. FULL_TERMINATOR ends the band that faces the light squarely;
// GRAZE_TERMINATOR ends the band it only grazes. Neither is zero: at grazing incidence a normal
// turning slowly through a boundary would flicker between tones frame to frame, and a boundary a
// little way in reads as a band rather than as an edge (ADR-002's threshold, kept for its reason).
//
// **A SECOND THRESHOLD ON THE KEY, NOT A SECOND LIGHT.** A fill from the opposite side was modelled
// first and measured badly: it put 7% of the ball in the new band at the opening framing and
// swallowed the shadow entirely a quarter turn round. Two steps down one light put all four bands
// on the ball at every angle (ADR-105).
static const float FULL_TERMINATOR = 0.50;
static const float GRAZE_TERMINATOR = 0.10;

// The third step down, which ends the band just past the terminator and starts the shadow proper
// (ADR-108). It is tested AFTER the rim, so the silhouette keeps exactly the extent ADR-104 tuned
// for it and this splits only what was left: the flat 20% of the ball that was one tone.
//
// **A DIFFUSE band, so it competes for width, which is the bar ADR-105 set.** -0.10 is the value
// that halves that 20% rather than eating it -- at -0.30 the new band took 15% and left the shadow
// a 4% sliver. Measured at four ball sizes on 2026-09-14: the band is 9% of a near ball and 7-8% of
// a far one, 2.2 pixels wide at r=16.5 and 1.3 at r=10.6, and **0.0% of its pixels are isolated at
// any size** -- the same as the grazed band above it, and the number ADR-106's dither failed on.
static const float DUSK_TERMINATOR = -0.10;

// How far round the limb the rim tone reaches. On a sphere a threshold t lights the outer
// sqrt(1 - (1 - t)^2) of the radius, so 0.35 is the outer fifth -- about two pixels on a near ball
// and one on a far one, which is what a rim has to be to read at this size.
static const float RIM_TERMINATOR = 0.35;

// Where the glint starts, on dot(normal, halfway-between-light-and-eye). 0.97 is a cap fourteen
// degrees across, which on a nine-pixel ball is a blob four or five pixels wide.
//
// **A SPOT, NOT A BAND, WHICH IS WHY IT FITS** (ADR-106). Every other tone here is a band, and a
// band on a ball this small is two pixels; a fifth band would have been one. A highlight is a disc
// in the middle of the lit face and it does not compete with the ramp for width -- which is what
// earned it a tone after ADR-105 said the next one would have to.
static const float GLINT_TERMINATOR = 0.97;

float4 main(VertexOut _input) : SV_Target
{
  float3 normal = normalize(_input.normal);

  // THE RIM IS MEASURED FROM THE EYE AND THE BANDS FROM THE LIGHT, and that is the whole difference
  // between them: one turns with the camera and the other does not. A surface facing away from the
  // viewer is at the silhouette whatever the light is doing.
  float3 toEye = normalize(g_eyePosition - _input.worldPosition);
  float toViewer = dot(normal, toEye);
  float facing = dot(normal, g_lightDirection);

  if (facing > FULL_TERMINATOR)
  {
    // The glint sits inside the lit band and nowhere else: a highlight on a surface the light is
    // not finding would be a reflection of nothing. A flat-faced solid passes `lit` here and so
    // never shows one, which is the same opt-out the rim has (`Ink::FlatRampFor`).
    float gloss = dot(normal, normalize(g_lightDirection + toEye));
    return gloss > GLINT_TERMINATOR ? _input.glintColor : _input.litColor;
  }
  if (facing > GRAZE_TERMINATOR)
  {
    return _input.halfLitColor;
  }

  // The rim only ever repaints what the light misses. On the lit side it would be an outline around
  // a shape the light has already described, which is a sticker with a border rather than a solid;
  // on the unlit side it is the light that got past the object, and it is what stops a ball dying
  // into the background where its shadow meets it.
  if (toViewer < 1.0 - RIM_TERMINATOR)
  {
    return _input.rimColor;
  }

  // What the rim did not take of the dark side, in two bands rather than one. A flat-faced solid
  // passes a real tone here like the other diffuse bands -- unlike the rim and the glint, a step
  // down the light is not a curved-surface effect (`Ink::FlatRampFor`).
  if (facing > DUSK_TERMINATOR)
  {
    return _input.quarterLitColor;
  }
  return _input.darkColor;
}
