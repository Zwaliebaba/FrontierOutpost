#!/usr/bin/env python3
"""Run clang-tidy over every hand-written translation unit, with the switches the build uses.

/.clang-tidy is the rule set (AGENTS.md §1). This script is the invocation, and it exists so the
invocation is derived rather than retyped: it reads each .vcxproj's Debug|x64 settings and hands
clang-tidy the same include directories and preprocessor definitions MSVC gets. A hand-written
command line in a YAML file drifts from the projects within a release or two, and the failure mode
is silent -- clang-tidy stops seeing a header, reports nothing, and the gate goes quietly green.

WHY MSVC DRIVER MODE. clang-tidy is pointed at the code through `--driver-mode=cl`. In GNU driver
mode it disagrees with MSVC in both directions against the Windows SDK, and the findings it
invents are more expensive than the ones it would have caught.

WHY UNICODE IS PASSED EXPLICITLY. Every project sets CharacterSet=Unicode, which MSBuild turns
into /DUNICODE /D_UNICODE. Without them `IDC_ARROW` is `MAKEINTRESOURCEA` and clang-tidy reports a
type error at every `LoadCursorW` that MSVC compiles without complaint.

Requires the MSVC environment (a Developer PowerShell, or the vcvars step CI runs) so that INCLUDE
points at the CRT and the Windows SDK.

Usage:
    python Build/RunClangTidy.py                  # the whole tree
    python Build/RunClangTidy.py NeuronCore       # one project
    python Build/RunClangTidy.py --clang-tidy <path>
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MSBUILD_NAMESPACE = "http://schemas.microsoft.com/developer/msbuild/2003"
NS = {"msb": MSBUILD_NAMESPACE}

PROJECTS = {
    "NeuronCore": "NeuronCore",
    "NeuronClient": "NeuronClient",
    "NeuronServer": "NeuronServer",
    "GameLogic": "GameLogic",
    "Lockstep": "Lockstep",
    "NeuronCoreTests": os.path.join("Tests", "NeuronCoreTests"),
    "NeuronClientTests": os.path.join("Tests", "NeuronClientTests"),
    "NeuronServerTests": os.path.join("Tests", "NeuronServerTests"),
    "GameLogicTests": os.path.join("Tests", "GameLogicTests"),
    "LockstepTests": os.path.join("Tests", "LockstepTests"),
}

VSWHERE = r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"


def visual_cpp_directory() -> str:
    """$(VCInstallDir), which the .vcxproj files use to find the CppUnitTest headers."""
    from_environment = os.environ.get("VCINSTALLDIR") or os.environ.get("VCInstallDir")
    if from_environment:
        return from_environment
    if os.path.isfile(VSWHERE):
        installation = subprocess.run(
            [VSWHERE, "-latest", "-products", "*", "-property", "installationPath"],
            capture_output=True, text=True, check=False,
        ).stdout.strip()
        if installation:
            return os.path.join(installation, "VC") + os.sep
    return ""


def expand(value: str, project_directory: str, vc_directory: str) -> str:
    # $(SolutionDir) is the repository root. MSBuild only defines it for a solution build, which is
    # why the projects must be built through Lockstep.slnx (AGENTS.md §3); here it is simply
    # known, so this script does not care how the last build was invoked.
    value = value.replace("$(SolutionDir)", REPO_ROOT + os.sep)
    value = value.replace("$(MSBuildThisFileDirectory)", project_directory + os.sep)
    value = value.replace("$(VCInstallDir)", vc_directory)
    return value


def debug_settings(project_file: str) -> dict[str, str]:
    tree = ElementTree.parse(project_file)
    condition = "'$(Configuration)|$(Platform)'=='Debug|x64'".replace(" ", "")
    settings: dict[str, str] = {}
    for group in tree.iter(f"{{{MSBUILD_NAMESPACE}}}ItemDefinitionGroup"):
        if group.get("Condition", "").replace(" ", "") != condition:
            continue
        for compile_element in group.findall("msb:ClCompile", NS):
            for setting in compile_element:
                settings[setting.tag.split("}")[-1]] = (setting.text or "").strip()
    return settings


def translation_units(project_file: str) -> list[str]:
    tree = ElementTree.parse(project_file)
    units = []
    for element in tree.iter(f"{{{MSBUILD_NAMESPACE}}}ClCompile"):
        include = element.get("Include")
        if not include or not include.endswith(".cpp"):
            continue

        # A .cpp borrowed from another project is checked where it lives, not here. LockstepTests
        # compiles four of Lockstep's own translation units a second time (AGENTS.md 2); linting
        # them twice would double every finding in them and report the same line under two
        # different projects.
        if include.startswith(os.pardir) or os.pardir + os.sep in include or os.pardir + "/" in include:
            continue

        units.append(include.replace("/", os.sep))
    return units


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("projects", nargs="*", default=[], help="projects to check (default: all)")
    parser.add_argument("--clang-tidy", dest="binary", default=None)
    arguments = parser.parse_args()

    binary = shutil.which(arguments.binary or "clang-tidy")
    if not binary:
        sys.exit("RunClangTidy: no clang-tidy on PATH. Pass --clang-tidy <path>.")

    version = subprocess.run([binary, "--version"], capture_output=True, text=True, check=True).stdout
    match = re.search(r"version (\S+)", version)
    print(f"RunClangTidy: {binary} ({match.group(1) if match else 'unknown version'})")
    if not os.environ.get("INCLUDE"):
        print("RunClangTidy: WARNING - INCLUDE is not set. Run this from a Developer PowerShell, "
              "or clang will not find the CRT or the Windows SDK.")

    vc_directory = visual_cpp_directory()
    selected = arguments.projects or list(PROJECTS)

    failures: list[str] = []
    checked = 0
    for name in selected:
        if name not in PROJECTS:
            sys.exit(f"RunClangTidy: unknown project {name!r}. Known: {', '.join(PROJECTS)}")
        directory = os.path.join(REPO_ROOT, PROJECTS[name])
        project_file = os.path.join(directory, f"{name}.vcxproj")
        settings = debug_settings(project_file)

        flags = ["--driver-mode=cl", "/std:c++latest", "/EHsc", "/DUNICODE", "/D_UNICODE"]
        for define in settings.get("PreprocessorDefinitions", "").split(";"):
            if define and not define.startswith("%("):
                flags.append(f"/D{define}")
        for directory_entry in settings.get("AdditionalIncludeDirectories", "").split(";"):
            if not directory_entry or directory_entry.startswith("%("):
                continue
            flags.append("/I" + os.path.normpath(expand(directory_entry, directory, vc_directory)))

        for unit in translation_units(project_file):
            path = os.path.join(directory, unit)
            checked += 1
            result = subprocess.run([binary, "--quiet", path, "--"] + flags, cwd=REPO_ROOT)
            if result.returncode != 0:
                failures.append(os.path.relpath(path, REPO_ROOT))

    print(f"RunClangTidy: checked {checked} translation units, {len(failures)} with findings")
    for failure in failures:
        print(f"  {failure}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
