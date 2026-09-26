// GameLogic/Visual.cpp
#include "Visual.h"

#include <map>
#include <string>
#include <utility>

namespace Game
{

namespace
{

std::map<std::string, VisualFactory>& Factories()
{
  // Built on first use: the client registers from static constructors, in no set order.
  static std::map<std::string, VisualFactory> g_factories;
  return g_factories;
}

std::map<std::pair<std::string, DrawPhase>, DrawFunction>& Draws()
{
  static std::map<std::pair<std::string, DrawPhase>, DrawFunction> g_draws;
  return g_draws;
}

} // namespace

void RegisterDraw(const char* _kind, DrawPhase _phase, DrawFunction _draw)
{
  Draws()[{_kind, _phase}] = _draw;
}

void Draw(const char* _kind, DrawPhase _phase, ObjectT* _object, DrawState* _state)
{
  auto found = Draws().find({_kind, _phase});
  if (found != Draws().end())
  {
    found->second(_object, _state);
  }
}

void RegisterVisual(const char* _kind, VisualFactory _factory)
{
  Factories()[_kind] = _factory;
}

RenderableT* CreateVisual(const char* _kind, ObjectT* _object)
{
  auto found = Factories().find(_kind);
  return found == Factories().end() ? nullptr : found->second(_object);
}

} // namespace Game
