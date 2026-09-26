// Tests/GameLogicTests/ClientlessGame.cpp
//
// The game runs without a client (Design/ADR/ADR-016). This suite links NeuronCore, GameLogic and
// NeuronServer whole and NeuronClient not at all, so a render, window or input symbol left in the
// game stops it linking: the link is the test that matters most. The tests below check what the
// game sees without a client: no presentation, and draw calls that land nowhere.
#include "pch.h"

#include "Presentation.h"
#include "Server.h"
#include "Visual.h"

#include <cstring>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

int g_interiorDraws = 0;
int g_objectDraws = 0;

void CountInteriorDraw(ObjectT*, DrawState*)
{
  ++g_interiorDraws;
}

void CountObjectDraw(ObjectT*, DrawState*)
{
  ++g_objectDraws;
}

} // namespace

TEST_CLASS(ClientlessGame)
{
public:
  TEST_METHOD(NoPresentationWithoutAClient)
  {
    Assert::IsNull(Game::GetPresentation());
  }

  TEST_METHOD(DrawingAKindNobodyRegisteredDoesNothing)
  {
    Game::Draw("GameLogicTests.Unregistered", Game::DrawPhase::Object, nullptr, nullptr);
  }

  TEST_METHOD(ADrawRunsForItsKindAndPhaseAlone)
  {
    Game::RegisterDraw("GameLogicTests.Counted", Game::DrawPhase::Interior, CountInteriorDraw);
    Game::RegisterDraw("GameLogicTests.Counted", Game::DrawPhase::Object, CountObjectDraw);
    g_interiorDraws = 0;
    g_objectDraws = 0;

    Game::Draw("GameLogicTests.Counted", Game::DrawPhase::Interior, nullptr, nullptr);
    Game::Draw("GameLogicTests.Counted", Game::DrawPhase::Interior, nullptr, nullptr);
    Game::Draw("GameLogicTests.Counted", Game::DrawPhase::EndInterior, nullptr, nullptr);
    Game::Draw("GameLogicTests.Other", Game::DrawPhase::Object, nullptr, nullptr);

    Assert::AreEqual(2, g_interiorDraws);
    Assert::AreEqual(0, g_objectDraws);
  }

  TEST_METHOD(TheServerLibraryIsLinked)
  {
    Assert::AreEqual(0, std::strcmp(Neuron::Server::Name(), "NeuronServer"));
  }
};

} // namespace GameLogicTests
