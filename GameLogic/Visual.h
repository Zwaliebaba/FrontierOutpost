// GameLogic/Visual.h
#pragma once

#include <cstdint>

struct DrawState;
struct ObjectT;
struct RenderableT;
template <class T> struct Reference;

namespace Game
{

/// Makes the renderable that shows one game object, into its second argument. The client
/// registers one for each kind of object that is drawn (ADR-016). The renderable is handed back
/// as a reference, never a bare pointer, so the one it makes is held from the moment it exists.
using VisualFactory = void (*)(ObjectT*, Reference<RenderableT>&);

/// Registers the factory for a kind of object. The client calls it before any object is made.
void RegisterVisual(const char* _kind, VisualFactory _factory);

/// Makes the renderable for _object into _visual, or leaves _visual as it was where no factory is
/// registered for _kind, as on a server, which draws nothing.
void CreateVisual(const char* _kind, ObjectT* _object, Reference<RenderableT>& _visual);

/// Makes a renderable from a seed, and for some kinds a count: an asteroid's shape, a starfield.
using SeededVisualFactory = void (*)(std::uint32_t, std::uint32_t, Reference<RenderableT>&);

/// Registers the factory for a kind of seeded renderable.
void RegisterSeededVisual(const char* _kind, SeededVisualFactory _factory);

/// Makes the renderable of _kind for _seed and _count into _visual, or leaves it as it was where no
/// factory is registered for _kind.
void CreateSeededVisual(const char* _kind, std::uint32_t _seed, std::uint32_t _count, Reference<RenderableT>& _visual);

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
