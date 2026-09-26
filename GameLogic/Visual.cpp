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

std::map<std::string, SeededVisualFactory>& SeededFactories()
{
  static std::map<std::string, SeededVisualFactory> g_seededFactories;
  return g_seededFactories;
}

std::map<std::pair<std::string, DrawPhase>, DrawFunction>& Draws()
{
  static std::map<std::pair<std::string, DrawPhase>, DrawFunction> g_draws;
  return g_draws;
}

} // namespace

void RegisterSeededVisual(const char* _kind, SeededVisualFactory _factory)
{
  SeededFactories()[_kind] = _factory;
}

void CreateSeededVisual(const char* _kind, std::uint32_t _seed, std::uint32_t _count, Reference<RenderableT>& _visual)
{
  auto found = SeededFactories().find(_kind);
  if (found != SeededFactories().end())
  {
    found->second(_seed, _count, _visual);
  }
}

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

void CreateVisual(const char* _kind, ObjectT* _object, Reference<RenderableT>& _visual)
{
  auto found = Factories().find(_kind);
  if (found != Factories().end())
  {
    found->second(_object, _visual);
  }
}

} // namespace Game
