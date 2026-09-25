#!/usr/bin/env python3
"""Check the build shape and the project registries without a build (AGENTS.md §1-§5).

Usage: python Build/CheckProjectFiles.py [--root DIR]

What no compiler checks, read from the files themselves:

  Solution        Every .vcxproj in the tree is in the solution, every project the solution names
                  exists, and the solution's platforms are x64 and ARM64 only (§3, ADR-001, ADR-006).
  Settings (§3)   Toolset v145, /std:c++latest, /permissive-, /W4 with warnings as errors,
                  /fp:precise, and /arch stated for each platform (R16), in every configuration.
                  Debug and Release differ only in the optimisation settings §3 lists.
  Platforms       x64 only, except the projects ADR-006 lets build for ARM64 as well (N1).
  Include path    A project does not put its own folder on it (§3), and defines none of the
                  Windows macro family (§4).
  Registration    Every source, header and shader in a project's folder is in its .vcxproj and
                  its .filters, and every file they list exists (§2).
  Layout          Project folders are flat; shaders sit in Shaders/ only (§2). Files are
                  PascalCase .h and .cpp, and shaders are named for their stage (R7, ADR-008).
  Names           No type carries an affix (R2), and no identifier a British spelling (R11).
  clang-tidy      .clang-tidy's HeaderFilterRegex covers every project it should lint.
  Dependencies    No NuGet package without an ADR (R14).

FrontierOutpost/ and GameData/ are the legacy import (ADR-001, ADR-004). Of the above, only the
rules ADR-001 keeps apply to them: they are in the solution, on toolset v145, with relative paths.

Exit 0 when nothing is wrong, 1 otherwise, naming each file and rule.
"""

import argparse
import pathlib
import re
import sys

from ProjectModel import (EXEMPT_PREFIXES, IsExempt, Project, SolutionFiles, SolutionPlatforms, SolutionProjects,
                          StripCode, TreeFiles)

TOOLSET = "v145"
CONFIGURATIONS = ("Debug", "Release")
# ADR-006 (the owner's N1): NeuronClient, and the tests that exercise it on an ARM64 device, build
# for ARM64 as well as x64. Every other first-party project is x64 only (AGENTS.md §3).
ARM64_PROJECTS = {"NeuronClient", "NeuronClientTests"}
# ADR-006 (N5, N10): lt's instruction sets, SSE2 on x64 and the compiler's default on ARM64, stated
# rather than inherited (R16), so no binary mixes /arch.
ARCH = {"x64": "StreamingSIMDExtensions2", "ARM64": "NotSet"}

INVARIANT = {"ClCompile.LanguageStandard": "stdcpplatest", "ClCompile.ConformanceMode": "true",
             "ClCompile.WarningLevel": "Level4", "ClCompile.TreatWarningAsError": "true",
             "ClCompile.FloatingPointModel": "Precise"}
# §3: the whole of what may differ between Debug and Release.
MAY_DIFFER = {"UseDebugLibraries", "LinkIncremental", "WholeProgramOptimization", "ClCompile.Optimization",
              "ClCompile.RuntimeLibrary", "ClCompile.FunctionLevelLinking", "ClCompile.IntrinsicFunctions",
              "ClCompile.WholeProgramOptimization", "Link.EnableCOMDATFolding", "Link.OptimizeReferences",
              "Link.LinkTimeCodeGeneration", "Lib.LinkTimeCodeGeneration"}
DEFINE = {"Debug": "_DEBUG", "Release": "NDEBUG"}
# §4: one header owns these, and no project file defines any of them.
WINDOWS_MACROS = {"NOMINMAX", "WIN32_LEAN_AND_MEAN", "VC_EXTRALEAN", "STRICT", "NOGDI", "NOSERVICE", "NOMCX",
                  "NOHELP", "NOCOMM", "NOKERNEL", "NOUSER"}

CPP = (".h", ".cpp")
NOT_CPP = (".hpp", ".hh", ".hxx", ".cc", ".cxx", ".c++", ".inl", ".ipp", ".tpp", ".c")
SHADERS = (".hlsl", ".hlsli")
# R7: the names MSBuild and the Visual Studio wizards give these.
WIZARD_FILES = {"pch.h", "pch.cpp", "framework.h", "targetver.h", "Resource.h"}
PASCAL_FILE = re.compile(r"^[A-Z][A-Za-z0-9]*\.(h|cpp)$")
SHADER_FILE = re.compile(r"^[A-Z][A-Za-z0-9]*(VS|PS|CS|GS|HS|DS)\.hlsl$|^[A-Z][A-Za-z0-9]*\.hlsli$")

# R2: no prefix and no suffix on a type name. clang-tidy can require the absence of a prefix but
# cannot see a present suffix, so this carries both.
TYPE_DECLARATION = re.compile(
    r"\b(?:class|struct|union|enum\s+class|enum\s+struct|enum|concept)\s+"
    r"(?:\[\[[^\]]*\]\]\s*|alignas\s*\([^)]*\)\s*|__declspec\s*\([^)]*\)\s*)*([A-Za-z_]\w*)")
ALIAS_DECLARATION = re.compile(r"\busing\s+([A-Za-z_]\w*)\s*=")
AFFIX = re.compile(r"^[ICSE][A-Z][a-z0-9]|(?:Base|Abstract|Impl|_t)$")
# R11: one spelling per family, the SDK's. The British half of each family R11 names, and a few
# more of the same kind.
BRITISH = re.compile(r"colour|initialis|serialis|normalis|quantis|synchronis|behaviour|neighbour|centre|grey|"
                     r"cancell|optimis|organis|recognis|visualis|minimis|maximis|customis|utilis|prioritis|"
                     r"authoris|finalis|favour|honour|analys(?!is)")
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
DRIVE_PATH = re.compile(r"(?<![A-Za-z])[A-Za-z]:[\\/]")

faults = []


def Fault(_where, _message):
  faults.append(f"{_where}: {_message}")


def CheckSolution(_root, _files):
  solutions = SolutionFiles(_root)
  if len(solutions) != 1:
    Fault(".", f"expected one .slnx at the repository root, found {[s.name for s in solutions]}")
    return []
  solution = solutions[0]
  listed = SolutionProjects(solution)
  for relative in listed:
    if not (_root / relative).is_file():
      Fault(solution.name, f"names {relative}, which does not exist")
  onDisk = [f for f in _files if f.endswith(".vcxproj") and not f.startswith(("ltheory-old-main/", "_baseline_build/"))]
  for relative in onDisk:
    if relative not in listed:
      Fault(relative, f"is not in {solution.name}, so no solution build compiles it (§3)")
  for platform in SolutionPlatforms(solution):
    if platform not in ("x64", "ARM64"):
      Fault(solution.name, f"has the platform {platform}; x64 is the only platform, and ARM64 is kept "
                           f"for FrontierOutpost and NeuronClient (§3, ADR-001, ADR-006)")
  return [relative for relative in listed if (_root / relative).is_file()]


def CheckRelativePaths(_root, _relative):
  for name in (_relative, _relative + ".filters"):
    path = _root / name
    if path.is_file():
      for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        if DRIVE_PATH.search(line):
          Fault(f"{name}:{number}", "an absolute path; every path is relative (ADR-001)")


def CheckLegacyProject(_root, _project):
  """ADR-001 keeps these rules for the legacy import: the toolset, and relative paths."""
  CheckRelativePaths(_root, _project.relative)
  for pair in _project.Configurations():
    configuration, _, platform = pair.partition("|")
    toolset = _project.Settings(configuration, platform).get("PlatformToolset")
    if toolset != TOOLSET:
      Fault(_project.relative, f"{pair} uses toolset {toolset!r}, not {TOOLSET} (ADR-001)")


def CheckProject(_root, _project, _files):
  relative = _project.relative
  CheckRelativePaths(_root, relative)
  platforms = ("x64", "ARM64") if _project.name in ARM64_PROJECTS else ("x64",)
  expected = sorted(f"{c}|{p}" for c in CONFIGURATIONS for p in platforms)
  if _project.Configurations() != expected:
    Fault(relative, f"configurations are {_project.Configurations()}, expected {expected} (§3"
                    f"{', ADR-006' if _project.name in ARM64_PROJECTS else ''})")

  settings = {(c, p): _project.Settings(c, p) for c in CONFIGURATIONS for p in platforms}
  for (configuration, platform), values in settings.items():
    pair = f"{configuration}|{platform}"
    if values.get("PlatformToolset") != TOOLSET:
      Fault(relative, f"{pair} toolset is {values.get('PlatformToolset')!r}, expected {TOOLSET} (§3)")
    for key, want in INVARIANT.items():
      if values.get(key) != want:
        Fault(relative, f"{pair} {key.split('.')[-1]} is {values.get(key)!r}, expected {want!r} (§3)")
    arch = values.get("ClCompile.EnableEnhancedInstructionSet")
    if not arch:
      Fault(relative, f"{pair} does not state EnableEnhancedInstructionSet (R16)")
    elif _project.name in ARM64_PROJECTS and arch != ARCH[platform]:
      Fault(relative, f"{pair} EnableEnhancedInstructionSet is {arch!r}, expected {ARCH[platform]!r} (ADR-006)")
    defines = {d.strip() for d in values.get("ClCompile.PreprocessorDefinitions", "").split(";") if d.strip()}
    if DEFINE[configuration] not in defines:
      Fault(relative, f"{pair} does not define {DEFINE[configuration]} (§3)")
    for macro in sorted(defines & WINDOWS_MACROS):
      Fault(relative, f"{pair} defines {macro}; one header owns the Windows macros (§4)")
    for directory in values.get("ClCompile.AdditionalIncludeDirectories", "").split(";"):
      directory = directory.strip().replace("\\", "/").rstrip("/")
      own = ("$(ProjectDir)", "$(MSBuildProjectDirectory)", "$(MSBuildThisFileDirectory)", ".",
             f"$(SolutionDir){_project.folder.relative_to(_root).as_posix()}")
      if directory in own:
        Fault(relative, f"{pair} puts the project's own folder on the include path (§3)")

  # §3: Debug and Release agree on everything that is not about optimisation.
  for platform in platforms:
    debug, release = settings[("Debug", platform)], settings[("Release", platform)]
    for key in sorted(set(debug) | set(release)):
      if debug.get(key) == release.get(key) or key in MAY_DIFFER:
        continue
      if key == "ClCompile.PreprocessorDefinitions":
        strip = lambda _values: {d.strip() for d in _values.split(";") if d.strip()} - {"_DEBUG", "NDEBUG"}
        if strip(debug.get(key, "")) == strip(release.get(key, "")):
          continue
      Fault(relative, f"Debug|{platform} and Release|{platform} disagree on {key}: "
                      f"{debug.get(key)!r} against {release.get(key)!r} (§3)")

  for fault in _project.faults:
    Fault(relative, fault)

  # §2 and R14: registration, layout and dependencies.
  folder = _project.folder.relative_to(_root).as_posix()
  items = _project.Items()
  filters = _project.FilterItems()
  if filters is None:
    Fault(relative, "has no .filters file (§2)")
    filters = {}
  listed = {path for paths in items.values() for path in paths}
  filtered = {path for paths in filters.values() for path in paths}
  for path in sorted(listed | filtered):
    if not (_project.folder / path).is_file():
      Fault(relative, f"lists {path}, which does not exist (§2)")
  for path in sorted(listed - filtered):
    Fault(relative, f"lists {path}, which its .filters does not (§2)")
  for path in sorted(filtered - listed):
    Fault(relative + ".filters", f"lists {path}, which the project does not (§2)")

  mine = [f[len(folder) + 1:] for f in _files if f.startswith(folder + "/")]
  for path in mine:
    name = path.split("/")[-1]
    parts = path.split("/")
    if name.endswith(CPP) and len(parts) > 1:
      Fault(f"{folder}/{path}", "a source or header in a subfolder; project folders are flat (§2)")
    elif name.endswith(SHADERS) and parts[:-1] != ["Shaders"]:
      Fault(f"{folder}/{path}", "a shader outside the project's Shaders/ folder (§2)")
    elif name.endswith(CPP + SHADERS) and path not in listed:
      Fault(f"{folder}/{path}", f"is not in {_project.path.name} (§2)")
    if name in ("packages.config",):
      Fault(f"{folder}/{path}", "a NuGet package is a dependency, and needs an ADR first (R14)")
  if "PackageReference" in _project.path.read_text(encoding="utf-8-sig"):
    Fault(relative, "a PackageReference is a dependency, and needs an ADR first (R14)")


def CheckNames(_root, _files):
  """R7 on every first-party file, and R2 and R11 on every first-party C++ file."""
  for relative in _files:
    if IsExempt(relative):
      continue
    name = relative.split("/")[-1]
    if name.endswith(NOT_CPP):
      Fault(relative, "C++ lives in .h and .cpp only (R7)")
      continue
    if name.endswith(CPP) and name not in WIZARD_FILES and not PASCAL_FILE.match(name):
      Fault(relative, "a C++ file is named in PascalCase for its primary type (R7)")
    if name.endswith(SHADERS) and not SHADER_FILE.match(name):
      Fault(relative, "a shader is named <Shader><Stage>.hlsl, or <Name>.hlsli (R7, ADR-008)")
    if not name.endswith(CPP):
      continue
    code = StripCode((_root / relative).read_text(encoding="utf-8-sig", errors="replace"))
    for number, line in enumerate(code.splitlines(), 1):
      for match in list(TYPE_DECLARATION.finditer(line)) + list(ALIAS_DECLARATION.finditer(line)):
        if AFFIX.search(match[1]):
          Fault(f"{relative}:{number}", f"the type name {match[1]} carries a prefix or suffix (R2)")
      for word in IDENTIFIER.findall(line):
        if BRITISH.search(word.lower()):
          Fault(f"{relative}:{number}", f"the identifier {word} uses the British spelling (R11)")


def CheckHeaderFilter(_root, _projects):
  """.clang-tidy's HeaderFilterRegex is a project registry: a project it does not match is one
    whose headers nothing lints (.clang-tidy says so)."""
  path = _root / ".clang-tidy"
  if not path.is_file():
    Fault(".clang-tidy", "missing; the naming table has no checker without it (§1)")
    return
  match = re.search(r"^HeaderFilterRegex:\s*'([^']*)'", path.read_text(encoding="utf-8"), re.M)
  if not match:
    Fault(".clang-tidy", "has no HeaderFilterRegex, so no header is linted")
    return
  pattern = re.compile(match[1])
  for project in _projects:
    folder = project.folder.name
    if not (pattern.search(f"{folder}/Probe.h") and pattern.search(f"{folder}\\Probe.h")):
      Fault(".clang-tidy", f"HeaderFilterRegex does not match {folder}/, so no header of "
                           f"{project.path.name} is linted")


def main():
  parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
  parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent)
  root = parser.parse_args().root.resolve()

  files = TreeFiles(root)
  projects = [Project(root, relative) for relative in CheckSolution(root, files)]
  legacy = [p for p in projects if p.relative.startswith(EXEMPT_PREFIXES)]
  firstParty = [p for p in projects if not p.relative.startswith(EXEMPT_PREFIXES)]
  for project in legacy:
    CheckLegacyProject(root, project)
  for project in firstParty:
    CheckProject(root, project, files)
  CheckNames(root, files)
  CheckHeaderFilter(root, firstParty)

  homes = [p.folder.relative_to(root).as_posix() + "/" for p in firstParty]
  for relative in files:
    if relative.endswith(CPP + SHADERS) and not IsExempt(relative) and not relative.startswith(tuple(homes)):
      Fault(relative, "a source, header or shader outside every project folder (§2)")

  for line in faults:
    print(line)
  print(f"{len(faults)} fault(s). Checked {len(firstParty)} first-party and {len(legacy)} legacy "
        f"project(s) (ADR-001), and {sum(1 for f in files if not IsExempt(f))} first-party file(s).")
  return 1 if faults else 0


if __name__ == "__main__":
  sys.exit(main())
