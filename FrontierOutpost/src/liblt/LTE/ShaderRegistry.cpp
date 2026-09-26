#include "ShaderRegistry.h"

/* FXC's headers declare their bytecode as BYTE, which is windows.h's. */
typedef unsigned char BYTE;

#include "CompiledShaders/IdentityVS.h"
#include "CompiledShaders/NpmVS.h"
#include "CompiledShaders/Smaa1VS.h"
#include "CompiledShaders/WidgetTextureVS.h"
#include "CompiledShaders/WidgetVS.h"

#include "CompiledShaders/MaterialLambertPS.h"
#include "CompiledShaders/PostBlurPS.h"
#include "CompiledShaders/Smaa1PS.h"
#include "CompiledShaders/UiTexturePS.h"

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
    {"identity.jsl", Bytes(IDENTITY_VS)},
    {"npm.jsl", Bytes(NPM_VS)},
    {"smaa_1.jsl", Bytes(SMAA1_VS)},
    {"widget.jsl", Bytes(WIDGET_VS)},
    {"widgetTexture.jsl", Bytes(WIDGET_TEXTURE_VS)},
  };

  /* Each pixel shader, by its path below GameData/shader/fragment/. */
  Entry const kPixel[] = {
    {"material/lambert.jsl", Bytes(MATERIAL_LAMBERT_PS)},
    {"post/blur.jsl", Bytes(POST_BLUR_PS)},
    {"smaa_1.jsl", Bytes(SMAA1_PS)},
    {"ui/texture.jsl", Bytes(UI_TEXTURE_PS)},
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
