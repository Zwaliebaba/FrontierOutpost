#include "ShaderRegistry.h"

/* FXC's headers declare their bytecode as BYTE, which is windows.h's. */
typedef unsigned char BYTE;

#include "CompiledShaders/BillboardAxisVS.h"
#include "CompiledShaders/BillboardAxisWrappedVS.h"
#include "CompiledShaders/BillboardSoftVS.h"
#include "CompiledShaders/BillboardVS.h"
#include "CompiledShaders/IdentityVS.h"
#include "CompiledShaders/NpmVS.h"
#include "CompiledShaders/ParticleVS.h"
#include "CompiledShaders/SkyboxVS.h"
#include "CompiledShaders/Smaa1VS.h"
#include "CompiledShaders/Smaa2VS.h"
#include "CompiledShaders/Smaa3VS.h"
#include "CompiledShaders/TrailVS.h"
#include "CompiledShaders/WidgetTextureVS.h"
#include "CompiledShaders/WidgetVS.h"
#include "CompiledShaders/WorldrayVS.h"

#include "CompiledShaders/ComputeOcclusionPS.h"
#include "CompiledShaders/MaterialLambertPS.h"
#include "CompiledShaders/PostAddPS.h"
#include "CompiledShaders/PostBloomCompositePS.h"
#include "CompiledShaders/PostBlurPS.h"
#include "CompiledShaders/PostCircleminmaxPS.h"
#include "CompiledShaders/PostColorgrade1DPS.h"
#include "CompiledShaders/PostColorgradebezierPS.h"
#include "CompiledShaders/PostDitherPS.h"
#include "CompiledShaders/PostEdgedetectPS.h"
#include "CompiledShaders/PostExpmapPS.h"
#include "CompiledShaders/PostGlowthreshPS.h"
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
#include "CompiledShaders/PostTonemapPS.h"
#include "CompiledShaders/PostVignettePS.h"
#include "CompiledShaders/Smaa1PS.h"
#include "CompiledShaders/Smaa2PS.h"
#include "CompiledShaders/Smaa3PS.h"
#include "CompiledShaders/UiArcPS.h"
#include "CompiledShaders/UiBasicPS.h"
#include "CompiledShaders/UiBoxPS.h"
#include "CompiledShaders/UiCirclePS.h"
#include "CompiledShaders/UiCompositePS.h"
#include "CompiledShaders/UiGradientPS.h"
#include "CompiledShaders/UiGridPS.h"
#include "CompiledShaders/UiLinePS.h"
#include "CompiledShaders/UiLinefadePS.h"
#include "CompiledShaders/UiNonePS.h"
#include "CompiledShaders/UiPanelPS.h"
#include "CompiledShaders/UiRadialpanelPS.h"
#include "CompiledShaders/UiRingPS.h"
#include "CompiledShaders/UiTextPS.h"
#include "CompiledShaders/UiTexturePS.h"
#include "CompiledShaders/UiTextureadditivePS.h"
#include "CompiledShaders/UiTrianglePS.h"

/* Build/CheckProjectFiles.py reads the tables below, one entry a line, and
 * holds them to ADR-008: every shader in Shaders/ is here, under the legacy
 * name its file is named for, and with the array lt.vcxproj compiles it to. */
namespace {
  struct Entry {
    char const* name;
    std::span<std::byte const> bytecode;
  };

  template <std::size_t Count>
  std::span<std::byte const> Bytes(BYTE const (&bytecode)[Count]) {
    return std::as_bytes(std::span(bytecode));
  }

  /* Each vertex shader, by its path below GameData/shader/vertex/. */
  Entry const kVertex[] = {
    {"billboard.jsl", Bytes(BILLBOARD_VS)},
    {"billboard_axis.jsl", Bytes(BILLBOARD_AXIS_VS)},
    {"billboard_axis_wrapped.jsl", Bytes(BILLBOARD_AXIS_WRAPPED_VS)},
    {"billboard_soft.jsl", Bytes(BILLBOARD_SOFT_VS)},
    {"identity.jsl", Bytes(IDENTITY_VS)},
    {"npm.jsl", Bytes(NPM_VS)},
    {"particle.jsl", Bytes(PARTICLE_VS)},
    {"skybox.jsl", Bytes(SKYBOX_VS)},
    {"smaa_1.jsl", Bytes(SMAA1_VS)},
    {"smaa_2.jsl", Bytes(SMAA2_VS)},
    {"smaa_3.jsl", Bytes(SMAA3_VS)},
    {"trail.jsl", Bytes(TRAIL_VS)},
    {"widget.jsl", Bytes(WIDGET_VS)},
    {"widgetTexture.jsl", Bytes(WIDGET_TEXTURE_VS)},
    {"worldray.jsl", Bytes(WORLDRAY_VS)},
  };

  /* Each pixel shader, by its path below GameData/shader/fragment/. */
  Entry const kPixel[] = {
    {"compute/occlusion.jsl", Bytes(COMPUTE_OCCLUSION_PS)},
    {"material/lambert.jsl", Bytes(MATERIAL_LAMBERT_PS)},
    {"post/add.jsl", Bytes(POST_ADD_PS)},
    {"post/bloom_composite.jsl", Bytes(POST_BLOOM_COMPOSITE_PS)},
    {"post/blur.jsl", Bytes(POST_BLUR_PS)},
    {"post/circleminmax.jsl", Bytes(POST_CIRCLEMINMAX_PS)},
    {"post/colorgrade1D.jsl", Bytes(POST_COLORGRADE1D_PS)},
    {"post/colorgradebezier.jsl", Bytes(POST_COLORGRADEBEZIER_PS)},
    {"post/dither.jsl", Bytes(POST_DITHER_PS)},
    {"post/edgedetect.jsl", Bytes(POST_EDGEDETECT_PS)},
    {"post/expmap.jsl", Bytes(POST_EXPMAP_PS)},
    {"post/glowthresh.jsl", Bytes(POST_GLOWTHRESH_PS)},
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
    {"post/tonemap.jsl", Bytes(POST_TONEMAP_PS)},
    {"post/vignette.jsl", Bytes(POST_VIGNETTE_PS)},
    {"smaa_1.jsl", Bytes(SMAA1_PS)},
    {"smaa_2.jsl", Bytes(SMAA2_PS)},
    {"smaa_3.jsl", Bytes(SMAA3_PS)},
    {"ui/arc.jsl", Bytes(UI_ARC_PS)},
    {"ui/basic.jsl", Bytes(UI_BASIC_PS)},
    {"ui/box.jsl", Bytes(UI_BOX_PS)},
    {"ui/circle.jsl", Bytes(UI_CIRCLE_PS)},
    {"ui/composite.jsl", Bytes(UI_COMPOSITE_PS)},
    {"ui/gradient.jsl", Bytes(UI_GRADIENT_PS)},
    {"ui/grid.jsl", Bytes(UI_GRID_PS)},
    {"ui/line.jsl", Bytes(UI_LINE_PS)},
    {"ui/linefade.jsl", Bytes(UI_LINEFADE_PS)},
    {"ui/none.jsl", Bytes(UI_NONE_PS)},
    {"ui/panel.jsl", Bytes(UI_PANEL_PS)},
    {"ui/radialpanel.jsl", Bytes(UI_RADIALPANEL_PS)},
    {"ui/ring.jsl", Bytes(UI_RING_PS)},
    {"ui/text.jsl", Bytes(UI_TEXT_PS)},
    {"ui/texture.jsl", Bytes(UI_TEXTURE_PS)},
    {"ui/textureadditive.jsl", Bytes(UI_TEXTUREADDITIVE_PS)},
    {"ui/triangle.jsl", Bytes(UI_TRIANGLE_PS)},
  };

  template <std::size_t Count>
  std::span<std::byte const> Find(Entry const (&entries)[Count], String const& name) {
    for (Entry const& entry : entries)
      if (name == entry.name)
        return entry.bytecode;
    return {};
  }
}

std::span<std::byte const> ShaderRegistry_Vertex(String const& name) {
  return Find(kVertex, name);
}

std::span<std::byte const> ShaderRegistry_Pixel(String const& name) {
  return Find(kPixel, name);
}
