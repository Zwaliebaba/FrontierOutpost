#!/usr/bin/env python3
"""Summarize MSBuild file logs for the migration workflow (FrontierOutpost/MIGRATION_NOTES.md).

Usage: SummarizeBuildLog.py [--list CODE[,CODE...]] LABEL=LOG [LABEL=LOG ...]

For each log it reports MSBuild's own totals, then the UNIQUE diagnostics: MSBuild repeats every
diagnostic in its closing summary, and a warning raised in a header repeats once per translation
unit that includes it, so the raw totals overstate what there is to fix. Unique diagnostics are
broken down by code and by project, and the first errors are listed in the order they occurred.
--list prints every unique occurrence of the given diagnostic codes with its location.

Markdown goes to stdout and, when GITHUB_STEP_SUMMARY is set, is appended there as well.
"""

import collections
import os
import re
import sys

# "  3>C:\src\a.cpp(12,5): warning C4244: 'x': conversion ... [C:\build\lt.vcxproj]"
# "LINK : fatal error LNK1104: cannot open file 'lt.lib' [C:\build\launch.vcxproj]"
# "cl : command line warning D9025: overriding '/EHs' with '/EHsc' [C:\build\lt.vcxproj]"
DIAGNOSTIC = re.compile(
    r"^\s*(?:\d+>)?(?P<origin>.*?)\s*:\s*(?:fatal\s+|command line\s+)?"
    r"(?P<severity>warning|error)\s+(?P<code>[A-Za-z]+\d+)\s*:\s*(?P<message>.*?)"
    r"(?:\s*\[(?P<project>[^\[\]]+)\])?\s*$")
# MSVC 17.x appends the translation unit to a diagnostic raised in a header.
COMPILING = re.compile(r"\s*\(compiling source file '[^']*'\)")
TOTAL = re.compile(r"^\s*(\d+) (Warning|Error)\(s\)", re.M)
ELAPSED = re.compile(r"^\s*Time Elapsed ([0-9:.]+)", re.M)
FIRST_ERRORS = 40


def Summarize(_label, _path, _listCodes):
    lines = [f"### {_label}", ""]
    if not os.path.exists(_path):
        lines += [f"No log at `{_path}`: the step that writes it did not run.", ""]
        return lines

    text = open(_path, encoding="utf-8-sig", errors="replace").read()
    totals = {kind: int(count) for count, kind in TOTAL.findall(text)}
    elapsed = ELAPSED.findall(text)

    seen = set()
    unique = {"warning": [], "error": []}
    for raw in text.splitlines():
        match = DIAGNOSTIC.match(raw)
        if not match:
            continue
        message = COMPILING.sub("", match["message"])
        key = (match["origin"].strip().lower(), match["severity"], match["code"], message)
        if key in seen:
            continue
        seen.add(key)
        project = os.path.splitext(re.split(r"[\\/]", (match["project"] or "?").strip())[-1])[0]
        unique[match["severity"]].append((match["code"], project, match["origin"].strip(), message))

    lines.append(
        f"MSBuild totals: **{totals.get('Warning', '?')} warning(s), {totals.get('Error', '?')} error(s)**"
        f" | unique: **{len(unique['warning'])} warning(s), {len(unique['error'])} error(s)**"
        f" | elapsed {elapsed[-1] if elapsed else '?'}")
    lines.append("")

    for severity in ("warning", "error"):
        entries = unique[severity]
        if not entries:
            continue
        byCode = collections.Counter(code for code, _, _, _ in entries)
        byProject = collections.Counter(project for _, project, _, _ in entries)
        lines.append(f"Unique {severity}s by code: " + ", ".join(f"{c} {n}" for c, n in byCode.most_common()))
        lines.append("")
        lines.append(f"Unique {severity}s by project: " + ", ".join(f"{p} {n}" for p, n in byProject.most_common()))
        lines.append("")

    for code in _listCodes:
        hits = [entry for severity in ("warning", "error") for entry in unique[severity] if entry[0] == code]
        lines.append(f"Every {code} ({len(hits)}):")
        lines.append("")
        if hits:
            lines.append("```")
            lines += [f"[{project}] {origin}: {message}" for _, project, origin, message in hits]
            lines.append("```")
            lines.append("")

    if unique["error"]:
        lines.append(f"First {min(FIRST_ERRORS, len(unique['error']))} unique errors, in order:")
        lines.append("")
        lines.append("```")
        for code, project, origin, message in unique["error"][:FIRST_ERRORS]:
            lines.append(f"[{project}] {origin}: error {code}: {message}")
        lines.append("```")
        lines.append("")
    return lines


def main(_args):
    if not _args:
        print(__doc__)
        return 2
    listCodes = []
    if _args[:1] == ["--list"] and len(_args) > 1:
        listCodes = [code for code in _args[1].split(",") if code]
        _args = _args[2:]
    out = []
    for arg in _args:
        label, _, path = arg.partition("=")
        out += Summarize(label, path, listCodes)
    report = "\n".join(out) + "\n"
    sys.stdout.write(report)
    summaryPath = os.environ.get("GITHUB_STEP_SUMMARY")
    if summaryPath:
        with open(summaryPath, "a", encoding="utf-8") as summary:
            summary.write(report)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
