# UI-02 - Touch targets, and the case of a title

**Status:** **In progress**, written 2026-09-14. Three owner decisions taken the same day, before
any code: **the floor is 44 everywhere**; **the garrison badge keeps its 16px drawing and gets a 44px
hit rectangle**, because a touch target and a visual are allowed to differ; and **the locks rail
scrolls** the way the digest does (ADR-080), which is what 44px rows force.
 Two decisions taken and deliberately not built in
UI-01: ADR-098 (this game is for touch and its targets are too small) and ADR-099 (card titles are
mixed case). Both were held back for the same reason -- each touches essentially every draw call on
the main page, and doing either inside UI-01 would have meant redoing the other's work.

**Do this before the capture pass, or accept that the captures are taken twice.** Twelve of the
twenty-three change for item 1 alone.

---

## 1. Every target reaches the touch floor (ADR-098)

**Problem.** Measured on 2026-09-14: the sheet row is 44, and the three controls a player uses most
are 18 (digest card button), 21 (locks rail row) and 16 (map garrison badge). `SHEET_ROW_HEIGHT`'s
own comment names 44 as "the smallest target a finger hits reliably" and it is applied once.

**Change.**
- ~~Pick the floor~~ **44**, owner decision 2026-09-14. It is the number `SHEET_ROW_HEIGHT` already
  names and already uses; 32 satisfies neither a finger nor the constant in the tree.
- **Done.** `MainPage::BUTTON_HEIGHT` 18 -> the floor, and `LayoutCard`'s action row re-derived
  from it (ADR-100). The re-derivation was not bookkeeping: the row had reserved one `LINE_HEIGHT`
  while `BandTopForText` centred the button on that line's baseline, which at 44 painted a whole
  line above the row. Found in a screenshot, not in the audit.
- **Done.** The locks rail's `row` and section heights -> the floor, **and the rail scrolls**
  (ADR-101). The wheel notch is routed by `HandleZoom`'s rail case, which did nothing until now --
  but the wheel is the shortcut, not the control: the band at the foot of the column is a pair of
  44px targets, because this game is for touch and a finger has no wheel. A row not wholly inside
  the band is culled rather than clipped (`ShapeRenderer` has no clip rectangle) and a culled row
  registers no hit. This closes ADR-086's open question.
- **Done.** Map garrison badge (`BADGE_HEIGHT`, ADR-079): **drawn at 16, hit at 44**, owner
  decision 2026-09-14. A touch target and a visual are allowed to differ and usually should; this keeps
  ADR-079's design (the disc is the system, the badge is what stands on it) and costs nothing on
  screen. Watch the overlap with the disc's own hit and with a neighbouring badge -- hit order
  already resolves ties, and the badges are placed clear of both.
- **Done for the main page.** Top bar chips, the `RESET` chip and the digest's title/page bands.
  The sheet's 36px header and its 22px section band are **not** raised: the header's close corner is
  already a 36x36 hit and the section band is never a target. Recorded in the guidelines rather than
  changed.
- **Done.** `JoinPage`, `SeatsPage` and `ConnectionDialog`, audited the same way and by the same
  test. It named 28 offenders across the three. The floor moved to `Frame::TOUCH_FLOOR` in
  `DesignTokens.h` on the way, with `Frame::GrownToFloor` beside it, because four screens growing
  chips is four copies of a `std::max` otherwise.
- Re-derive every capture's tap coordinate in `Design/UI/README.md` §Photographing.

**Done when.** Every `AddHit` rectangle on every screen is at least the floor in both dimensions,
asserted by a test that walks the hit list rather than by reading the constants; all 23 captures
retaken; the table in `DESIGN-GUIDELINES.md` §Frame updated to say the floor is met.

## 2. Card titles are mixed case (ADR-099)

**Problem.** `BATTLE AT ULME` shouts over its own detail line. Uppercase was a font constraint until
ADR-074 and is now a choice.

**Change.**
- `FontRenderer::DrawnString` carries what a string IS -- label or sentence -- set at the call site.
- `FaceRuleTests` uses that tag instead of looking for a lowercase letter. It gets stronger: a system
  named `McBride` defeats the current discriminator silently.
- Stop applying `Uppercased()` to digest card titles and sheet titles; keep it for chips, section
  headers and status words.

**Done when.** `FaceRuleTests` passes on the tag; no card title is uppercased; `01-main-page.png` and
the sheet captures retaken; `DESIGN-GUIDELINES.md` §Copy says the rule in the present tense.

## Order

1 then 2, because 1 moves the boxes the titles sit in.

## What has landed

| | |
|---|---|
| ADR-100 | A target is 44 pixels, and a sibling grows its box while an isolated chip grows only its hit. `TouchTargetTests` is the audit. |
| ADR-101 | The locks rail scrolls, and its page band -- not the wheel -- is the control. |

Section 1 is done. What is left in this plan is section 2, and then the captures.

Commits: `Hold the main page's targets to the 44px floor`, and the rail's scroll.
