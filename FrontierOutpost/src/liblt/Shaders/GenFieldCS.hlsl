// FrontierOutpost/src/liblt/Shaders/GenFieldCS.hlsl
//
// GameData/shader/fragment/gen/field.jsl, as the compute shader that interprets an SDF
// (Design/ADR/ADR-009). LTE/SDFMesh.cpp encodes the SDF as postfix instructions (LTE/SDF.h); each
// thread walks them for its voxel, with a stack of values and a stack of points, and writes the
// field's value there. Voxel i of n lies i / (n - 1) of the way across the field, where field.jsl
// put its texel.

#include "Math.hlsli"
#include "Noise.hlsli"
#include "Field.hlsli"

// LTE/SDFMesh.cpp refuses an SDF that does not fit these.
#define MAX_INSTRUCTIONS 64
#define MAX_VALUES 16
#define MAX_POINTS 8

// LTE/SDF.h's SDFOp.
#define OP_SPHERE 1
#define OP_ROUND_BOX 2
#define OP_CYLINDER 3
#define OP_SHELL 4
#define OP_TORUS 5
#define OP_FRACTAL_WORLEY 6
#define OP_RADIAL 7
#define OP_SUBTRACT 8
#define OP_SUBTRACT_HARD 9
#define OP_SCALE_BEGIN 10
#define OP_SCALE_END 11

// Every thread reads the same instruction at the same time, which is what a constant buffer serves
// best. Two float4 an instruction: the opcode and parameters 0 to 2, then parameters 3 to 6.
float4 instructions[2 * MAX_INSTRUCTIONS];
uint instructionCount;
float3 origin;
float3 extent;
uint3 resolution;
uint firstSlice;

RWTexture3D<float> field : register(u0);

float Evaluate(float3 p) {
  float values[MAX_VALUES];
  float3 points[MAX_POINTS];
  [unroll] for (uint v = 0; v < MAX_VALUES; ++v)
    values[v] = 1.0;
  [unroll] for (uint s = 0; s < MAX_POINTS; ++s)
    points[s] = p;
  uint top = 0;
  uint saved = 0;
  float3 q = p;

  [loop] for (uint i = 0; i < instructionCount; ++i) {
    float4 a = instructions[2 * i];
    float4 b = instructions[2 * i + 1];
    uint op = (uint)a.x;
    if (op == OP_SCALE_BEGIN) {
      points[saved++] = q;
      q /= a.yzw;
    } else if (op == OP_SCALE_END) {
      q = points[--saved];
    } else if (op == OP_RADIAL) {
      values[top - 1] = length(q) - lerp(a.y, a.z, values[top - 1]);
    } else if (op == OP_SUBTRACT || op == OP_SUBTRACT_HARD) {
      float second = values[--top];
      float first = values[top - 1];
      values[top - 1] = op == OP_SUBTRACT ? intersect(first, -second, a.y) : max(first, -second);
    } else {
      float value = 1.0;
      [branch] switch (op) {
      case OP_SPHERE:
        value = length(q - a.yzw) - b.x;
        break;
      case OP_ROUND_BOX:
        value = boxr(q, a.yzw, b.xyz, b.w);
        break;
      case OP_CYLINDER:
        value = cylinder(q, a.yzw, b.xyz, b.w);
        break;
      case OP_SHELL:
        value = shell(q, a.yzw, b.x, b.y);
        break;
      case OP_TORUS:
        value = torus(q, a.yzw, b.x, b.y);
        break;
      case OP_FRACTAL_WORLEY:
        value = fcnoise(q, a.y, (int)a.z, a.w);
        break;
      }
      values[top++] = value;
    }
  }
  return values[0];
}

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID) {
  uint3 voxel = uint3(id.xy, id.z + firstSlice);
  if (any(voxel >= resolution))
    return;
  field[voxel] = Evaluate(origin + extent * (float3(voxel) / float3(resolution - 1)));
}
