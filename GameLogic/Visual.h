// GameLogic/Visual.h
#pragma once

#include <cstdint>

struct DrawState;
struct ObjectT;
struct RenderableT;

namespace Game
{

/// Makes the renderable that shows one game object. The client registers one for each kind of
/// object that is drawn (ADR-016). The renderable keeps the object it is given, and the object
/// keeps the renderable, as its Drawable component did before.
using VisualFactory = RenderableT* (*)(ObjectT*);

/// Registers the factory for a kind of object. The client calls it before any object is made.
void RegisterVisual(const char* _kind, VisualFactory _factory);

/// The renderable for _object, or null where no factory is registered for _kind, as on a server,
/// which draws nothing.
[[nodiscard]] RenderableT* CreateVisual(const char* _kind, ObjectT* _object);

/// The draw methods a kind of object overrides: its own drawing, and its interior's before, while
/// and after the objects inside it are drawn.
enum class DrawPhase : std::uint8_t
{
  Object,
  Interior,
  BeginInterior,
  EndInterior
};

using DrawFunction = void (*)(ObjectT*, DrawState*);

/// Registers how the client draws one phase of a kind of object.
void RegisterDraw(const char* _kind, DrawPhase _phase, DrawFunction _draw);

/// Draws one phase of _object as the client registered it for _kind; does nothing where nothing is
/// registered, as on a server.
void Draw(const char* _kind, DrawPhase _phase, ObjectT* _object, DrawState* _state);

} // namespace Game
