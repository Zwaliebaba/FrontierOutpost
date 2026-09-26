#include "GraphicsEffects.h"

#include "Drawable.h"

#include "Objects.h"

#include "SoundEngine.h"

#include "Bound.h"
#include "LteMath.h"
#include "Matrix.h"
#include "Transform.h"
#include "Vector.h"

#include "Presentation.h"

/* The particle effects are the client's (FrontierOutpost/ClientPresentation.cpp), called here where
   the game has always called them, so that they draw on Rand in the same order (ADR-016). */

void Effect_BeamHit(
  Position const& origin,
  V3 const& baseVelocity,
  float scale,
  V3 const& color)
{
  if (Game::Presentation* presentation = Game::GetPresentation())
    presentation->BeamHit(origin, baseVelocity, scale, color);
}

void Effect_ParticleFirefly(
  Position const& origin,
  V3 const& velocity,
  V3 const& color,
  float size,
  float lifeTime)
{
  if (Game::Presentation* presentation = Game::GetPresentation())
    presentation->ParticleFirefly(origin, velocity, color, size, lifeTime);
}

void Effect_MultiExplosionRadial(
  Object const& object,
  float scale,
  ExplosionType type)
{
  ComponentDrawable* d = object->GetDrawable();
  scale *= Length(object->GetExtent());

  for (int i = 0; i < 15; ++i) {
    float age = -Pow(RandExp(), 1.5f);
    float duration = Rand(1.0f, 5.0f);

    Object e = Object_Explosion(type, age, duration);
    /* CRITICAL : Negative scale o.o */
    object->Attach(e, Transform_ST(-Sqrt(RandExp()), d->renderable()->Sample()));
  }

  if (type == ExplosionType_Plasma)
    object->GetContainer()->AddInterior(
      Object_SoundEmitter("shield/explosion.wav",
                          object->GetPos(), 1, scale / 15));
  else if (type == ExplosionType_Fire)
    object->GetContainer()->AddInterior(
      Object_SoundEmitter("explosion/altsmall3.wav",
                          object->GetPos(), 0.5f, scale / 15));

}

void Effect_SmallPlume(
  Position const& origin,
  V3 const& baseVelocity,
  V3 const& color,
  float size)
{
  if (Game::Presentation* presentation = Game::GetPresentation())
    presentation->SmallPlume(origin, baseVelocity, color, size);
}
