#include "ShaderRegistry.h"

/* FXC's headers declare their bytecode as BYTE, which is windows.h's. */
typedef unsigned char BYTE;

#include "CompiledShaders/IdentityVS.h"
#include "CompiledShaders/NpmVS.h"
#include "CompiledShaders/WidgetTextureVS.h"
#include "CompiledShaders/WidgetVS.h"

#include "CompiledShaders/ComputeOcclusionPS.h"
#include "CompiledShaders/ComputeSdffontPS.h"
#include "CompiledShaders/HologramPS.h"
#include "CompiledShaders/PostBloomCompositePS.h"
#include "CompiledShaders/PostBlurPS.h"
#include "CompiledShaders/PostGlowthreshPS.h"
#include "CompiledShaders/PostTonemapPS.h"
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

#include "CompiledShaders/GenFieldCS.h"
#include "CompiledShaders/GenFieldcopyCS.h"
#include "CompiledShaders/GenFieldocclusionCS.h"

#include <cctype>
#include <vector>

/* Build/CheckProjectFiles.py reads the tables below, one entry a line, and
 * holds them to ADR-008: every shader in Shaders/ is here, under the legacy
 * name its file is named for, and with the array lt.vcxproj compiles it to. */
namespace {
  typedef ShaderRegistryEntry Entry;

  template <std::size_t Count>
  std::span<std::byte const> Bytes(BYTE const (&bytecode)[Count]) {
    return std::as_bytes(std::span(bytecode));
  }

  /* Each vertex shader, by its GLSL's path below GameData/shader/vertex/. */
  Entry const kVertex[] = {
    {"identity.jsl", Bytes(IDENTITY_VS)},
    {"npm.jsl", Bytes(NPM_VS)},
    {"widget.jsl", Bytes(WIDGET_VS)},
    {"widgetTexture.jsl", Bytes(WIDGET_TEXTURE_VS)},
  };

  /* Each pixel shader, by its GLSL's path below GameData/shader/fragment/. */
  Entry const kPixel[] = {
    {"compute/occlusion.jsl", Bytes(COMPUTE_OCCLUSION_PS)},
    {"compute/sdffont.jsl", Bytes(COMPUTE_SDFFONT_PS)},
    {"hologram.jsl", Bytes(HOLOGRAM_PS)},
    {"post/bloom_composite.jsl", Bytes(POST_BLOOM_COMPOSITE_PS)},
    {"post/blur.jsl", Bytes(POST_BLUR_PS)},
    {"post/glowthresh.jsl", Bytes(POST_GLOWTHRESH_PS)},
    {"post/tonemap.jsl", Bytes(POST_TONEMAP_PS)},
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

  /* Each compute shader, by the GLSL path below GameData/shader/fragment/ of
     the pixel shader it replaced (Design/ADR/ADR-009). */
  Entry const kCompute[] = {
    {"gen/field.jsl", Bytes(GEN_FIELD_CS)},
    {"gen/fieldcopy.jsl", Bytes(GEN_FIELDCOPY_CS)},
    {"gen/fieldocclusion.jsl", Bytes(GEN_FIELDOCCLUSION_CS)},
  };

  /* Other projects' tables (ShaderRegistry_Add), by stage. */
  std::vector<std::span<Entry const>>& Added(int stage) {
    static std::vector<std::span<Entry const>> added[3];
    return added[stage];
  }

  template <std::size_t Count>
  std::span<std::byte const> Find(Entry const (&entries)[Count], int stage, String const& name) {
    for (Entry const& entry : entries)
      if (name == entry.name)
        return entry.bytecode;
    for (std::span<Entry const> table : Added(stage))
      for (Entry const& entry : table)
        if (name == entry.name)
          return entry.bytecode;
    return {};
  }
}

void ShaderRegistry_Add(
  std::span<ShaderRegistryEntry const> vertex,
  std::span<ShaderRegistryEntry const> pixel,
  std::span<ShaderRegistryEntry const> compute)
{
  Added(0).push_back(vertex);
  Added(1).push_back(pixel);
  Added(2).push_back(compute);
}

std::span<std::byte const> ShaderRegistry_Vertex(String const& name) {
  return Find(kVertex, 0, name);
}

std::span<std::byte const> ShaderRegistry_Pixel(String const& name) {
  return Find(kPixel, 1, name);
}

std::span<std::byte const> ShaderRegistry_Compute(String const& name) {
  return Find(kCompute, 2, name);
}

String ShaderRegistry_SourceName(String const& name, char const* stage) {
  std::size_t const end = name.ends_with(".jsl") ? name.size() - 4 : name.size();
  String source;
  bool wordStart = true;
  for (std::size_t i = 0; i < end; ++i) {
    char const c = name[i];
    if (c == '/' || c == '_') {
      wordStart = true;
      continue;
    }
    source += wordStart ? (char)std::toupper((unsigned char)c) : c;
    wordStart = false;
  }
  return source + stage + ".hlsl";
}
