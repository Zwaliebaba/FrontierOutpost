#include "Generators.h"

#include "Messages.h"
#include "Planet.h"

#include "CubeMap.h"
#include "Hash.h"
#include "LteMath.h"
#include "RNG.h"
#include "ShaderInstance.h"
#include "StackFrame.h"

const size_t res = 1024;

namespace {
  CubeMap Generate(Planet const* planet) {
    SFRAME("Generate Planet Skybox");
    static Shader shader = Shader_Create("identity.jsl", "gen/planetskybox.jsl");

    CubeMap self = CubeMap_Create(res);
    RNG rg = RNG_MTG(Hash(planet->GetPos()));

    MessageCollectStars stars;
    planet->GetContainer()->Broadcast(stars);

    (*shader)
      ("atmoDensity", planet->atmoDensity)
      ("atmoTint", planet->atmoTint)
      ("starColor", stars.GetColor())
      ("starPos", (V3)stars.GetPosition());

    self->GenerateFromShader(shader);
    return self;
  }
}

Generic<CubeMap> Generator_PlanetSkybox(Planet const* planet) {
  return Cached(Bind(FreeFn(Generate), planet));
}
