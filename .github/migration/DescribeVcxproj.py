#!/usr/bin/env python3
"""Print the effective settings of CMake-generated .vcxproj files (FrontierOutpost/MIGRATION_NOTES.md).

Usage: DescribeVcxproj.py WORKSPACE BUILD_DIR [CONFIG ...]

The projects CMake's Visual Studio generator writes are the ground truth for what the original
build passes to cl.exe, link.exe and lib.exe: every CMake default, CMAKE_CXX_FLAGS edit and
target property ends up there as MSBuild metadata. For each project under BUILD_DIR (CMake's own
ALL_BUILD, ZERO_CHECK, INSTALL and PACKAGE excluded) this prints the configuration properties and
every ClCompile, ResourceCompile, Link, Lib and build-event setting, once when the configurations
agree and per configuration when they differ. Absolute paths under WORKSPACE print as <ws>.
"""

import collections
import glob
import os
import re
import sys
import xml.etree.ElementTree as ElementTree

NS = "{http://schemas.microsoft.com/developer/msbuild/2003}"
CMAKE_UTILITY_PROJECTS = {"ALL_BUILD", "ZERO_CHECK", "INSTALL", "PACKAGE", "RUN_TESTS"}
TOOLS = ("ClCompile", "ResourceCompile", "Midl", "Link", "Lib",
         "PreBuildEvent", "PreLinkEvent", "PostBuildEvent", "ProjectReference")
CONDITION = re.compile(r"'\$\(Configuration\)\|\$\(Platform\)'\s*==\s*'([^|']+)\|([^']+)'")


def Stem(_path):
    """File name without extension, whichever separator the path uses."""
    return os.path.splitext(re.split(r"[\\/]", _path)[-1])[0]


def Relative(_text, _workspace):
    text = " ".join((_text or "").split())
    for spelling in {_workspace, _workspace.replace("\\", "/"), _workspace.replace("/", "\\")}:
        if spelling:
            text = re.sub(re.escape(spelling), "<ws>", text, flags=re.I)
    return text


def ConfigurationOf(_element):
    match = CONDITION.search(_element.get("Condition", ""))
    return match.group(1) if match else None


def Describe(_path, _workspace, _configs):
    root = ElementTree.parse(_path).getroot()
    settings = collections.defaultdict(dict)  # key -> {config: value}

    for group in root.iter(NS + "PropertyGroup"):
        config = ConfigurationOf(group)
        if config not in _configs:
            continue
        label = group.get("Label") or "Properties"
        for prop in group:
            settings[f"{label}.{prop.tag[len(NS):]}"][config] = Relative(prop.text, _workspace)

    for group in root.iter(NS + "ItemDefinitionGroup"):
        config = ConfigurationOf(group)
        if config not in _configs:
            continue
        for tool in group:
            toolName = tool.tag[len(NS):]
            if toolName not in TOOLS:
                continue
            for setting in tool:
                settings[f"{toolName}.{setting.tag[len(NS):]}"][config] = Relative(setting.text, _workspace)

    sources = collections.Counter()
    customBuilds = []
    references = []
    for group in root.iter(NS + "ItemGroup"):
        for item in group:
            kind = item.tag[len(NS):]
            include = Relative(item.get("Include"), _workspace)
            if kind in ("ClCompile", "ClInclude", "ResourceCompile", "None", "Object"):
                sources[kind] += 1
            elif kind == "CustomBuild":
                customBuilds.append(include)
            elif kind == "ProjectReference":
                if Stem(include) not in CMAKE_UTILITY_PROJECTS:
                    references.append(Stem(include))

    lines = [f"--- {Stem(_path)} ({Relative(_path, _workspace)})"]
    lines.append("    items: " + ", ".join(f"{k} {n}" for k, n in sorted(sources.items())))
    if customBuilds:
        lines.append("    CustomBuild: " + "; ".join(customBuilds))
    if references:
        lines.append("    ProjectReference: " + ", ".join(sorted(set(references))))
    for key in sorted(settings):
        values = settings[key]
        present = [c for c in _configs if c in values]
        if len(present) == len(_configs) and len({values[c] for c in present}) == 1:
            lines.append(f"    {key} = {values[present[0]]}")
        else:
            for config in _configs:
                lines.append(f"    {key} [{config}] = {values.get(config, '(unset)')}")
    return lines


def main(_args):
    if len(_args) < 2:
        print(__doc__)
        return 2
    workspace, buildDir = _args[0].rstrip("\\/"), _args[1]
    configs = _args[2:] or ["Debug", "Release"]
    projects = sorted(glob.glob(os.path.join(buildDir, "**", "*.vcxproj"), recursive=True))
    projects = [p for p in projects if Stem(p) not in CMAKE_UTILITY_PROJECTS]
    if not projects:
        print(f"No .vcxproj under {buildDir}: configure did not get that far.")
        return 0
    for project in projects:
        print("\n".join(Describe(project, workspace, configs)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
