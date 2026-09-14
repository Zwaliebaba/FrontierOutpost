# UI-02 - Touch targets, and the case of a title

**Status:** **Not started**, written 2026-09-14. Two decisions taken and deliberately not built in
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
- Pick the floor: 44, or 32 with a written reason. ADR-098's first open question.
- `MainPage::BUTTON_HEIGHT` 18 -> the floor, and re-derive `LayoutCard`'s action row from it.
- The locks rail's `row` height -> the floor. The rail does not scroll (ADR-086's open question), so
  this is the item where a late-game rail runs off the bottom; decide then whether it scrolls.
- Map garrison badge (`BADGE_HEIGHT`, ADR-079): a 44px box beside a system name is a different
  design, not a bigger one. Options are a larger hit rectangle around a small drawn badge, or
  moving the badge to the system's own hit. **[ASK]** if neither is obviously right.
- Top bar chips, the `RESET` chip, the sheet's bands and the digest's title/page bands.
- Re-derive every capture's tap coordinate in `Design/UI/README.md` §Photographing.

**Done when.** Every `AddHit` rectangle on every screen is at least the floor tall, asserted by a
test that walks the hit list rather than by reading the constants; all 23 captures retaken; the table
in `DESIGN-GUIDELINES.md` §Frame updated to say the floor is met.

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
