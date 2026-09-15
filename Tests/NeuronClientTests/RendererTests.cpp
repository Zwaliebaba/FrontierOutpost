// RendererTests.cpp -- the two recorders: shapes tessellated on the CPU, and the world-space solids
// (ADR-014, ADR-041).

#include "pch.h"
#include "CppUnitTest.h"

#include <algorithm>
#include <array>
#include <cmath>

// NeuronClient.h, not NeuronCore.h: it is the umbrella the library's own translation units
// compile against, so a suite that includes anything else is testing a header in a configuration
// nothing else ever builds it in.
#include "NeuronClient.h"

#include "Color.h"
#include "FontRenderer.h"
#include "MeshRenderer.h"
#include "OrbitCamera.h"
#include "PointerInput.h"
#include "Presentation.h"
#include "SceneTarget.h"
#include "ShapeRenderer.h"
#include "Starfield.h"
#include "TextField.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

// The interface renderer tessellates on the CPU, so how many segments a circle gets is a real
// decision: too few facets a node, too many spends the frame's vertex budget on the star field.
TEST_CLASS(ShapeRendererTests)
{
public:
  TEST_METHOD(SegmentCountsAreClampedAndRiseWithRadius)
  {
    Assert::AreEqual(12u, Neuron::ShapeRenderer::SegmentsForRadius(0.7F), L"a star does not need more than the floor");
    Assert::AreEqual(12u, Neuron::ShapeRenderer::SegmentsForRadius(5.0F));
    Assert::AreEqual(40u, Neuron::ShapeRenderer::SegmentsForRadius(20.0F));
    Assert::AreEqual(64u, Neuron::ShapeRenderer::SegmentsForRadius(1000.0F), L"and never more than the ceiling");
  }

  // A layer boundary is what lets the mesh pass be drawn between two of one page's shape layers
  // (ADR-103): the take stops at it, the next take carries on, and a page that never marks one is
  // drained exactly as before.
  TEST_METHOD(ALayerBoundaryStopsTheNextTake)
  {
    Neuron::ShapeRenderer shapes;
    shapes.BeginFrame();

    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE); // 6 vertices
    shapes.EndLayer();
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE);
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE); // 12 more

    const Neuron::ShapeRenderer::Batch ground = shapes.TakeUnflushed();
    Assert::AreEqual(static_cast<std::size_t>(6), ground.vertices.size(), L"the first take stops at the boundary");
    Assert::AreEqual(0u, ground.firstVertex);

    const Neuron::ShapeRenderer::Batch over = shapes.TakeUnflushed();
    Assert::AreEqual(static_cast<std::size_t>(12), over.vertices.size(), L"the second take is the rest");
    Assert::AreEqual(6u, over.firstVertex);

    Assert::IsTrue(shapes.TakeUnflushed().vertices.empty(), L"and then nothing is new");
  }

  TEST_METHOD(AnEmptyLayerStillCountsAsATake)
  {
    Neuron::ShapeRenderer shapes;
    shapes.BeginFrame();

    shapes.EndLayer();
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE);

    Assert::IsTrue(shapes.TakeUnflushed().vertices.empty(), L"an empty layer is an empty take, not a skipped one");
    Assert::AreEqual(static_cast<std::size_t>(6), shapes.TakeUnflushed().vertices.size(), L"so the overlay lands on the take after it");
  }

  // A stroked polygon is CLOSED, and that is the whole of what it adds over a run of `Line` calls:
  // the edge somebody forgets is the last one, and a hexagon with a gap in it reads as a rendering
  // fault rather than as a missing statement (ADR-107).
  TEST_METHOD(AStrokedPolygonJoinsItsLastCornerBackToItsFirst)
  {
    Neuron::ShapeRenderer shapes;
    shapes.BeginFrame();

    // A line is a quad, so four corners drawn open would be three of them.
    const std::array<Neuron::ShapeRenderer::ShapePoint, 4> diamond = {
      Neuron::ShapeRenderer::ShapePoint{11.0F, 2.0F}, Neuron::ShapeRenderer::ShapePoint{20.0F, 11.0F},
      Neuron::ShapeRenderer::ShapePoint{11.0F, 20.0F}, Neuron::ShapeRenderer::ShapePoint{2.0F, 11.0F}};
    shapes.StrokePolygon(diamond, Neuron::WHITE, 1.5F);
    Assert::AreEqual(static_cast<std::size_t>(24), shapes.TakeUnflushed().vertices.size(), L"a four-corner polygon is not four edges");
  }

  TEST_METHOD(APolygonOfFewerThanTwoCornersDrawsNothing)
  {
    Neuron::ShapeRenderer shapes;
    shapes.BeginFrame();

    const std::array<Neuron::ShapeRenderer::ShapePoint, 1> alone = {Neuron::ShapeRenderer::ShapePoint{4.0F, 4.0F}};
    shapes.StrokePolygon(alone, Neuron::WHITE);
    shapes.StrokePolygon({}, Neuron::WHITE);
    Assert::IsTrue(shapes.TakeUnflushed().vertices.empty(), L"a polygon with no edges recorded geometry");
  }

  TEST_METHOD(BeginFrameForgetsTheBoundaries)
  {
    Neuron::ShapeRenderer shapes;
    shapes.BeginFrame();
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE);
    shapes.EndLayer();

    shapes.BeginFrame();
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE);
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE);
    Assert::AreEqual(static_cast<std::size_t>(12), shapes.TakeUnflushed().vertices.size(), L"a boundary from the last frame is gone");
  }
};

// The mesh recorder: world-space solids the shape recorder is not (ADR-103). What is pinned here
// is the one thing a screenshot cannot say and the culler would silently hide: that every face is
// wound counter-clockwise seen from outside, with a normal pointing the same way.
TEST_CLASS(MeshRendererTests)
{
public:
  using MeshVertex = Neuron::MeshRenderer::MeshVertex;

  /// Six tones a test can tell apart by value, in ramp order: shadow, past the terminator, grazed,
  /// lit, silhouette, glint. The mid grey is a literal because the palette has no name between
  /// `DARK_GRAY` and `LIGHT_GRAY`, and what this needs is six DISTINCT values rather than six
  /// meaningful ones.
  static constexpr Neuron::ColorRamp TONES = {Neuron::DARK_GRAY,   Neuron::Color{0x77, 0x77, 0x77, Neuron::OPAQUE_ALPHA},
                                              Neuron::LIGHT_GRAY,  Neuron::WHITE,
                                              Neuron::BRIGHT_CYAN, Neuron::BRIGHT_MAGENTA};

  /// (b - a) x (c - a), the direction a counter-clockwise triangle faces.
  static std::array<float, 3> FaceDirection(const MeshVertex& _a, const MeshVertex& _b, const MeshVertex& _c)
  {
    const float ux = _b.x - _a.x;
    const float uy = _b.y - _a.y;
    const float uz = _b.z - _a.z;
    const float vx = _c.x - _a.x;
    const float vy = _c.y - _a.y;
    const float vz = _c.z - _a.z;
    return {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
  }

  TEST_METHOD(SegmentCountsAreClampedAndRiseWithRadius)
  {
    Assert::AreEqual(8u, Neuron::MeshRenderer::SegmentsForRadius(1.0F), L"a far ball gets the floor");
    Assert::AreEqual(10u, Neuron::MeshRenderer::SegmentsForRadius(5.0F));
    Assert::AreEqual(12u, Neuron::MeshRenderer::SegmentsForRadius(6.0F));
    Assert::AreEqual(12u, Neuron::MeshRenderer::SegmentsForRadius(100.0F), L"and never more than the ceiling");
    Assert::AreEqual(8u, Neuron::MeshRenderer::RingsForSegments(12), L"a twelve-segment ball is twelve by eight");
  }

  TEST_METHOD(ASphereIsWoundOutwardWithOutwardNormals)
  {
    Neuron::MeshRenderer meshes;
    meshes.BeginFrame();
    meshes.Sphere({10.0F, 20.0F, -30.0F}, 5.0F, TONES, 12);

    const std::span<const MeshVertex> vertices = meshes.Vertices();
    Assert::AreEqual(static_cast<std::size_t>(Neuron::MeshRenderer::SphereVertexCount(12)), vertices.size());
    Assert::AreEqual(static_cast<std::size_t>(504), vertices.size(), L"twelve by eight: two polar bands of triangles, six of quads");

    for (std::size_t index = 0; index < vertices.size(); index += 3)
    {
      const MeshVertex& a = vertices[index];
      const MeshVertex& b = vertices[index + 1];
      const MeshVertex& c = vertices[index + 2];

      const std::array<float, 3> facing = FaceDirection(a, b, c);
      const float outX = (a.x + b.x + c.x) / 3.0F - 10.0F;
      const float outY = (a.y + b.y + c.y) / 3.0F - 20.0F;
      const float outZ = (a.z + b.z + c.z) / 3.0F + 30.0F;
      Assert::IsTrue(facing[0] * outX + facing[1] * outY + facing[2] * outZ > 0.0F, L"a face is counter-clockwise seen from outside");

      for (const MeshVertex& vertex : {a, b, c})
      {
        const float length = std::sqrt(vertex.nx * vertex.nx + vertex.ny * vertex.ny + vertex.nz * vertex.nz);
        Assert::AreEqual(1.0F, length, 0.001F, L"a normal is unit length");
        const float along = vertex.nx * (vertex.x - 10.0F) + vertex.ny * (vertex.y - 20.0F) + vertex.nz * (vertex.z + 30.0F);
        Assert::AreEqual(5.0F, along, 0.001F, L"and points from the centre through its vertex");
        Assert::AreEqual(Neuron::Pack(Neuron::WHITE), vertex.litColor);
        Assert::AreEqual(Neuron::Pack(Neuron::LIGHT_GRAY), vertex.halfLitColor, L"the grazed band's tone did not reach the vertex");
        Assert::AreEqual(Neuron::Pack(Neuron::DARK_GRAY), vertex.darkColor);
        Assert::AreEqual(Neuron::Pack(Neuron::BRIGHT_CYAN), vertex.rimColor, L"the silhouette's tone did not reach the vertex");
        Assert::AreEqual(Neuron::Pack(Neuron::BRIGHT_MAGENTA), vertex.glintColor, L"the glint's tone did not reach the vertex");
      }
    }
  }

  TEST_METHOD(AnOctahedronHasEightFlatFacesFacingOutward)
  {
    Neuron::MeshRenderer meshes;
    meshes.BeginFrame();
    meshes.Octahedron({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, 3.0F, 3.0F, 5.0F, TONES);

    const std::span<const MeshVertex> vertices = meshes.Vertices();
    Assert::AreEqual(static_cast<std::size_t>(24), vertices.size(), L"eight faces of three");

    for (std::size_t index = 0; index < vertices.size(); index += 3)
    {
      const MeshVertex& a = vertices[index];
      const MeshVertex& b = vertices[index + 1];
      const MeshVertex& c = vertices[index + 2];
      const std::array<float, 3> facing = FaceDirection(a, b, c);
      Assert::IsTrue(facing[0] * a.nx + facing[1] * a.ny + facing[2] * a.nz > 0.0F, L"the face's normal is the way it winds");
      Assert::IsTrue(a.nx == b.nx && a.ny == b.ny && a.nz == b.nz && a.nx == c.nx && a.ny == c.ny && a.nz == c.nz,
                     L"a flat face carries one normal on all three corners");
      const float outward = facing[0] * (a.x + b.x + c.x) + facing[1] * (a.y + b.y + c.y) + facing[2] * (a.z + b.z + c.z);
      Assert::IsTrue(outward > 0.0F, L"and it faces away from the centre");
    }
  }

  // A stem is a column now (ADR-105). Its four sides have to face outward, or the culler removes
  // the ones the camera can see and leaves the ones it cannot.
  // A fleet marker has to say which way the fleet is going (ADR-106), so the solid that replaced the
  // flat arrowhead has to be longer along its heading than across it -- and along the heading it was
  // GIVEN, not along an axis.
  TEST_METHOD(AnOctahedronIsLongestAlongTheWayItPoints)
  {
    Neuron::MeshRenderer meshes;
    meshes.BeginFrame();
    // A heading at forty-five degrees, so an implementation that quietly used an axis fails.
    meshes.Octahedron({0.0F, 0.0F, 0.0F}, {0.7071F, 0.0F, 0.7071F}, 10.0F, 2.0F, 2.0F, TONES);

    float alongMost = 0.0F;
    float acrossMost = 0.0F;
    for (const MeshVertex& vertex : meshes.Vertices())
    {
      // The heading and the axis across it, both unit.
      alongMost = std::max(alongMost, std::abs(vertex.x * 0.7071F + vertex.z * 0.7071F));
      acrossMost = std::max(acrossMost, std::abs(vertex.x * -0.7071F + vertex.z * 0.7071F));
    }

    Assert::AreEqual(10.0F, alongMost, 0.01F, L"the dart does not reach its length along its heading");
    Assert::AreEqual(2.0F, acrossMost, 0.01F, L"the dart is not its width across its heading");
  }

  TEST_METHOD(AColumnStandsOnItsFootWithOutwardFaces)
  {
    Neuron::MeshRenderer meshes;
    meshes.BeginFrame();
    meshes.Column({40.0F, 0.0F, -15.0F}, 30.0F, 1.5F, TONES);

    const std::span<const MeshVertex> vertices = meshes.Vertices();
    Assert::AreEqual(static_cast<std::size_t>(Neuron::MeshRenderer::COLUMN_VERTEX_COUNT), vertices.size(), L"four sides and a cap");

    float lowest = 1000.0F;
    float highest = -1000.0F;
    for (std::size_t index = 0; index < vertices.size(); index += 3)
    {
      const MeshVertex& a = vertices[index];
      const std::array<float, 3> facing = FaceDirection(a, vertices[index + 1], vertices[index + 2]);
      Assert::IsTrue(facing[0] * a.nx + facing[1] * a.ny + facing[2] * a.nz > 0.0F, L"a face's normal is the way it winds");

      // Away from the column's own axis, or straight up for the cap. A side that faced inward
      // would be culled exactly when it should be drawn.
      const float outX = (a.x + vertices[index + 1].x + vertices[index + 2].x) / 3.0F - 40.0F;
      const float outZ = (a.z + vertices[index + 1].z + vertices[index + 2].z) / 3.0F + 15.0F;
      Assert::IsTrue(a.ny > 0.9F || a.nx * outX + a.nz * outZ > 0.0F, L"a side faces away from the axis");

      for (const MeshVertex& vertex : {a, vertices[index + 1], vertices[index + 2]})
      {
        lowest = std::min(lowest, vertex.y);
        highest = std::max(highest, vertex.y);
      }
    }

    Assert::AreEqual(0.0F, lowest, 0.0001F, L"a column starts on the plane");
    Assert::AreEqual(30.0F, highest, 0.0001F, L"and reaches exactly its height");
  }

  TEST_METHOD(TakingHandsOverOnlyWhatIsNew)
  {
    Neuron::MeshRenderer meshes;
    meshes.BeginFrame();
    meshes.Sphere({0.0F, 0.0F, 0.0F}, 1.0F, TONES, 8);
    const std::size_t first = meshes.Vertices().size();

    const Neuron::MeshRenderer::Batch one = meshes.TakeUnflushed();
    Assert::AreEqual(first, one.vertices.size());
    Assert::AreEqual(0u, one.firstVertex);

    meshes.Sphere({0.0F, 0.0F, 0.0F}, 1.0F, TONES, 8);
    const Neuron::MeshRenderer::Batch two = meshes.TakeUnflushed();
    Assert::AreEqual(first, two.vertices.size(), L"the second take is only the second ball");
    Assert::AreEqual(static_cast<std::uint32_t>(first), two.firstVertex, L"and it sits after the first in the frame");
    Assert::IsTrue(meshes.TakeUnflushed().vertices.empty());
  }
};

} // namespace NeuronClientTests
