#!/usr/bin/env python3
"""Print the tool command lines an MSBuild log records, per project (FrontierOutpost/MIGRATION_NOTES.md).

Usage: CommandLines.py [--root PATH=NAME ...] LOG

At normal verbosity MSBuild logs every command line it runs. This script extracts those of the
compiler (CL), librarian (Lib), linker (link) and resource compiler (rc), groups them by project,
and prints them in a form two builds can be compared by, switch by switch:

- the tool that ran (its host and target directories show the architecture);
- include and library directories, and the libraries linked, in their order, because order matters;
- definitions and every other switch sorted, because their order does not;
- the files compiled or linked, only as a count.

A project's CL invocations that differ only in /Fo (MSBuild batches the sources that have their
own object folder separately) are shown once. --root replaces a path prefix with NAME, so that a
checkout's location does not make two builds look different.
"""

import collections
import os
import re
import sys

TOOLS = {"cl.exe": "CL", "lib.exe": "Lib", "link.exe": "link", "rc.exe": "rc"}
# MSBuild logs the tool's path unquoted, spaces and all: "C:\Program Files\...\CL.exe /c ...".
INVOCATION = re.compile(r'^"?(?P<exe>(?:[A-Za-z]:\\[^"]*?\\)?(?:cl|lib|link|rc)\.exe)"?\s+(?P<args>/.*)$', re.I)
# Switches whose value is the next token when MSBuild writes them apart: "/D WIN32".
SPLIT_SWITCHES = {"/d", "/i", "/u", "/fi"}
ORDERED = {"CL": ("/I",), "rc": ("/I",), "link": ("/LIBPATH:",), "Lib": ("/LIBPATH:",)}
SORTED_DEFINES = ("/D ", "/U ")
# Per-invocation paths: which object folder a batch writes to.
VARYING = ("/Fo",)


def Tokenize(_line):
    """Split a Windows command line as CommandLineToArgvW does."""
    tokens, current, inQuotes, i, hasToken = [], [], False, 0, False
    while i < len(_line):
        c = _line[i]
        if c == "\\":
            run = 0
            while i < len(_line) and _line[i] == "\\":
                run += 1
                i += 1
            if i < len(_line) and _line[i] == '"':
                current.append("\\" * (run // 2))
                if run % 2:
                    current.append('"')
                    i += 1
            else:
                current.append("\\" * run)
            hasToken = True
            continue
        if c == '"':
            inQuotes = not inQuotes
            hasToken = True
        elif c.isspace() and not inQuotes:
            if hasToken:
                tokens.append("".join(current))
            current, hasToken = [], False
        else:
            current.append(c)
            hasToken = True
        i += 1
    if hasToken:
        tokens.append("".join(current))
    return tokens


def Normalize(_text, _roots):
    # A root matches whichever separators and case a path was written with: the original's generated
    # projects write some paths with forward slashes.
    for path, name in _roots:
        parts = [re.escape(p) for p in re.split(r"[\\/]+", path.rstrip("\\/"))]
        _text = re.sub(r"[\\/]+".join(parts), lambda _m: name, _text, flags=re.I)
    return _text


def ProjectOf(_tool, _switches):
    """The project an invocation belongs to, from where it writes."""
    key = {"CL": "/Fo", "rc": "/fo"}.get(_tool, "/OUT:")
    for s in _switches:
        if s[: len(key)].lower() == key.lower():
            path = s[len(key):]
            if _tool in ("CL", "rc"):
                # FrontierOutpost: obj\<Platform>\<Configuration>\<Project>\...; the original: <Project>.dir\...
                m = re.search(r"[\\/]obj[\\/][^\\/]+[\\/][^\\/]+[\\/]([^\\/]+)", path) or re.search(r"([^\\/]+)\.dir[\\/]", path)
                return m.group(1) if m else path
            stem = os.path.splitext(re.split(r"[\\/]", path)[-1])[0]
            stem = re.sub(r"^(sfml-[a-z]+)(-s)?(-d)?$", r"\1", stem)
            return {"glew32s": "glew"}.get(stem, stem)
    return "?"


def Parse(_path, _roots):
    projects = collections.OrderedDict()
    for raw in open(_path, encoding="utf-8-sig", errors="replace"):
        line = re.sub(r"^\s*(\d+>)?\s*", "", raw.rstrip("\r\n"))
        match = INVOCATION.match(line)
        if not match:
            continue
        tool = TOOLS[re.split(r"[\\/]", match["exe"])[-1].lower()]
        tokens = [match["exe"]] + Tokenize(match["args"])
        switches, files = [], []
        i = 1
        while i < len(tokens):
            t = tokens[i]
            if t.lower() in SPLIT_SWITCHES and i + 1 < len(tokens):
                i += 1
                t = t + tokens[i]
            if tool in ("CL", "rc"):
                # One spelling for a definition, whether it was written "/D WIN32" or "/DWIN32".
                t = re.sub(r"^/([DU])\s*(?=\S)", r"/\1 ", t)
            (switches if t.startswith("/") or t.startswith("-") else files).append(t)
            i += 1
        project = ProjectOf(tool, switches)
        switches = [Normalize(s, _roots) for s in switches]
        files = [Normalize(f, _roots) for f in files]
        entry = projects.setdefault(project, collections.OrderedDict()).setdefault(tool, [])
        entry.append((tokens[0], switches, files))
    return projects


def Show(_projects, _roots):
    for project, tools in _projects.items():
        print(f"== {project}")
        for tool, invocations in tools.items():
            groups = collections.OrderedDict()
            for exe, switches, files in invocations:
                fixed = tuple(s for s in switches if not s.startswith(VARYING))
                group = groups.setdefault(fixed, {"exe": exe, "count": 0, "files": [], "varying": set()})
                group["count"] += 1
                group["files"] += files
                group["varying"].update(s for s in switches if s.startswith(VARYING))
            for fixed, group in groups.items():
                sources = [f for f in group["files"] if not f.lower().endswith(".lib")]
                libs = [f for f in group["files"] if f.lower().endswith(".lib")]
                print(f"  {tool}: {group['count']} invocation(s), {len(sources)} input file(s)"
                      + (f", {len(group['varying'])} object folder(s)" if len(group['varying']) > 1 else ""))
                print(f"    tool: {Normalize(group['exe'], _roots)}")
                for prefix in ORDERED.get(tool, ()):
                    values = [s[len(prefix):] for s in fixed if s.upper().startswith(prefix.upper())]
                    if values:
                        print(f"    {prefix} in order:")
                        for v in values:
                            print(f"      {v}")
                # Case matters: cl's /D defines, its /diagnostics does not.
                defines = sorted(s for s in fixed if s.startswith(SORTED_DEFINES) and tool in ("CL", "rc"))
                if defines:
                    print("    definitions: " + " ".join(defines))
                rest = sorted(s for s in fixed if s not in defines
                              and not any(s.upper().startswith(p.upper()) for p in ORDERED.get(tool, ())))
                print("    switches: " + " ".join(rest))
                if libs:
                    print("    libraries in order: " + " ".join(libs))
        print()


def Main(_argv):
    roots = []
    args = list(_argv)
    while args and args[0] == "--root":
        path, _, name = args[1].partition("=")
        roots.append((path, name))
        args = args[2:]
    if len(args) != 1:
        sys.exit(__doc__)
    if not os.path.exists(args[0]):
        print(f"No log at {args[0]}.")
        return
    # Longest prefix first, so that a root inside another root is replaced by its own name.
    roots.sort(key=lambda r: -len(r[0]))
    Show(Parse(args[0], roots), roots)


if __name__ == "__main__":
    Main(sys.argv[1:])
