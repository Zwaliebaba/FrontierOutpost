#ifndef LTE_ShaderRegistry_h__
#define LTE_ShaderRegistry_h__

/* The shaders FXC compiled into lt.dll, by the names Shader_Create is given:
 * a path below GameData/shader/vertex/ or fragment/, such as "post/blur.jsl".
 * Design/ADR/ADR-008 names each file for its legacy path, and
 * Build/CheckProjectFiles.py holds the table to that rule. A name the table
 * does not hold returns an empty span. Nothing reads them until the renderer
 * moves onto NeuronClient (Design/Plan/NeuronClient-migration.md, Phase 4
 * step 3). */

#include "String.h"

#include <cstddef>
#include <span>

std::span<std::byte const> ShaderRegistry_Vertex(String const& name);
std::span<std::byte const> ShaderRegistry_Pixel(String const& name);

#endif
