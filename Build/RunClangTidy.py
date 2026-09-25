#!/usr/bin/env python3
"""Run clang-tidy over every first-party translation unit the solution builds (AGENTS.md §1).

Usage: python Build/RunClangTidy.py [--clang-tidy PATH] [--configuration Debug] [--platform x64]
                                    [--root DIR]

For each project in the solution outside the legacy import (FrontierOutpost/, GameData/:
ADR-001, ADR-004), it reads the project's sources, include directories and definitions, and runs
clang-tidy in clang-cl mode with /.clang-tidy, whose HeaderFilterRegex decides which headers are
reported. .clang-tidy makes every warning an error, so any diagnostic fails the run.

clang-tidy comes from --clang-tidy, else from Visual Studio's C++ Clang tools, else from PATH. It
must be 19 or newer: an older one rejects .clang-tidy's ExcludeHeaderFilterRegex, checks nothing,
and still exits 0. The Windows SDK headers come from INCLUDE, as in a Developer PowerShell; when
INCLUDE is not set, as on a CI runner, this script takes it from Visual Studio's vcvarsall.bat.

Exit 0 when every translation unit is clean, or when there is none; 1 otherwise.
"""

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys

from ProjectModel import EXEMPT_PREFIXES, Project, SolutionFiles, SolutionProjects

MINIMUM_MAJOR = 19
DIAGNOSTIC = re.compile(r": (?:warning|error): ")
RUNTIME = {"MultiThreadedDLL": "/MD", "MultiThreadedDebugDLL": "/MDd", "MultiThreaded": "/MT",
           "MultiThreadedDebug": "/MTd"}
ARCH = {"AdvancedVectorExtensions": "/arch:AVX", "AdvancedVectorExtensions2": "/arch:AVX2",
        "AdvancedVectorExtensions512": "/arch:AVX512"}
TARGET = {"x64": "x86_64-pc-windows-msvc", "ARM64": "aarch64-pc-windows-msvc"}


def VisualStudio():
  vswhere = pathlib.Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / \
      "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
  if not vswhere.is_file():
    return None
  result = subprocess.run([str(vswhere), "-latest", "-prerelease", "-products", "*", "-property", "installationPath"],
                          capture_output=True, text=True)
  path = result.stdout.strip().splitlines()
  return pathlib.Path(path[0]) if path else None


def FindClangTidy(_requested):
  if _requested:
    return shutil.which(_requested) or (_requested if pathlib.Path(_requested).is_file() else None)
  studio = VisualStudio()
  if studio:
    for candidate in (studio / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clang-tidy.exe",
                      studio / "VC" / "Tools" / "Llvm" / "bin" / "clang-tidy.exe"):
      if candidate.is_file():
        return str(candidate)
  return shutil.which("clang-tidy")


def Major(_clangTidy):
  output = subprocess.run([_clangTidy, "--version"], capture_output=True, text=True).stdout
  match = re.search(r"version (\d+)\.", output)
  return int(match[1]) if match else 0


def Environment():
  """The environment for clang-tidy: INCLUDE as a Developer PowerShell would have it, on Windows."""
  environment = dict(os.environ)
  if os.name != "nt" or environment.get("INCLUDE"):
    return environment
  studio = VisualStudio()
  vcvars = studio / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat" if studio else None
  if not vcvars or not vcvars.is_file():
    print("INCLUDE is not set and vcvarsall.bat was not found: run this from a Developer PowerShell.")
    return environment
  result = subprocess.run(f'cmd /s /c ""{vcvars}" x64 >nul 2>&1 && set"', capture_output=True, text=True, shell=True)
  for line in result.stdout.splitlines():
    name, equals, value = line.partition("=")
    if equals and name:
      environment[name] = value
  print(f"INCLUDE taken from {vcvars}")
  return environment


def Expand(_value, _root, _project, _configuration, _platform, _studio=None):
  # $(VCInstallDir) is where a test project finds CppUnitTest.h (VC\Auxiliary\VS\UnitTest\include).
  if _studio is not None:
    _value = _value.replace("$(VCInstallDir)", str(_studio / "VC") + os.sep)
  return (_value.replace("$(SolutionDir)", str(_root) + os.sep)
                .replace("$(ProjectDir)", str(_project.folder) + os.sep)
                .replace("$(MSBuildProjectDirectory)", str(_project.folder))
                .replace("$(MSBuildThisFileDirectory)", str(_project.folder) + os.sep)
                .replace("$(Configuration)", _configuration)
                .replace("$(Platform)", _platform))


def Units(_root, _configuration, _platform):
  """[(source, [flags])] for every first-party translation unit."""
  units = []
  solutions = SolutionFiles(_root)
  if len(solutions) != 1:
    return units
  studio = VisualStudio()
  for relative in SolutionProjects(solutions[0]):
    if relative.startswith(EXEMPT_PREFIXES) or not (_root / relative).is_file():
      continue
    project = Project(_root, relative)
    settings = project.Settings(_configuration, _platform)
    flags = ["--driver-mode=cl", f"--target={TARGET[_platform]}", "/std:c++latest", "/permissive-", "/EHsc",
             "/W4", "/DWIN32", "/D_WINDOWS"]
    flags += [f"/D{d.strip()}" for d in settings.get("ClCompile.PreprocessorDefinitions", "").split(";")
              if d.strip() and not d.strip().startswith("%(")]
    if settings.get("ClCompile.RuntimeLibrary") in RUNTIME:
      flags.append(RUNTIME[settings["ClCompile.RuntimeLibrary"]])
    if settings.get("ClCompile.EnableEnhancedInstructionSet") in ARCH and _platform == "x64":
      flags.append(ARCH[settings["ClCompile.EnableEnhancedInstructionSet"]])
    for directory in settings.get("ClCompile.AdditionalIncludeDirectories", "").split(";"):
      directory = Expand(directory.strip(), _root, project, _configuration, _platform, studio)
      if directory and "$(" not in directory and "%(" not in directory:
        path = pathlib.Path(directory)
        flags.append(f"/I{path if path.is_absolute() else project.folder / path}")
    # cl searches the including file's own folder for a quoted include; clang-cl does the same.
    for source in project.Items().get("ClCompile", []):
      if source.split("/")[-1] == "pch.cpp":
        continue  # it creates the precompiled header and holds no code of its own
      units.append((project.folder / source, flags))
  return units


def main():
  parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
  parser.add_argument("--clang-tidy", dest="clangTidy")
  parser.add_argument("--configuration", default="Debug", choices=("Debug", "Release"))
  parser.add_argument("--platform", default="x64", choices=tuple(TARGET))
  parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent)
  args = parser.parse_args()
  root = args.root.resolve()

  units = Units(root, args.configuration, args.platform)
  if not units:
    print("No first-party translation unit to check: the solution builds only the legacy import "
          "(ADR-001), which clang-tidy leaves alone.")
    return 0

  clangTidy = FindClangTidy(args.clangTidy)
  if not clangTidy:
    print(f"clang-tidy not found, and {len(units)} translation unit(s) need it. Install Visual Studio's "
          f"'C++ Clang tools for Windows', or pass --clang-tidy.")
    return 1
  major = Major(clangTidy)
  print(f"clang-tidy {major} ({clangTidy})")
  if major < MINIMUM_MAJOR:
    print(f"clang-tidy {MINIMUM_MAJOR} or newer is needed: an older one rejects .clang-tidy and checks nothing.")
    return 1

  environment = Environment()
  failed = 0
  for source, flags in units:
    result = subprocess.run([clangTidy, "--quiet", str(source), "--", *flags], capture_output=True, text=True,
                            env=environment, cwd=root)
    output = "\n".join(line for line in (result.stdout + result.stderr).splitlines()
                       if not re.match(r"^\d+ warnings? (and \d+ errors? )?generated\.?$", line.strip()))
    if result.returncode != 0 or DIAGNOSTIC.search(output):
      failed += 1
      print(f"--- {source.relative_to(root).as_posix()} (exit {result.returncode}) ---")
      print(output.strip())
  print(f"{len(units)} translation unit(s), {failed} with diagnostics.")
  return 1 if failed else 0


if __name__ == "__main__":
  sys.exit(main())
