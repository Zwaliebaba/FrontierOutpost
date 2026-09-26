// GameLogic/Presentation.h
#pragma once

struct DrawState;
struct ObjectT;
struct RenderableT;
template <class T> struct V3T;

namespace Game
{

/// What the game asks of whatever shows it: drawing objects and interiors, the particles each
/// interior keeps, and the particle effects game events set off. The client installs one; a
/// server installs none, and the game runs without it (ADR-016).
///
/// The effects take the game's shared random numbers (Rand), so the game calls them exactly where
/// it always did, and a client's run draws the same sequence as before. A run without a
/// presentation draws fewer, which a deterministic server will have to account for.
class Presentation
{
public:
  virtual ~Presentation() = default;

  /// Draws _object's renderable where the object is.
  virtual void DrawObject(ObjectT* _object, RenderableT* _renderable, DrawState* _state) = 0;
  /// Draws what is inside _interior, when the view is inside it.
  virtual void DrawInterior(ObjectT* _interior, DrawState* _state) = 0;
  /// Where the view being drawn is: game code that follows the camera while it is drawn, such as
  /// a zone's asteroid fields, reads it here.
  [[nodiscard]] virtual V3T<double> ViewPosition(DrawState* _state) = 0;

  /// An interior's particles, which it steps and makes current for effects around its update.
  /// _interior identifies the interior; the presentation makes its particles on first use.
  virtual void BeginInteriorUpdate(const void* _interior, float _dtSeconds) = 0;
  virtual void EndInteriorUpdate(const void* _interior) = 0;
  /// The interior is gone, and so are its particles.
  virtual void ForgetInterior(const void* _interior) = 0;

  virtual void BeamHit(const V3T<double>& _origin, const V3T<float>& _baseVelocity, float _scale, const V3T<float>& _color) = 0;
  virtual void ParticleFirefly(const V3T<double>& _origin, const V3T<float>& _velocity, const V3T<float>& _color, float _size,
                               float _lifeSeconds) = 0;
  virtual void SmallPlume(const V3T<double>& _origin, const V3T<float>& _baseVelocity, const V3T<float>& _color, float _scale) = 0;
};

/// Installs the client's presentation. It must outlive every game object.
void SetPresentation(Presentation* _presentation) noexcept;

/// The installed presentation, or null where there is none, as on a server.
[[nodiscard]] Presentation* GetPresentation() noexcept;

} // namespace Game
