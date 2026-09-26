// FrontierOutpost/src/liblt/Shaders/Field.hlsli
//
// What the SDF field's compute shaders share (Design/ADR/ADR-009): the functions of
// GameData/shader/common/field.jsl that the SDF opcodes use, in HLSL, and the field texture read
// as GL read it. The rest of field.jsl, hull, sandbox, wedge and their helpers, no SDF node
// reached; its field(), which FIELDFN filled in, is GenFieldCS.hlsl's interpreter.

#ifndef FIELD_HLSLI
#define FIELD_HLSLI

float sigmoid(float t) {
  return 1.0 / (1.0 + exp(-t));
}

float boxr(float3 p, float3 c, float3 sides, float radius) {
  return length(max(abs(p - c) - sides * (1.0 - radius), 0.0)) - radius;
}

float cylinder(float3 p, float3 c, float3 axis, float r) {
  float3 to = p - c;
  float proj = dot(axis, to);
  float orthoDist = length(to - axis * proj);
  return orthoDist - r;
}

float intersect(float a, float b, float sharpness) {
  return lerp(a, b, sigmoid((b - a) * sharpness));
}

float shell(float3 p, float3 center, float radius, float thickness) {
  return max(0.0, abs(length(p - center) - radius) - thickness);
}

float torus(float3 p, float3 center, float radius, float thickness) {
  p -= center;
  return length(p - radius * normalize(float3(p.x, 0.0, p.z))) - thickness;
}

// A texel of the field, and the border colour's 1 past its edges, as the field texture's
// AddressBorder(1, 0, 0, 0) gave GL. One result, returned at the end: FXC reads an early return
// as a path that leaves the value unset (X4000).
float FieldTexel(Texture3D<float> source, uint3 size, int3 at) {
  float value = 1.0;
  if (all(at >= 0) && all(at < int3(size)))
    value = source.Load(int4(at, 0));
  return value;
}

// The field at coord, from 0 to 1 across it, filtered as GL's texture3D filtered it: linearly
// between the eight texels about it. Compute is given no samplers (NeuronClient's DrawContext), so
// the filter is written out.
float SampleField(Texture3D<float> source, uint3 size, float3 coord) {
  float3 texel = coord * float3(size) - 0.5;
  float3 lower = floor(texel);
  float3 t = texel - lower;
  int3 at = int3(lower);
  float x00 = lerp(FieldTexel(source, size, at), FieldTexel(source, size, at + int3(1, 0, 0)), t.x);
  float x10 = lerp(FieldTexel(source, size, at + int3(0, 1, 0)), FieldTexel(source, size, at + int3(1, 1, 0)), t.x);
  float x01 = lerp(FieldTexel(source, size, at + int3(0, 0, 1)), FieldTexel(source, size, at + int3(1, 0, 1)), t.x);
  float x11 = lerp(FieldTexel(source, size, at + int3(0, 1, 1)), FieldTexel(source, size, at + int3(1, 1, 1)), t.x);
  return lerp(lerp(x00, x10, t.y), lerp(x01, x11, t.y), t.z);
}

#endif
