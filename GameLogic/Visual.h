// GameLogic/Visual.h
#pragma once

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

} // namespace Game
