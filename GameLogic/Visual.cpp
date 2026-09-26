// GameLogic/Visual.cpp
#include "Visual.h"

#include <map>
#include <string>

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

} // namespace

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
