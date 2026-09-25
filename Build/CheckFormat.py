#!/usr/bin/env python3
"""Check that every first-party C++ file is formatted as /.clang-format says (AGENTS.md §4).

Usage: python Build/CheckFormat.py [--clang-format PATH] [--fix] [--root DIR]

It runs over the whole tree that git does not ignore, and leaves alone what AGENTS.md does not
govern: the legacy import and its data (FrontierOutpost/, GameData/: ADR-001, ADR-004), the
original's copy, build output, and the Resource.h that Visual Studio's resource editor rewrites.

The layout changes between clang-format releases, so the answer that counts is the pinned
version's, which CI runs. A different version says so, and its verdict is advisory.

--fix rewrites the files that differ, in place. Exit 0 when every file is formatted (or has just
been fixed), 1 otherwise.
"""

import argparse
import difflib
import pathlib
import shutil
import subprocess
import sys

from ProjectModel import IsExempt, TreeFiles

PINNED = "22.1.3"  # build.yml's `pip install clang-format==22.1.3`, the version Visual Studio 2026 bundles
UNFORMATTED = {"Resource.h"}
SHOWN_LINES = 12


def Version(_clangFormat):
  output = subprocess.run([_clangFormat, "--version"], capture_output=True, text=True).stdout
  for word in output.split():
    if word[:1].isdigit() and word.count(".") >= 2:
      return word.split("-")[0]
  return output.strip() or "unknown"


def main():
  parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
  parser.add_argument("--clang-format", dest="clangFormat", default="clang-format")
  parser.add_argument("--fix", action="store_true")
  parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent)
  args = parser.parse_args()
  root = args.root.resolve()

  clangFormat = shutil.which(args.clangFormat) or args.clangFormat
  if not pathlib.Path(clangFormat).is_file():
    print(f"clang-format not found ({args.clangFormat}). `pip install clang-format=={PINNED}` gets the pinned one.")
    return 1
  version = Version(clangFormat)
  print(f"clang-format {version} ({clangFormat})")
  if version != PINNED:
    print(f"  Not the pinned {PINNED}: its layout can differ, so treat this run as advisory.")

  files = [f for f in TreeFiles(root)
           if f.endswith((".h", ".cpp")) and not IsExempt(f) and f.split("/")[-1] not in UNFORMATTED]
  style = f"--style=file:{root / '.clang-format'}"
  offenders = []
  for relative in files:
    path = root / relative
    original = path.read_bytes()
    result = subprocess.run([clangFormat, style, f"--assume-filename={path}"], input=original, capture_output=True)
    if result.returncode != 0:
      print(f"{relative}: clang-format failed: {result.stderr.decode(errors='replace').strip()}")
      offenders.append(relative)
      continue
    if result.stdout == original:
      continue
    if args.fix:
      path.write_bytes(result.stdout)
      print(f"{relative}: reformatted")
      continue
    offenders.append(relative)
    before = original.decode("utf-8", errors="replace").splitlines()
    after = result.stdout.decode("utf-8", errors="replace").splitlines()
    diff = list(difflib.unified_diff(before, after, f"{relative}", f"{relative} (formatted)", n=1, lineterm=""))
    print("\n".join(diff[:SHOWN_LINES]) + ("\n..." if len(diff) > SHOWN_LINES else ""))

  print(f"{len(files)} file(s) checked, {len(offenders)} not formatted."
        + (" Run `python Build/CheckFormat.py --fix`." if offenders else ""))
  return 1 if offenders else 0


if __name__ == "__main__":
  sys.exit(main())
