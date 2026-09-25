#!/usr/bin/env python3
"""Name the warnings two MSBuild file logs do not share (FrontierOutpost/MIGRATION_NOTES.md §18.3).

Usage: DiffWarnings.py OLD_LABEL=OLD_LOG=OLD_ROOT NEW_LABEL=NEW_LOG=NEW_ROOT

Each log's unique warnings are collected as SummarizeBuildLog.py collects them, with every path made
relative to its own build's root, so that two checkouts in different directories compare like for
like. It prints the warnings only the old build raised, then those only the new one raised. A
warning whose line moved, because an edit above it added or removed lines, shows up in both lists.

Markdown goes to stdout and, when GITHUB_STEP_SUMMARY is set, is appended there as well.
"""

import collections
import os
import sys

from SummarizeBuildLog import COMPILING, DIAGNOSTIC


def Relative(_origin, _root):
    origin = _origin.strip().replace("/", "\\")
    root = _root.strip().replace("/", "\\").rstrip("\\") + "\\"
    if origin.lower().startswith(root.lower()):
        return origin[len(root):]
    return origin


def Collect(_path, _root):
    """Unique warnings as {key: (origin, code, message)}, keyed case-insensitively on the path, as
    SummarizeBuildLog.py counts them: MSVC can spell one header differently from different sources."""
    warnings = {}
    text = open(_path, encoding="utf-8-sig", errors="replace").read()
    for raw in text.splitlines():
        match = DIAGNOSTIC.match(raw)
        if not match or match["severity"] != "warning":
            continue
        origin = Relative(match["origin"], _root)
        message = COMPILING.sub("", match["message"])
        warnings.setdefault((origin.lower(), match["code"], message), (origin, match["code"], message))
    return warnings


def Section(_title, _entries):
    lines = [f"{_title} ({len(_entries)}):", ""]
    if _entries:
        byCode = collections.Counter(code for _, code, _ in _entries)
        lines.append("By code: " + ", ".join(f"{c} {n}" for c, n in byCode.most_common()))
        lines.append("")
        lines.append("```")
        lines += [f"{origin}: warning {code}: {message}" for origin, code, message in sorted(_entries)]
        lines.append("```")
        lines.append("")
    return lines


def main(_args):
    if len(_args) != 2 or any(arg.count("=") < 2 for arg in _args):
        print(__doc__)
        return 2
    (oldLabel, oldLog, oldRoot), (newLabel, newLog, newRoot) = (arg.split("=", 2) for arg in _args)
    for path in (oldLog, newLog):
        if not os.path.exists(path):
            print(f"No log at `{path}`: the build that writes it did not run.")
            return 1
    old = Collect(oldLog, oldRoot)
    new = Collect(newLog, newRoot)
    shared = old.keys() & new.keys()
    lines = [f"### Warnings: {oldLabel} against {newLabel}", "",
             f"{oldLabel}: {len(old)} unique | {newLabel}: {len(new)} unique | shared: {len(shared)}", ""]
    lines += Section(f"Only in {oldLabel}", [old[key] for key in old.keys() - shared])
    lines += Section(f"Only in {newLabel}", [new[key] for key in new.keys() - shared])
    report = "\n".join(lines) + "\n"
    sys.stdout.write(report)
    summaryPath = os.environ.get("GITHUB_STEP_SUMMARY")
    if summaryPath:
        with open(summaryPath, "a", encoding="utf-8") as summary:
            summary.write(report)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
