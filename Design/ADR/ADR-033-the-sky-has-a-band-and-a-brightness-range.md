# ADR-033 — The sky has a band, and brightness is the cue

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** Owner, asked directly, closing the two open questions ADR-032 left.

**Supersedes:** —

---

## Context

ADR-032 rebuilt the star field as directions on a sphere and deliberately stopped there, recording
two things it did not do:

> **Whether stars should twinkle or vary in brightness.** They vary in size across a 0.7–1.2 px
> range and that is all. Brightness would be the better cue and needs a colour per star rather than
> one for the field.
>
> **Whether the sky should be denser near the galactic plane.** Real skies are. This one is uniform,
> which is the honest default and looks like deep space rather than like anywhere in particular.

Both were put to the owner and both were taken.

## Decision

**Brightness varies, and size follows it.**

Each star carries one number. The dot's radius is that number mapped across 0.7–1.2 px and its alpha
is that number mapped across half to all of the colour the caller passes. One draw, not two,
because a bright small star and a dim large one are both things the eye reads as a contradiction and
two independent draws would produce both.

`Starfield::Draw`'s colour argument is now **the brightest star** rather than the whole field's one
tone. `MainPage` raises it from alpha 140 to 220 to match, which puts the faintest star at 110 and
the mean near 150 — about as present as the flat field was, with the bright ones standing out of it.

**The sky is denser towards a galactic plane**, by rejection against a bell in the angle off that
plane. Measured: the plane carries **3.8 times** the density of the poles over equal solid angle.

**The plane is tilted rather than being the map's own.** The systems lie on y = 0, so an in-fiction
galactic band would run exactly along the horizon — under the ground grid, and mostly off the bottom
of a pane whose camera always looks down. Tilted, it crosses the sky where it can be seen, and it
gives the sky an orientation: you can tell roughly which way you are facing from the background
alone, which is most of the argument for having a band at all.

## Two things that were tried and were worse

**Brightness skewed as the square of the draw.** Closer to a real sky, where faint stars vastly
outnumber bright ones, and at thirty stars in a small pane it is indistinguishable from having drawn
fewer stars: most of the field went to 0.7-pixel dots at an alpha too low to see. The screenshot of
it reads as an *emptier* sky than the uniform one it replaced, with exactly the same number of stars
in it. The curve is now the power of one and a half, and the faintest star keeps half the alpha
rather than 45% of a lower base.

**A band at 0.6 strength**, which measured 2.2× and could not be seen. At a 40-degree field of view
so little of the sky is in the pane at once that a band only reads as one when it is markedly denser
than what is beside it. 0.82 measured 3.8× and reads.

Both of these are the same lesson and it is worth writing down: **this pane shows about a fortieth of
the sky, so any property distributed across the whole sphere has to be exaggerated to survive the
crop.** Realism tuned by the numbers looked wrong; the numbers had to be tuned by the screenshot.

## Consequences

**1100 stars, up from 800.** The band takes stars out of most of the sky and puts them in one place,
so the count had to rise to keep the sparse directions populated. Measured across the whole tilt and
a full turn: **13 stars in view at the emptiest angle and 89 at the fullest**, averaging about thirty.
The old uniform field was 17 to 41. A wider spread is what a band *is*.

**The band widens the top-to-bottom variation too**, from 0.44–1.29 to 0.73–2.71, because the band
crosses the top of the pane at some orientations and the bottom at others. The test that guards
against a systematically starved edge has been loosened to match, and its comment says why — that
guard exists to catch a trend, and a band is a trend somebody asked for.

**Generation is now a rejection loop, and it terminates.** Acceptance never falls below
`1 - bandStrength`, which is 0.18 here, so a star takes a few draws at worst and cannot hang. A
rejection scheme with no floor is the one that spins forever; this one has a floor by construction.

**The band strength is a constructor parameter, not a constant**, so that the pole-bunching test can
ask for a sky with no band in it. A deliberate concentration towards the plane would otherwise mask
exactly the accidental one that test is looking for.

## What this changes elsewhere

`Starfield::Star` gains a brightness; `MainPage`'s `STAR` colour becomes the top of a range. Nothing
outside the sky is touched.

## Open questions

**Whether the band should be visible from the map's own orientation on purpose.** It is tilted to an
arbitrary direction. Tying it to something in the fiction — the direction of the sealed region, say,
or wherever the galaxy's core is meant to be — would make it mean something rather than merely read
well.

**Whether stars should have colour as well as brightness.** They are all the same white. Real fields
are not, and a few warm and cool ones would cost a draw each.

**Whether twinkling is worth it.** ADR-032 raised it and nobody has asked for it. It would be the
first thing on this screen that moves on its own other than the countdown, which is a reason to be
careful rather than a reason not to.
