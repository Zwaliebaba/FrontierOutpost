#include "ShaderRegistry.h"

/* FXC's headers declare their bytecode as BYTE, which is windows.h's. */
typedef unsigned char BYTE;

#include "CompiledShaders/AtmospherePS.h"
#include "CompiledShaders/BeamPS.h"
#include "CompiledShaders/BillboardAxisVS.h"
#include "CompiledShaders/BillboardAxisWrappedVS.h"
#include "CompiledShaders/BillboardSoftVS.h"
#include "CompiledShaders/BillboardVS.h"
#include "CompiledShaders/ComputeLensflareVisibilityPS.h"
#include "CompiledShaders/CubemapBlurPS.h"
#include "CompiledShaders/CubemapIrmapPS.h"
#include "CompiledShaders/CubemapMultiplyPS.h"
#include "CompiledShaders/DepthprepassPS.h"
#include "CompiledShaders/DustfleckPS.h"
#include "CompiledShaders/ExplosionPS.h"
#include "CompiledShaders/FilterAoPS.h"
#include "CompiledShaders/FilterDirtPS.h"
#include "CompiledShaders/FilterRustPS.h"
#include "CompiledShaders/GenLensflarePS.h"
#include "CompiledShaders/GenNebulaPS.h"
#include "CompiledShaders/GenNormalmapLumPS.h"
#include "CompiledShaders/GenPlanetPS.h"
#include "CompiledShaders/GenPlanetringPS.h"
#include "CompiledShaders/GenPlanetskyboxPS.h"
#include "CompiledShaders/GenPlatingPS.h"
#include "CompiledShaders/GenRockPS.h"
#include "CompiledShaders/GenStarbgPS.h"
#include "CompiledShaders/LensflarePS.h"
#include "CompiledShaders/LightCompositePS.h"
#include "CompiledShaders/LightGlobalPS.h"
#include "CompiledShaders/LightPointPS.h"
#include "CompiledShaders/MaterialDebugPS.h"
#include "CompiledShaders/MaterialFresnelPS.h"
#include "CompiledShaders/MaterialLambertPS.h"
#include "CompiledShaders/MaterialLodfadePS.h"
#include "CompiledShaders/MaterialMetalPS.h"
#include "CompiledShaders/MaterialWaterPS.h"
#include "CompiledShaders/ParticleRadialPS.h"
#include "CompiledShaders/ParticleRadialtexturedPS.h"
#include "CompiledShaders/ParticleVS.h"
#include "CompiledShaders/PlanetPS.h"
#include "CompiledShaders/PlanetringPS.h"
#include "CompiledShaders/PostAddPS.h"
#include "CompiledShaders/PostCircleminmaxPS.h"
#include "CompiledShaders/PostColorgrade1DPS.h"
#include "CompiledShaders/PostColorgradebezierPS.h"
#include "CompiledShaders/PostDitherPS.h"
#include "CompiledShaders/PostEdgedetectPS.h"
#include "CompiledShaders/PostExpmapPS.h"
#include "CompiledShaders/PostLensflareCompositePS.h"
#include "CompiledShaders/PostLevelsPS.h"
#include "CompiledShaders/PostLinearPS.h"
#include "CompiledShaders/PostMaxPS.h"
#include "CompiledShaders/PostMedianPS.h"
#include "CompiledShaders/PostMinPS.h"
#include "CompiledShaders/PostMultiplyPS.h"
#include "CompiledShaders/PostPowerPS.h"
#include "CompiledShaders/PostSamplecubicPS.h"
#include "CompiledShaders/PostSaturatePS.h"
#include "CompiledShaders/PostVignettePS.h"
#include "CompiledShaders/PulseHeadPS.h"
#include "CompiledShaders/PulseTailPS.h"
#include "CompiledShaders/RailPS.h"
#include "CompiledShaders/ShieldExplosionPS.h"
#include "CompiledShaders/ShieldPS.h"
#include "CompiledShaders/SkyboxPS.h"
#include "CompiledShaders/SkyboxVS.h"
#include "CompiledShaders/Smaa1PS.h"
#include "CompiledShaders/Smaa1VS.h"
#include "CompiledShaders/Smaa2PS.h"
#include "CompiledShaders/Smaa2VS.h"
#include "CompiledShaders/Smaa3PS.h"
#include "CompiledShaders/Smaa3VS.h"
#include "CompiledShaders/StarbgPS.h"
#include "CompiledShaders/ThrusterTrailPS.h"
#include "CompiledShaders/TrailPS.h"
#include "CompiledShaders/TrailVS.h"
#include "CompiledShaders/TransferbeamPS.h"
#include "CompiledShaders/TreePS.h"
#include "CompiledShaders/WorldrayVS.h"
#include "CompiledShaders/WormholePS.h"

/* The game's shaders, which FrontierOutpost compiles from its own Shaders/ (Design/ADR/ADR-014), by
 * their legacy names as NeuronClient/ShaderRegistry.cpp holds the engine's. Shader_Create finds
 * them through the same lookup. Build/CheckProjectFiles.py reads the tables below, one entry a line,
 * and holds them to ADR-008. */
namespace {
  typedef ShaderRegistryEntry Entry;

  template <std::size_t Count>
  std::span<std::byte const> Bytes(BYTE const (&bytecode)[Count]) {
    return std::as_bytes(std::span(bytecode));
  }

  Entry const kVertex[] = {
    {"billboard.jsl", Bytes(BILLBOARD_VS)},
    {"billboard_axis.jsl", Bytes(BILLBOARD_AXIS_VS)},
    {"billboard_axis_wrapped.jsl", Bytes(BILLBOARD_AXIS_WRAPPED_VS)},
    {"billboard_soft.jsl", Bytes(BILLBOARD_SOFT_VS)},
    {"particle.jsl", Bytes(PARTICLE_VS)},
    {"skybox.jsl", Bytes(SKYBOX_VS)},
    {"smaa_1.jsl", Bytes(SMAA1_VS)},
    {"smaa_2.jsl", Bytes(SMAA2_VS)},
    {"smaa_3.jsl", Bytes(SMAA3_VS)},
    {"trail.jsl", Bytes(TRAIL_VS)},
    {"worldray.jsl", Bytes(WORLDRAY_VS)},
  };

  Entry const kPixel[] = {
    {"atmosphere.jsl", Bytes(ATMOSPHERE_PS)},
    {"beam.jsl", Bytes(BEAM_PS)},
    {"compute/lensflare_visibility.jsl", Bytes(COMPUTE_LENSFLARE_VISIBILITY_PS)},
    {"cubemap/blur.jsl", Bytes(CUBEMAP_BLUR_PS)},
    {"cubemap/irmap.jsl", Bytes(CUBEMAP_IRMAP_PS)},
    {"cubemap/multiply.jsl", Bytes(CUBEMAP_MULTIPLY_PS)},
    {"depthprepass.jsl", Bytes(DEPTHPREPASS_PS)},
    {"dustfleck.jsl", Bytes(DUSTFLECK_PS)},
    {"explosion.jsl", Bytes(EXPLOSION_PS)},
    {"filter_ao.jsl", Bytes(FILTER_AO_PS)},
    {"filter_dirt.jsl", Bytes(FILTER_DIRT_PS)},
    {"filter_rust.jsl", Bytes(FILTER_RUST_PS)},
    {"gen/lensflare.jsl", Bytes(GEN_LENSFLARE_PS)},
    {"gen/nebula.jsl", Bytes(GEN_NEBULA_PS)},
    {"gen/normalmap_lum.jsl", Bytes(GEN_NORMALMAP_LUM_PS)},
    {"gen/planet.jsl", Bytes(GEN_PLANET_PS)},
    {"gen/planetring.jsl", Bytes(GEN_PLANETRING_PS)},
    {"gen/planetskybox.jsl", Bytes(GEN_PLANETSKYBOX_PS)},
    {"gen/plating.jsl", Bytes(GEN_PLATING_PS)},
    {"gen/rock.jsl", Bytes(GEN_ROCK_PS)},
    {"gen/starbg.jsl", Bytes(GEN_STARBG_PS)},
    {"lensflare.jsl", Bytes(LENSFLARE_PS)},
    {"light/composite.jsl", Bytes(LIGHT_COMPOSITE_PS)},
    {"light/global.jsl", Bytes(LIGHT_GLOBAL_PS)},
    {"light/point.jsl", Bytes(LIGHT_POINT_PS)},
    {"material/debug.jsl", Bytes(MATERIAL_DEBUG_PS)},
    {"material/fresnel.jsl", Bytes(MATERIAL_FRESNEL_PS)},
    {"material/lambert.jsl", Bytes(MATERIAL_LAMBERT_PS)},
    {"material/lodfade.jsl", Bytes(MATERIAL_LODFADE_PS)},
    {"material/metal.jsl", Bytes(MATERIAL_METAL_PS)},
    {"material/water.jsl", Bytes(MATERIAL_WATER_PS)},
    {"particle_radial.jsl", Bytes(PARTICLE_RADIAL_PS)},
    {"particle_radialtextured.jsl", Bytes(PARTICLE_RADIALTEXTURED_PS)},
    {"planet.jsl", Bytes(PLANET_PS)},
    {"planetring.jsl", Bytes(PLANETRING_PS)},
    {"post/add.jsl", Bytes(POST_ADD_PS)},
    {"post/circleminmax.jsl", Bytes(POST_CIRCLEMINMAX_PS)},
    {"post/colorgrade1D.jsl", Bytes(POST_COLORGRADE1D_PS)},
    {"post/colorgradebezier.jsl", Bytes(POST_COLORGRADEBEZIER_PS)},
    {"post/dither.jsl", Bytes(POST_DITHER_PS)},
    {"post/edgedetect.jsl", Bytes(POST_EDGEDETECT_PS)},
    {"post/expmap.jsl", Bytes(POST_EXPMAP_PS)},
    {"post/lensflare_composite.jsl", Bytes(POST_LENSFLARE_COMPOSITE_PS)},
    {"post/levels.jsl", Bytes(POST_LEVELS_PS)},
    {"post/linear.jsl", Bytes(POST_LINEAR_PS)},
    {"post/max.jsl", Bytes(POST_MAX_PS)},
    {"post/median.jsl", Bytes(POST_MEDIAN_PS)},
    {"post/min.jsl", Bytes(POST_MIN_PS)},
    {"post/multiply.jsl", Bytes(POST_MULTIPLY_PS)},
    {"post/power.jsl", Bytes(POST_POWER_PS)},
    {"post/samplecubic.jsl", Bytes(POST_SAMPLECUBIC_PS)},
    {"post/saturate.jsl", Bytes(POST_SATURATE_PS)},
    {"post/vignette.jsl", Bytes(POST_VIGNETTE_PS)},
    {"pulse_head.jsl", Bytes(PULSE_HEAD_PS)},
    {"pulse_tail.jsl", Bytes(PULSE_TAIL_PS)},
    {"rail.jsl", Bytes(RAIL_PS)},
    {"shield.jsl", Bytes(SHIELD_PS)},
    {"shield_explosion.jsl", Bytes(SHIELD_EXPLOSION_PS)},
    {"skybox.jsl", Bytes(SKYBOX_PS)},
    {"smaa_1.jsl", Bytes(SMAA1_PS)},
    {"smaa_2.jsl", Bytes(SMAA2_PS)},
    {"smaa_3.jsl", Bytes(SMAA3_PS)},
    {"starbg.jsl", Bytes(STARBG_PS)},
    {"thruster_trail.jsl", Bytes(THRUSTER_TRAIL_PS)},
    {"trail.jsl", Bytes(TRAIL_PS)},
    {"transferbeam.jsl", Bytes(TRANSFERBEAM_PS)},
    {"tree.jsl", Bytes(TREE_PS)},
    {"wormhole.jsl", Bytes(WORMHOLE_PS)},
  };

  bool registered = (ShaderRegistry_Add(kVertex, kPixel, {}), true);
}
