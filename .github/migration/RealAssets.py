#!/usr/bin/env python3
"""Replace GameData's Git LFS pointer files with the real files (FrontierOutpost/MIGRATION_NOTES.md §22).

Usage: RealAssets.py --upstream DIR --sha SHA [--fetch] [--skip-suffix SUFFIX ...] [--hold PATH ...]
                     [--subject TEXT] --message FILE

Run from the repository root. DIR is a clone of the original at SHA, checked out with
GIT_LFS_SKIP_SMUDGE=1, so that its resource/ holds the same pointer files GameData/ does. PATH is a
path below GameData/, with forward slashes. For every pointer file under GameData/ whose name ends
in none of the SUFFIXes:

  1. the original's resource/<same path> at SHA must be the very same pointer, byte for byte;
  2. with --fetch, git-lfs downloads those objects into DIR's working tree (without it, DIR must
     already hold them);
  3. the downloaded file must have the size and SHA-256 its pointer names;
  4. unless it is held, it replaces its pointer in GameData/ and is staged, and the staged blob must
     hash to the pointer's SHA-256 again, so that no attribute or filter changed a byte on its way
     into the index.

A held file is fetched and checked but stays a pointer. For a held font, the copyright, version and
licence entries of its name table are printed. Nothing is committed here: when anything was staged,
the commit message is written to FILE, and the workflow commits and pushes.

Markdown goes to stdout and, when GITHUB_STEP_SUMMARY is set, is appended there as well.
"""

import argparse
import collections
import hashlib
import os
import re
import struct
import subprocess
import sys
import textwrap

GAME_DATA = "GameData"
UPSTREAM_DATA = "resource"
POINTER_PREFIX = b"version https://git-lfs.github.com/spec/v1\n"
POINTER = re.compile(rb"\Aversion https://git-lfs\.github\.com/spec/v1\noid sha256:([0-9a-f]{64})\nsize (\d+)\n\Z")
# Name IDs in an OpenType 'name' table: copyright, version, licence description, licence URL.
FONT_NAMES = {0: "copyright", 5: "version", 13: "licence", 14: "licence URL"}
FONT_SUFFIXES = (".ttf", ".otf")


def Git(_args, _cwd=None):
    result = subprocess.run(["git", *_args], cwd=_cwd, capture_output=True)
    if result.returncode != 0:
        raise SystemExit(f"git {' '.join(_args[:3])} ... failed ({result.returncode}): "
                         f"{result.stderr.decode(errors='replace').strip()}")
    return result.stdout


def ReadPointer(_data):
    """(oid, size) for the bytes of an LFS pointer file, or None for anything else."""
    if not _data.startswith(POINTER_PREFIX):
        return None
    match = POINTER.match(_data)
    if not match:
        raise SystemExit(f"Malformed LFS pointer: {_data[:200]!r}")
    return match[1].decode(), int(match[2])


def Pointers(_root):
    """{path below _root, with forward slashes: (pointer bytes, oid, size)} for every pointer file."""
    pointers = {}
    for base, dirs, files in os.walk(_root):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(base, name)
            with open(path, "rb") as stream:
                data = stream.read(1024)
            pointer = ReadPointer(data)
            if pointer:
                pointers[os.path.relpath(path, _root).replace(os.sep, "/")] = (data, *pointer)
    return pointers


def FontNames(_data):
    """{name ID: text} for FONT_NAMES in a TrueType or CFF OpenType font, preferring Windows English."""
    tableCount = struct.unpack_from(">H", _data, 4)[0]
    for index in range(tableCount):
        tag, _, offset, _ = struct.unpack_from(">4sIII", _data, 12 + 16 * index)
        if tag == b"name":
            break
    else:
        return {}
    _, count, stringOffset = struct.unpack_from(">HHH", _data, offset)
    names = {}
    for index in range(count):
        platform, _, language, nameId, length, start = struct.unpack_from(">HHHHHH", _data, offset + 6 + 12 * index)
        if nameId not in FONT_NAMES or platform not in (0, 1, 3):
            continue
        begin = offset + stringOffset + start
        raw = _data[begin:begin + length]
        text = raw.decode("mac_roman" if platform == 1 else "utf-16-be", errors="replace")
        if nameId not in names or (platform == 3 and language == 0x409):
            names[nameId] = " ".join(text.split())
    return names


def Megabytes(_bytes):
    return f"{_bytes / (1024 * 1024):.1f} MB"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--upstream", required=True)
    parser.add_argument("--sha", required=True)
    parser.add_argument("--fetch", action="store_true")
    parser.add_argument("--skip-suffix", action="append", default=[])
    parser.add_argument("--hold", action="append", default=[])
    parser.add_argument("--subject", default="Put the real asset files in GameData")
    parser.add_argument("--message", required=True)
    args = parser.parse_args()

    pointers = Pointers(GAME_DATA)
    skipped = sorted(path for path in pointers if path.lower().endswith(tuple(args.skip_suffix)))
    wanted = sorted(path for path in pointers if path not in skipped)
    missingHolds = sorted(set(args.hold) - set(wanted))
    if missingHolds:
        raise SystemExit(f"Held, but not a pointer file below {GAME_DATA}/ any more: {', '.join(missingHolds)}")
    held = sorted(args.hold)
    replaced = [path for path in wanted if path not in held]

    lines = ["### Real asset files", "",
             f"{len(pointers)} pointer files below `{GAME_DATA}/`: {len(replaced)} to replace, "
             f"{len(held)} held, {len(skipped)} skipped ({', '.join(args.skip_suffix) or 'no suffix'}).", ""]
    if not replaced:
        lines += ["Nothing to replace.", ""]
        Report(lines)
        return 0

    # 1. The same pointer at the same path in the original, at the pinned commit.
    for path in wanted:
        theirs = Git(["cat-file", "blob", f"{args.sha}:{UPSTREAM_DATA}/{path}"], args.upstream)
        if theirs != pointers[path][0]:
            raise SystemExit(f"{UPSTREAM_DATA}/{path} at {args.sha} is not the pointer {GAME_DATA}/{path} is.")

    # 2. The objects themselves.
    if args.fetch:
        Git(["lfs", "install", "--local"], args.upstream)
        Git(["lfs", "pull", "--include", ",".join(f"{UPSTREAM_DATA}/{path}" for path in wanted)], args.upstream)

    # 3. Each is what its pointer names.
    contents = {}
    for path in wanted:
        _, oid, size = pointers[path]
        with open(os.path.join(args.upstream, UPSTREAM_DATA, *path.split("/")), "rb") as stream:
            data = stream.read()
        if len(data) != size or hashlib.sha256(data).hexdigest() != oid:
            raise SystemExit(f"{UPSTREAM_DATA}/{path}: {len(data)} bytes, SHA-256 {hashlib.sha256(data).hexdigest()};"
                             f" its pointer names {size} bytes, {oid}.")
        contents[path] = data

    # 4. Into GameData/ and the index, and the index holds exactly those bytes.
    for path in replaced:
        with open(os.path.join(GAME_DATA, *path.split("/")), "wb") as stream:
            stream.write(contents[path])
    Git(["add", "--", *(f"{GAME_DATA}/{path}" for path in replaced)])
    for path in replaced:
        staged = Git(["cat-file", "blob", f":{GAME_DATA}/{path}"])
        if hashlib.sha256(staged).hexdigest() != pointers[path][1]:
            raise SystemExit(f"The staged {GAME_DATA}/{path} is not the file its pointer names.")

    byKind = collections.Counter()
    bytesByKind = collections.Counter()
    for path in replaced:
        kind = f"{path.split('/')[0]}/*{os.path.splitext(path)[1].lower()}"
        byKind[kind] += 1
        bytesByKind[kind] += pointers[path][2]
    total = sum(pointers[path][2] for path in replaced)
    lines += ["| Replaced | Files | Size |", "|---|---|---|"]
    lines += [f"| `{kind}` | {count} | {Megabytes(bytesByKind[kind])} |" for kind, count in sorted(byKind.items())]
    lines += [f"| **all** | **{len(replaced)}** | **{Megabytes(total)}** |", ""]
    if held:
        lines += ["Held (fetched and checked, left as pointers):", ""]
        for path in held:
            lines.append(f"- `{path}`, {pointers[path][2]:,} bytes")
            if path.lower().endswith(FONT_SUFFIXES):
                names = FontNames(contents[path])
                lines += [f"  - {FONT_NAMES[nameId]}: {names.get(nameId, '(none)')}" for nameId in FONT_NAMES]
        lines.append("")
    Report(lines)

    summary = ", ".join(f"{count} {kind}" for kind, count in sorted(byKind.items()))
    paragraphs = [
        f"{len(replaced)} of the {len(pointers)} Git LFS pointer files in GameData/ become the files they"
        f" point to, {Megabytes(total)} in all: {summary}. Each comes from the original,"
        f" JoshParnell/ltheory-old at {args.sha[:7]}, where the same path holds the same pointer, and"
        " its size and SHA-256 were checked against that pointer before and after staging"
        " (FrontierOutpost/MIGRATION_NOTES.md §22).",
    ]
    if skipped or held:
        paragraphs.append(f"Still pointers: {len(skipped)} files ending in {' or '.join(args.skip_suffix)}, and"
                          f" {len(held)} held back. §22 says why.")
    else:
        paragraphs.append(f"No LFS pointer file is left in {GAME_DATA}/.")
    with open(args.message, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(f"{args.subject}\n\n")
        stream.write("".join(textwrap.fill(paragraph, 72) + "\n\n" for paragraph in paragraphs))
    return 0


def Report(_lines):
    report = "\n".join(_lines) + "\n"
    sys.stdout.write(report)
    summaryPath = os.environ.get("GITHUB_STEP_SUMMARY")
    if summaryPath:
        with open(summaryPath, "a", encoding="utf-8") as summary:
            summary.write(report)


if __name__ == "__main__":
    sys.exit(main())
