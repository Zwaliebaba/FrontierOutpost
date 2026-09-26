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
  Shaders         Each compiles as the stage its name ends in, at Shader Model 5.1, into a header
                  in CompiledShaders/ whose array its name spells in UPPER_CASE (R3, ADR-008).
  Names           No type carries an affix (R2), and no identifier a British spelling (R11).
  clang-tidy      .clang-tidy's HeaderFilterRegex covers every project it should lint.
  Dependencies    No NuGet package without an ADR (R14).

FrontierOutpost/ and GameData/ are the legacy import (ADR-001, ADR-004). Of the above, only the
rules ADR-001 keeps apply to them: they are in the solution, on toolset v145, with relative paths.
liblt's Shaders/ folder is new code, carved out of that (N11): the rules for shaders apply to it in
lt.vcxproj, and LTE/ShaderRegistry.cpp must hold each of its shaders under the legacy name the
file is named for, and nothing else (ADR-008).

Exit 0 when nothing is wrong, 1 otherwise, naming each file and rule.
"""

import argparse
import pathlib
import re
import sys

from ProjectModel import (CARVED_OUT, EXEMPT_PREFIXES, IsExempt, Project, SolutionFiles, SolutionPlatforms,
                          SolutionProjects, StripCode, TreeFiles)

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
# ADR-008: the stage a shader's name ends in, as FxCompile's ShaderType spells it, and the one
# Shader Model every shader compiles at.
STAGES = {"VS": "Vertex", "PS": "Pixel", "CS": "Compute", "GS": "Geometry", "HS": "Hull", "DS": "Domain"}
SHADER_MODEL = "5.1"
SHADER_HEADER = "$(ProjectDir)CompiledShaders/%(Filename).h"
# ADR-008: the registry of each carved-out shader folder, and the form of its tables, one entry a
# line: Entry const kPixel[] = { {"post/blur.jsl", Bytes(POST_BLUR_PS)}, ... };
REGISTRIES = {"FrontierOutpost/src/liblt/Shaders/": "FrontierOutpost/src/liblt/LTE/ShaderRegistry.cpp"}
REGISTRY_TABLE = re.compile(r"\bEntry const k(Vertex|Pixel|Compute)\[\] = \{(.*?)\};", re.S)
REGISTRY_ENTRY = re.compile(r'\{"([^"]+)", Bytes\((\w+)\)\},?')
TABLE_STAGES = {"Vertex": "VS", "Pixel": "PS", "Compute": "CS"}

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
  CheckShaderItems(_project, settings, "Shaders")

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


def ShaderVariable(_stem):
  """R3: the array FXC writes is a compile-time constant, so it is its file's name in UPPER_CASE."""
  return re.sub(r"(?<=[a-z])(?=[A-Z])", "_", _stem[:-2]).upper() + "_" + _stem[-2:]


def LegacyShader(_legacy, _stage):
  """ADR-008: the file a legacy name becomes. Drop .jsl, start a word at each / and _, add the stage."""
  words = re.split(r"[/_]", _legacy.removesuffix(".jsl"))
  return "".join(word[:1].upper() + word[1:] for word in words) + _stage + ".hlsl"


def CheckShaderItems(_project, _settings, _folder):
  """ADR-008 for the FxCompile items in the project's _folder: each compiles as the stage its name
    ends in, into the array its name spells (R3), at Shader Model 5.1, into CompiledShaders/."""
  relative = _project.relative
  items = {path: metadata for path, metadata in _project.ItemMetadata("FxCompile").items()
           if path.startswith(_folder + "/")}
  for path, metadata in sorted(items.items()):
    name = path.split("/")[-1]
    if not SHADER_FILE.match(name):
      continue  # CheckNames reports it
    stem = name.removesuffix(".hlsl")
    stage = STAGES[stem[-2:]]
    if metadata.get("ShaderType") != stage:
      Fault(relative, f"compiles {path} as {metadata.get('ShaderType')!r}, where its name says {stage!r} (ADR-008)")
    if metadata.get("VariableName") != ShaderVariable(stem):
      Fault(relative, f"names the array of {path} {metadata.get('VariableName')!r}, expected "
                      f"{ShaderVariable(stem)!r} (R3)")
  if not items:
    return
  for (configuration, platform), values in _settings.items():
    pair = f"{configuration}|{platform}"
    if values.get("FxCompile.ShaderModel") != SHADER_MODEL:
      Fault(relative, f"{pair} compiles shaders at Shader Model {values.get('FxCompile.ShaderModel')!r}, "
                      f"expected {SHADER_MODEL!r} (ADR-008)")
    header = values.get("FxCompile.HeaderFileOutput", "").replace("\\", "/")
    if header != SHADER_HEADER:
      Fault(relative, f"{pair} writes each shader's header to {header!r}, expected {SHADER_HEADER!r} (§2, ADR-008)")


def CheckRegistry(_root, _project, _folder, _registry):
  """ADR-008: the registry holds every shader in the carved-out _folder, under the legacy name the
    file is named for and with the array its FxCompile item writes, and holds nothing else."""
  path = _root / _registry
  if not path.is_file():
    Fault(_registry, f"is missing, so nothing holds the shaders of {_folder} by their legacy names (ADR-008)")
    return
  text = path.read_text(encoding="utf-8-sig")
  folder = _folder.rstrip("/").split("/")[-1]
  items = {p: metadata for p, metadata in _project.ItemMetadata("FxCompile").items() if p.startswith(folder + "/")}
  registered = set()
  tables = list(REGISTRY_TABLE.finditer(text))
  if not tables:
    Fault(_registry, "has no table the checker can read (ADR-008)")
  for table in tables:
    stage = TABLE_STAGES[table[1]]
    names = set()
    for number, line in enumerate(table[2].split("\n"), text.count("\n", 0, table.start(2)) + 1):
      line = line.strip()
      if not line or line.startswith(("//", "/*", "*")):
        continue
      where = f"{_registry}:{number}"
      entry = REGISTRY_ENTRY.fullmatch(line)
      if not entry:
        Fault(where, f"a line of k{table[1]} that is not one entry the checker can read (ADR-008)")
        continue
      legacy, variable = entry[1], entry[2]
      shader = f"{folder}/{LegacyShader(legacy, stage)}"
      if legacy in names:
        Fault(where, f"holds {legacy} twice")
      names.add(legacy)
      if shader not in items:
        Fault(where, f"holds {legacy}, but {_project.path.name} compiles no {shader} (ADR-008)")
        continue
      registered.add(shader)
      if variable != items[shader].get("VariableName"):
        Fault(where, f"holds {legacy} as {variable}, where {shader} compiles to "
                     f"{items[shader].get('VariableName')!r} (ADR-008)")
  for shader in sorted(set(items) - registered):
    Fault(_folder + shader.split("/")[-1], f"is not in {_registry} under its legacy name (ADR-008)")


def CheckCarvedOut(_root, _projects, _files):
  """N11: a shader folder carved out of the legacy import keeps §2's rules and ADR-008's in the
    project that owns it, though the rest of that project stays exempt."""
  for carved in CARVED_OUT:
    parent, _, folder = carved.rstrip("/").rpartition("/")
    mine = [f for f in _files if f.startswith(carved) and not IsExempt(f)]
    owner = next((p for p in _projects if p.folder == _root / parent), None)
    if owner is None:
      if mine:
        Fault(carved, "is in no project of the solution (N11)")
      continue
    items = owner.Items()
    filters = owner.FilterItems() or {}
    listed = {p for paths in items.values() for p in paths if p.startswith(folder + "/")}
    filtered = {p for paths in filters.values() for p in paths if p.startswith(folder + "/")}
    for path in sorted(listed | filtered):
      if not (owner.folder / path).is_file():
        Fault(owner.relative, f"lists {path}, which does not exist (§2)")
    for path in sorted(listed - filtered):
      Fault(owner.relative, f"lists {path}, which its .filters does not (§2)")
    for path in sorted(filtered - listed):
      Fault(owner.relative + ".filters", f"lists {path}, which the project does not (§2)")
    for relative in mine:
      name = relative[len(carved):]
      if "/" in name:
        Fault(relative, f"a file in a subfolder of {carved}, which is flat (§2)")
      elif name.endswith(CPP):
        Fault(relative, f"C++ in {carved}, which holds shaders (§2)")
      elif name.endswith(".hlsl") and f"{folder}/{name}" not in items.get("FxCompile", []):
        Fault(relative, f"is not an FxCompile item of {owner.path.name} (§2, ADR-008)")
      elif name.endswith(".hlsli") and f"{folder}/{name}" not in listed:
        Fault(relative, f"is not in {owner.path.name} (§2)")

    # §3 and ADR-008: how the folder's shaders compile, the same in Debug and Release.
    settings = {tuple(pair.split("|")): owner.Settings(*pair.split("|")) for pair in owner.Configurations()}
    CheckShaderItems(owner, settings, folder)
    for platform in sorted({platform for _, platform in settings}):
      debug, release = settings.get(("Debug", platform), {}), settings.get(("Release", platform), {})
      for key in sorted(k for k in set(debug) | set(release) if k.startswith("FxCompile.")):
        if debug.get(key) != release.get(key):
          Fault(owner.relative, f"Debug|{platform} and Release|{platform} disagree on {key}: "
                                f"{debug.get(key)!r} against {release.get(key)!r} (§3)")
    CheckRegistry(_root, owner, carved, REGISTRIES[carved])


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
  CheckCarvedOut(root, legacy, files)
  CheckNames(root, files)
  CheckHeaderFilter(root, firstParty)

  homes = [p.folder.relative_to(root).as_posix() + "/" for p in firstParty] + list(CARVED_OUT)
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
