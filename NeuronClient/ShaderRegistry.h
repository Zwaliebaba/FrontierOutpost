#ifndef LTE_ShaderRegistry_h__
#define LTE_ShaderRegistry_h__

/* The shaders FXC compiled into lt.dll, by the names Shader_Create is given:
 * the legacy path, where each one's GLSL lay below GameData/shader/vertex/ or
 * fragment/ until that folder was deleted, such as "post/blur.jsl". A compute
 * shader goes by the path of the pixel shader it replaced, such as
 * "gen/field.jsl" (Design/ADR/ADR-009). Design/ADR/ADR-008 names each file for
 * its legacy path, and Build/CheckProjectFiles.py holds the table to that rule.
 * A name the table does not hold returns an empty span. */

#include "LteString.h"

#include <cstddef>
#include <span>

std::span<std::byte const> ShaderRegistry_Vertex(String const& name);
std::span<std::byte const> ShaderRegistry_Pixel(String const& name);
std::span<std::byte const> ShaderRegistry_Compute(String const& name);

/* The file in Shaders/ a name was compiled from, by ADR-008's rule, which
 * Build/CheckProjectFiles.py's LegacyShader also spells: drop .jsl, start a
 * word at each / and _, and add the stage, so "post/tonemap.jsl" and "PS" give
 * "PostTonemapPS.hlsl". */
String ShaderRegistry_SourceName(String const& name, char const* stage);

#endif
