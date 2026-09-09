#!/usr/bin/env python3
"""The rules /.clang-tidy and /.clang-format cannot express, made mechanical.

/.clang-tidy owns identifiers and semantics; /.clang-format owns whitespace. Between them they
still cannot see three kinds of defect, and this script is where those live (AGENTS.md 1, 3, 6):

  1. BUILD SHAPE      Every project builds Debug|x64 and Release|x64 and nothing else, and the
                      two configurations agree on every setting that is not allowed to differ.
                      This is the check that keeps "Release is aligned with Debug" true after
                      the commit that made it true. A Release that quietly drops an include
                      directory or a language standard is a Release nobody builds until a
                      release is wanted.
  2. REGISTRATION     Every hand-written source file is listed in its project's .vcxproj AND its
                      .filters, with the exact on-disk spelling, and every listed file exists.
                      MSVC resolves includes case-insensitively and MSBuild silently ignores a
                      file it was never told about, so a half-done move builds locally and fails
                      only in CI -- or worse, links a stale object nobody notices.
  3. NAMING           R2's banned type affixes, R7's file naming and R11's standing spellings.
                      clang-tidy can require an absent prefix but cannot ban a present suffix,
                      and it never looks at a file name at all.

Usage:
    python Build/CheckProjectFiles.py
"""

from __future__ import annotations

import os
import re
import sys
import xml.etree.ElementTree as ElementTree

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MSBUILD_NAMESPACE = "http://schemas.microsoft.com/developer/msbuild/2003"
NS = {"msb": MSBUILD_NAMESPACE}

# name -> directory, relative to the repository root. AGENTS.md 2 is the prose version of this.
PROJECTS = {
    "NeuronCore": "NeuronCore",
    "NeuronClient": "NeuronClient",
    "NeuronServer": "NeuronServer",
    "GameLogic": "GameLogic",
    "FrontierOutpost": "FrontierOutpost",
    "NeuronCoreTests": os.path.join("Tests", "NeuronCoreTests"),
    "NeuronClientTests": os.path.join("Tests", "NeuronClientTests"),
    "NeuronServerTests": os.path.join("Tests", "NeuronServerTests"),
    "GameLogicTests": os.path.join("Tests", "GameLogicTests"),
}

REQUIRED_CONFIGURATIONS = {"Debug|x64", "Release|x64"}

# Settings that MUST read identically in Debug and Release. The point of the list is that these
# are the ones whose divergence does not show up as a build failure until much later: a Release
# on an older language standard, or without an include directory, compiles happily right up until
# the first file that needs either.
ALIGNED_SETTINGS = (
    "LanguageStandard",
    "ConformanceMode",
    "WarningLevel",
    "TreatWarningAsError",
    "ExternalWarningLevel",
    "TreatAngleIncludeAsExternal",
    "SDLCheck",
    "FloatingPointModel",
    "EnableEnhancedInstructionSet",
    "MultiProcessorCompilation",
    "AdditionalIncludeDirectories",
    "PrecompiledHeader",
    "PrecompiledHeaderFile",
)

# Settings whose whole purpose is to differ between the two configurations.
EXPECTED_TO_DIFFER = {"Optimization", "PreprocessorDefinitions", "FunctionLevelLinking", "IntrinsicFunctions"}

# R7 allows these exact names even though they are not PascalCase: MSBuild and the Visual Studio
# wizards spell them this way and renaming them buys nothing.
FILENAME_EXEMPTIONS = {"pch.h", "pch.cpp", "framework.h", "targetver.h", "Resource.h"}

GENERATED_FILES = {os.path.join("FrontierOutpost", "Resource.h")}

# R2: a type name carries no prefix and no affix. clang-tidy's AbstractClassPrefix can require an
# absent prefix; nothing in it can see a present suffix, so `TransportBase` slips through.
BANNED_TYPE_PREFIX = re.compile(r"\b(?:class|struct|enum\s+class|using)\s+(?:[ICSE][A-Z]\w*)\b")
BANNED_TYPE_SUFFIX = re.compile(r"\b(?:class|struct|enum\s+class|using)\s+(\w*(?:Base|Abstract|Impl|_t))\b")

# R11: one spelling per family, and it is the one the Windows SDK and DirectX already use. Mixing
# `Colour` into code that calls D3D12_CLEAR_VALUE::Color is the actual defect -- not that either
# spelling is wrong, but that a reader then has to know which half of the tree they are in, and a
# grep for one finds half the uses. Prose in comments is not checked; identifiers are.
BANNED_SPELLINGS = {
    "colour": "color",
    "initialise": "initialize",
    "serialise": "serialize",
    "normalise": "normalize",
    "quantise": "quantize",
    "synchronise": "synchronize",
    "behaviour": "behavior",
    "neighbour": "neighbor",
    "centre": "center",
    "grey": "gray",
    "cancelled": "canceled",
}
BANNED_SPELLING_PATTERN = re.compile("|".join(BANNED_SPELLINGS), re.IGNORECASE)

SKIP_DIRECTORIES = {".git", ".vs", "x64", "packages", "__pycache__"}

problems: list[str] = []


def fail(message: str) -> None:
    problems.append(message)


def strip_comments(text: str) -> str:
    """Blank out comments and string literals so a naming check does not fire on prose."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"//[^\n]*", " ", text)
    text = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    return text


# ---------------------------------------------------------------------------- 1. build shape


def item_definitions(tree: ElementTree.ElementTree, configuration: str) -> dict[str, str]:
    """The ClCompile settings a project states for one configuration."""
    condition = f"'$(Configuration)|$(Platform)'=='{configuration}'"
    settings: dict[str, str] = {}
    for group in tree.iter(f"{{{MSBUILD_NAMESPACE}}}ItemDefinitionGroup"):
        if group.get("Condition", "").replace(" ", "") != condition.replace(" ", ""):
            continue
        for compile_element in group.findall("msb:ClCompile", NS):
            for setting in compile_element:
                name = setting.tag.split("}")[-1]
                settings[name] = (setting.text or "").strip()
    return settings


def check_build_shape(name: str, project_path: str, tree: ElementTree.ElementTree) -> None:
    declared = {
        element.get("Include", "")
        for element in tree.iter(f"{{{MSBUILD_NAMESPACE}}}ProjectConfiguration")
    }
    if declared != REQUIRED_CONFIGURATIONS:
        fail(f"{project_path}: declares configurations {sorted(declared)}; x64 is the only platform "
             f"and Debug|x64 + Release|x64 the only configurations (AGENTS.md 3).")

    debug = item_definitions(tree, "Debug|x64")
    release = item_definitions(tree, "Release|x64")
    if not debug or not release:
        fail(f"{project_path}: missing a ClCompile ItemDefinitionGroup for Debug|x64 or Release|x64.")
        return

    for setting in ALIGNED_SETTINGS:
        in_debug = debug.get(setting)
        in_release = release.get(setting)
        if in_debug != in_release:
            fail(f"{project_path}: {setting} differs between configurations "
                 f"(Debug={in_debug!r}, Release={in_release!r}). Release must be aligned with Debug.")

    # PreprocessorDefinitions is allowed to differ, but only by _DEBUG vs NDEBUG.
    def normalise_defines(value: str) -> set[str]:
        return {d for d in value.split(";") if d and d not in ("_DEBUG", "NDEBUG")}

    if normalise_defines(debug.get("PreprocessorDefinitions", "")) != normalise_defines(release.get("PreprocessorDefinitions", "")):
        fail(f"{project_path}: PreprocessorDefinitions differ by more than _DEBUG/NDEBUG "
             f"(Debug={debug.get('PreprocessorDefinitions')!r}, Release={release.get('PreprocessorDefinitions')!r}).")

    for setting in ("PlatformToolset",):
        values = {
            (element.text or "").strip()
            for element in tree.iter(f"{{{MSBUILD_NAMESPACE}}}{setting}")
        }
        if len(values) > 1:
            fail(f"{project_path}: {setting} is spelled more than one way: {sorted(values)}.")

    unknown_differences = (set(debug) ^ set(release)) - EXPECTED_TO_DIFFER
    if unknown_differences:
        fail(f"{project_path}: {sorted(unknown_differences)} is set in one configuration and not the other. "
             f"Either set it in both or add it to EXPECTED_TO_DIFFER with a reason.")


def check_solution() -> None:
    solution = os.path.join(REPO_ROOT, "FrontierOutpost.slnx")
    with open(solution, "r", encoding="utf-8-sig") as handle:
        text = handle.read()
    platforms = re.findall(r'<Platform\s+Name="([^"]+)"', text)
    if platforms != ["x64"]:
        fail(f"FrontierOutpost.slnx: declares platforms {platforms}; x64 is the only one (AGENTS.md 3).")
    for name, directory in PROJECTS.items():
        expected = f'{directory}/{name}.vcxproj'.replace("\\", "/")
        if expected not in text:
            fail(f"FrontierOutpost.slnx: does not list {expected}.")


# ---------------------------------------------------------------------------- 2. registration


def registered_files(path: str) -> set[str]:
    tree = ElementTree.parse(path)
    names: set[str] = set()
    for tag in ("ClCompile", "ClInclude", "ResourceCompile", "None", "Image"):
        for element in tree.iter(f"{{{MSBUILD_NAMESPACE}}}{tag}"):
            include = element.get("Include")
            if include:
                names.add(include.replace("/", "\\"))
    return names


def check_registration(name: str, directory: str) -> None:
    absolute = os.path.join(REPO_ROOT, directory)
    project_file = os.path.join(absolute, f"{name}.vcxproj")
    filters_file = project_file + ".filters"

    in_project = registered_files(project_file)
    in_filters = registered_files(filters_file) if os.path.exists(filters_file) else set()
    if not os.path.exists(filters_file):
        fail(f"{directory}: has no {name}.vcxproj.filters.")

    on_disk = {
        entry
        for entry in os.listdir(absolute)
        if os.path.isfile(os.path.join(absolute, entry)) and entry.endswith((".cpp", ".h", ".rc", ".ico"))
    }

    for entry in sorted(on_disk):
        if entry not in in_project:
            fail(f"{directory}\\{entry}: on disk but not in {name}.vcxproj. "
                 f"A file MSBuild was never told about compiles for nobody.")
        elif entry not in in_filters:
            fail(f"{directory}\\{entry}: in {name}.vcxproj but not in {name}.vcxproj.filters.")

    for entry in sorted(in_project | in_filters):
        if "\\" in entry:  # a path outside the project folder; AGENTS.md 3 forbids it, checked below
            continue
        if entry not in on_disk:
            fail(f"{directory}: {name}.vcxproj lists {entry}, which is not on disk with that exact spelling. "
                 f"MSVC resolves includes case-insensitively, so a wrong case still builds here and fails elsewhere.")


def check_flat_directories(name: str, directory: str) -> None:
    """AGENTS.md 3: source lives directly in its project folder, one level, no subdirectories."""
    absolute = os.path.join(REPO_ROOT, directory)
    for entry in sorted(os.listdir(absolute)):
        path = os.path.join(absolute, entry)
        if not os.path.isdir(path) or entry in SKIP_DIRECTORIES:
            continue
        for _, _, files in os.walk(path):
            for candidate in files:
                if candidate.endswith((".cpp", ".h")):
                    fail(f"{directory}\\{entry}: holds C++ source. Project folders are flat (AGENTS.md 3); "
                         f"/.clang-tidy's HeaderFilterRegex only sees headers one level in.")
                    break
            else:
                continue
            break


# ---------------------------------------------------------------------------- 3. naming


def check_file_name(relative: str) -> None:
    name = os.path.basename(relative)
    if name in FILENAME_EXEMPTIONS:
        return
    stem, extension = os.path.splitext(name)
    if extension not in (".cpp", ".h"):
        fail(f"{relative}: R7 -- C++ source is .h or .cpp; .hpp, .cc and .inl are not used here.")
        return
    if not re.fullmatch(r"[A-Z][A-Za-z0-9]*", stem):
        fail(f"{relative}: R7 -- a file is named for its primary type, PascalCase.")


def check_source_text(relative: str, text: str) -> None:
    code = strip_comments(text)

    for match in BANNED_TYPE_PREFIX.finditer(code):
        fail(f"{relative}: R2 -- {match.group(0)!r} carries a type prefix. "
             f"An interface is Transport, not ITransport.")
    for match in BANNED_TYPE_SUFFIX.finditer(code):
        fail(f"{relative}: R2 -- {match.group(1)!r} carries a banned affix (Base/Abstract/Impl/_t). "
             f"Name the concept and let the concrete types say what they are.")
    for match in BANNED_SPELLING_PATTERN.finditer(code):
        found = match.group(0)
        preferred = BANNED_SPELLINGS[found.lower()]
        fail(f"{relative}: R11 -- {found!r} in code; this tree spells that family {preferred!r}, "
             f"the spelling the Windows SDK and DirectX already use.")


def check_sources() -> None:
    for directory, subdirectories, files in os.walk(REPO_ROOT):
        subdirectories[:] = [d for d in subdirectories if d not in SKIP_DIRECTORIES]
        for name in sorted(files):
            if not name.endswith((".cpp", ".h")):
                continue
            relative = os.path.relpath(os.path.join(directory, name), REPO_ROOT)
            if relative in GENERATED_FILES:
                continue
            check_file_name(relative)
            with open(os.path.join(directory, name), "r", encoding="utf-8-sig") as handle:
                check_source_text(relative, handle.read())


# ---------------------------------------------------------------------------- driver


def main() -> int:
    for name, directory in PROJECTS.items():
        project_file = os.path.join(REPO_ROOT, directory, f"{name}.vcxproj")
        if not os.path.exists(project_file):
            fail(f"{directory}: no {name}.vcxproj.")
            continue
        tree = ElementTree.parse(project_file)
        check_build_shape(name, os.path.join(directory, f"{name}.vcxproj"), tree)
        check_registration(name, directory)
        check_flat_directories(name, directory)

    check_solution()
    check_sources()

    if problems:
        print(f"CheckProjectFiles: {len(problems)} problem(s)\n")
        for problem in problems:
            print(f"  {problem}")
        return 1

    print(f"CheckProjectFiles: {len(PROJECTS)} projects, build shape and registration clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
