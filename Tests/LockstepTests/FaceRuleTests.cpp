// FaceRuleTests.cpp -- ADR-074's face rule, held to account by something other than memory.
//
// The rule is one sentence: data is mono, sentences are sans. It is applied at eighty-odd draw
// sites across four screens, and it is the kind of rule that decays silently. Nothing breaks when
// a new line of copy is drawn in the wrong face -- the screen still renders, every test still
// passes, and the only symptom is that a paragraph is set in the face that exists to hold columns.
// ADR-074 asked for this to be checked rather than remembered, and `DrawnStrings` is the seam that
// makes it possible: a headless renderer records what it was ASKED to draw, beside the face it was
// asked for, before the string becomes glyph boxes with no words in it.
//
// WHAT IS ASSERTED, AND WHY IT IS NOT WHAT FONT-01 PROPOSED.
//
// The plan proposed two halves: no sans string carries a digit, and no mono string ends in a full
// stop or a question mark. The first half is wrong, and the screen says so. ADR-074 names the
// rail's help line as sans, and at the lock that line reads `Resolving T47. Controls return with
// the new digest. Anything you tap now is an order for T48.` It is a sentence, it is sans by the
// ADR's own list, and it is full of numbers. An event card's detail lines and a sheet row's second
// line are the same -- `Unanswered for 3 more tick(s)` is prose about a quantity. The rule's two
// clauses overlap, and where they do, the sentence wins: `contains a number` distinguishes text
// that IS data, not a sentence that mentions some.
//
// So the sans half asserts what actually separates the two faces here: data on these screens is
// SHOUTED. `Uppercased()` exists for that, and every data literal in the client is written in
// capitals. A string drawn in sans with no lowercase letter in it is a label that got the sentence
// face -- which is the mistake worth catching, and it is catchable.
//
// **That discriminator is borrowed, and it can be taken away.** The screen shouts its labels
// because the 8x8 font had no lowercase; Plex has both, and ADR-074 left open whether the shouting
// should continue now that nothing forces it. If the answer ever becomes no, this test needs a
// different way to tell a label from a sentence -- the face each was ASKED for would still be
// recorded, but the strings would no longer give the rule away.
//
// The mono half keeps the full stop and drops the question mark, for a reason of the same kind:
// `FIRST MATCH?` is a shouted two-word prompt sharing a row with the button it introduces, and it
// aligns with that button. A question mark ends a sentence and also ends a short prompt; a full
// stop only ever ends a sentence.

#include "pch.h"
#include "CppUnitTest.h"

#include "ConnectionDialog.h"
#include "JoinPage.h"
#include "MainPage.h"
#include "SeatsPage.h"
#include "SnapshotView.h"

#include "BotPolicy.h"
#include "MatchSimulation.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

namespace
{

using Neuron::Face;

/// **The family, not the weight and not the size.** `MonoDisplay` is the 16px cut of Plex Mono
/// (ADR-084), so it carries data and is held to the mono half of ADR-074's rule like the other two;
/// a list written as "the two mono faces" is a list that goes wrong the next time a cut is baked.
[[nodiscard]] bool IsMono(Face _face) noexcept
{
  return _face == Face::MonoRegular || _face == Face::MonoMedium || _face == Face::MonoDisplay;
}

/// What one screen drew, kept under the screen's name.
///
/// **Grouped rather than pooled, so that coverage is a claim about each screen.** A flat list lets
/// one screen's sentences stand in for another's: revert every sans site on the main page and a
/// pooled count is still non-zero, because the connection dialog is almost entirely prose.
struct Screen
{
  const char* name;
  std::vector<Neuron::FontRenderer::DrawnString> drawn;
};

/// Every string the four screens draw, gathered from a headless renderer.
///
/// One renderer across every page rather than one each, and the strings are taken after each page
/// rather than at the end, because `BeginFrame` clears the record -- the recording is per frame,
/// which is what keeps it from growing without bound in a test that draws in a loop.
[[nodiscard]] std::vector<Screen> EverythingDrawn()
{
  Neuron::ShapeRenderer shapes;
  Neuron::FontRenderer text;

  std::vector<Screen> screens;
  const auto collect = [&text, &screens](const char* _name)
  {
    const std::vector<Neuron::FontRenderer::DrawnString>& frame = text.DrawnStrings();
    if (!screens.empty() && std::string_view{screens.back().name} == _name)
    {
      screens.back().drawn.insert(screens.back().drawn.end(), frame.begin(), frame.end());
      return;
    }
    screens.push_back(Screen{_name, {frame.begin(), frame.end()}});
  };

  // ---- The main page, at a tick with a digest behind it ------------------------------------------
  //
  // Six seats and five bots, resolved far enough that the digest has cards with details and
  // verdicts in it -- which is where every sans string on this screen comes from. An empty match
  // would pass this test by drawing no sentences at all.
  Lockstep::MatchRules rules;
  rules.playerCount = 6;
  std::vector<std::optional<Lockstep::BotPolicy>> bots(6, std::optional<Lockstep::BotPolicy>{Lockstep::BotPolicy::ExpandNear});
  bots[0].reset();

  Lockstep::MatchSimulation simulation{rules, 0x5441'5053'2121'2121ULL, bots};
  for (std::int32_t tick = 0; tick < 12; ++tick)
  {
    simulation.Resolve();
  }

  const Lockstep::PlayerId seat{0};
  const Lockstep::MatchState state =
    Lockstep::ViewOf(Lockstep::Snapshot::For(simulation.State(), seat), Lockstep::Snapshot::DigestFor(simulation.LastTick(), seat), 600);

  Lockstep::MainPage main;
  main.Create(state);
  shapes.BeginFrame();
  text.BeginFrame();
  main.DrawWorld(shapes, text);
  main.DrawInterface(shapes, text);
  collect("the main page");

  // ---- The lobby ---------------------------------------------------------------------------------
  Lockstep::SeatsPage seats{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};
  shapes.BeginFrame();
  text.BeginFrame();
  seats.DrawWorld(shapes, text);
  seats.DrawInterface(shapes, text);
  collect("the lobby");

  // ---- The join screen, refused ------------------------------------------------------------------
  //
  // Refused rather than idle, because the refusal detail is one of the two sentences this screen
  // draws and it is only on the screen after the server has said no.
  Lockstep::JoinPage join;
  join.SetStatus(Lockstep::JoinPage::Status::Refused, "No answer from that server.");
  shapes.BeginFrame();
  text.BeginFrame();
  join.DrawWorld(shapes, text);
  join.DrawInterface(shapes, text);
  collect("the join screen");

  // ---- The connection dialog, every kind it has --------------------------------------------------
  //
  // A dialog is almost entirely paragraphs, and each kind has its own. Drawing one of them would
  // check one paragraph and claim to have checked the rule.
  for (const Lockstep::ConnectionDialog::Kind kind :
       {Lockstep::ConnectionDialog::Kind::Connecting, Lockstep::ConnectionDialog::Kind::Waiting, Lockstep::ConnectionDialog::Kind::Refused,
        Lockstep::ConnectionDialog::Kind::Lost, Lockstep::ConnectionDialog::Kind::Finished})
  {
    Lockstep::ConnectionDialog::Facts facts;
    facts.server = "127.0.0.1:7341";
    facts.reason = Neuron::RefusalReason::UnknownToken;
    facts.seat = 0;
    facts.reconnects = 2;
    facts.secondsToNextAttempt = 3.0;
    facts.lockCountdown = "00:04:12";
    facts.standings = {Lockstep::ConnectionDialog::Facts::Standing{.text = "1ST  P6         95", .isYou = false},
                       Lockstep::ConnectionDialog::Facts::Standing{.text = "3RD  YOU        35", .isYou = true}};

    Lockstep::ConnectionDialog dialog;
    dialog.Update(kind, facts, 0.0);
    shapes.BeginFrame();
    text.BeginFrame();
    dialog.Draw(shapes, text);
    collect("the connection dialog");
  }

  return screens;
}

/// The offenders, as one message, rather than the first one.
///
/// A rule applied at eighty sites is broken in batches -- a new screen, a reworded paragraph -- and
/// a test that names only the first offender turns one fix into as many runs as there are strings.
[[nodiscard]] std::wstring Listed(std::string_view _what, const std::vector<std::string>& _offenders)
{
  std::wstring message{_what.begin(), _what.end()};
  for (const std::string& offender : _offenders)
  {
    message += L"\n  \"" + std::wstring{offender.begin(), offender.end()} + L"\"";
  }
  return message;
}

} // namespace

TEST_CLASS(FaceRuleTests)
{
public:
  // A sentence drawn in the data face. The full stop is the tell, and it is the one piece of
  // punctuation on these screens that means nothing else.
  TEST_METHOD(NothingInMonoEndsASentence)
  {
    std::vector<std::string> offenders;
    for (const Screen& screen : EverythingDrawn())
    {
      for (const Neuron::FontRenderer::DrawnString& drawn : screen.drawn)
      {
        if (IsMono(drawn.face) && !drawn.text.empty() && drawn.text.back() == '.')
        {
          offenders.push_back(std::string{screen.name} + ": " + drawn.text);
        }
      }
    }

    Assert::IsTrue(offenders.empty(), Listed("drawn in mono, but ends a sentence (ADR-074: sentences are sans)", offenders).c_str());
  }

  // A label drawn in the sentence face. Data on these screens is shouted -- `Uppercased()` is what
  // does it -- so a sans string with no lowercase letter in it is a label that took the wrong face.
  TEST_METHOD(NothingInSansIsShouted)
  {
    std::vector<std::string> offenders;
    for (const Screen& screen : EverythingDrawn())
    {
      for (const Neuron::FontRenderer::DrawnString& drawn : screen.drawn)
      {
        if (IsMono(drawn.face))
        {
          continue;
        }
        const bool hasLetter = std::ranges::any_of(drawn.text, [](unsigned char _c) { return std::isalpha(_c) != 0; });
        const bool hasLower = std::ranges::any_of(drawn.text, [](unsigned char _c) { return std::islower(_c) != 0; });
        if (hasLetter && !hasLower)
        {
          offenders.push_back(std::string{screen.name} + ": " + drawn.text);
        }
      }
    }

    Assert::IsTrue(offenders.empty(), Listed("drawn in sans, but shouted (ADR-074: data is mono)", offenders).c_str());
  }

  // **Every screen has to reach both families**, or the two tests above pass on it by drawing
  // nothing they can object to. Per screen rather than overall: the connection dialog is almost
  // all prose and the map is almost all labels, so a pooled count stays comfortably non-zero while
  // an entire page has been reverted to one face.
  TEST_METHOD(BothFamiliesAreOnTheScreen)
  {
    std::vector<std::string> offenders;
    for (const Screen& screen : EverythingDrawn())
    {
      std::size_t mono = 0;
      std::size_t sans = 0;
      for (const Neuron::FontRenderer::DrawnString& drawn : screen.drawn)
      {
        (IsMono(drawn.face) ? mono : sans) += 1;
      }

      if (mono == 0 || sans == 0)
      {
        offenders.push_back(std::string{screen.name} + " drew " + std::to_string(mono) + " in mono and " + std::to_string(sans) +
                            " in sans");
      }
    }

    Assert::IsTrue(offenders.empty(),
                   Listed("a screen is not using both families, so the two rules above check nothing on it", offenders).c_str());
  }
};

} // namespace LockstepTests
