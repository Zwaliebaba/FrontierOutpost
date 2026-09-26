// GameLogic/Presentation.cpp
#include "Presentation.h"

namespace Game
{

namespace
{

Presentation* g_presentation = nullptr;

} // namespace

void SetPresentation(Presentation* _presentation) noexcept
{
  g_presentation = _presentation;
}

Presentation* GetPresentation() noexcept
{
  return g_presentation;
}

} // namespace Game
