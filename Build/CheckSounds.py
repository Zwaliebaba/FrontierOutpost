#!/usr/bin/env python3
"""Check the sounds the game names against the WAV files in GameData/sound (NeuronClient plan, Phase 1).

Usage: python Build/CheckSounds.py [--root DIR]

The game plays a sound by a name written into its code or its scripts: a string ending in .wav, in
a source file the solution builds or in GameData/script. For each name it reports whether
GameData/sound holds the file. A missing one does not fail the check: the engine logs a warning
the first time it is played and plays silence (N6), so this list, not the log, is where a missing
sound shows.

It then reads every WAV file in GameData/sound as the engine does (SoundData::Read in
FrontierOutpost/src/liblt/Module/SoundEngine/XAudio2.cpp), and checks its format against those
XAudio2 plays: PCM at 8, 16, 24 or 32 bits, 32-bit IEEE float, either of them through
WAVE_FORMAT_EXTENSIBLE, and Microsoft ADPCM with its standard coefficients. A file that fails
stops the game the first time it is played (N9), and fails the check.

Exit 0 when every WAV file there can be played, whatever is missing; 1 otherwise.
"""

import argparse
import pathlib
import re
import struct
import sys

from ProjectModel import Project, SolutionFiles, SolutionProjects, TreeFiles

SOUNDS = "GameData/sound/"
SCRIPTS = "GameData/script/"
NAME = re.compile(r'"((?:[^"\\\n]|\\.)*\.wav)"')

PCM = 0x0001
ADPCM = 0x0002
IEEE_FLOAT = 0x0003
EXTENSIBLE = 0xFFFE
REFUSED = {0x0006: "A-law", 0x0007: "mu-law", 0x0011: "IMA ADPCM", 0x0031: "GSM 6.10", 0x0050: "MPEG",
           0x0055: "MPEG layer 3", 0x0161: "xWMA", 0x0162: "xWMA"}
# KSDATAFORMAT_SUBTYPE_PCM and _IEEE_FLOAT are {0000000X-0000-0010-8000-00AA00389B71}: past the
# format tag in their first four bytes, the GUID's bytes are these.
SUBFORMAT_TAIL = bytes.fromhex("00001000800000aa00389b71")
ADPCM_COEFFICIENTS = [(256, 0), (512, -256), (0, 0), (192, 64), (240, 0), (460, -208), (392, -232)]
MAX_CHANNELS = 64            # XAUDIO2_MAX_AUDIO_CHANNELS
MIN_RATE, MAX_RATE = 1000, 200000  # XAUDIO2_MIN_SAMPLE_RATE, XAUDIO2_MAX_SAMPLE_RATE


def CppCode(_text):
  """C++ text without its comments, with strings kept, lines kept."""
  out = []
  i, n = 0, len(_text)
  while i < n:
    if _text.startswith("//", i):
      j = _text.find("\n", i)
      i = n if j < 0 else j
    elif _text.startswith("/*", i):
      j = _text.find("*/", i + 2)
      j = n if j < 0 else j + 2
      out.append("\n" * _text.count("\n", i, j))
      i = j
    elif _text[i] in "\"'":
      j = i + 1
      while j < n and _text[j] != _text[i] and _text[j] != "\n":
        j += 2 if _text[j] == "\\" else 1
      out.append(_text[i:j + 1])
      i = j + 1
    else:
      out.append(_text[i])
      i += 1
  return "".join(out)


def ScriptCode(_text):
  """LTSL text without its comments, lines kept. A line whose first token is # comments out the
    block indented under it as well."""
  out = []
  commentIndent = None
  for line in _text.split("\n"):
    stripped = line.lstrip(" ")
    indent = len(line) - len(stripped)
    if commentIndent is not None and (not stripped.strip() or indent > commentIndent):
      out.append("")
      continue
    commentIndent = None
    if stripped.startswith("#"):
      commentIndent = indent
      out.append("")
      continue
    out.append(line)
  return "\n".join(out)


def Sources(_root):
  """Every C++ file a project in the solution compiles or lists, relative to the root."""
  solutions = SolutionFiles(_root)
  files = set()
  if len(solutions) != 1:
    return files
  for relative in SolutionProjects(solutions[0]):
    if not (_root / relative).is_file():
      continue
    project = Project(_root, relative)
    items = project.Items()
    for item in items.get("ClCompile", []) + items.get("ClInclude", []):
      path = (project.folder / item).resolve()
      if path.is_file():
        files.add(path.relative_to(_root).as_posix())
  return files


def Names(_root, _tree):
  """{sound name: [where it is named]}."""
  names = {}
  sources = [(f, CppCode) for f in sorted(Sources(_root))]
  sources += [(f, ScriptCode) for f in _tree if f.startswith(SCRIPTS) and f.endswith(".lts")]
  for relative, strip in sources:
    text = strip((_root / relative).read_text(encoding="utf-8", errors="replace"))
    for number, line in enumerate(text.split("\n"), 1):
      for name in NAME.findall(line):
        names.setdefault(name, []).append(f"{relative}:{number}")
  return names


def Problem(_data):
  """Why the engine or XAudio2 cannot play this WAV file, or None."""
  if len(_data) < 12 or _data[:4] != b"RIFF" or _data[8:12] != b"WAVE":
    return "not a RIFF WAVE file"
  fmt = dataBytes = None
  at = 12
  while at + 8 <= len(_data):
    size = struct.unpack_from("<I", _data, at + 4)[0]
    length = min(size, len(_data) - (at + 8))
    if _data[at:at + 4] == b"fmt ":
      fmt = _data[at + 8:at + 8 + length]
    elif _data[at:at + 4] == b"data":
      dataBytes = length
    at += 8 + size + (size & 1)
  if fmt is None or dataBytes is None:
    return "no fmt chunk" if fmt is None else "no data chunk"
  if len(fmt) < 16:
    return f"a fmt chunk of {len(fmt)} bytes, where the engine needs 16"
  tag, channels, rate, _, align, bits = struct.unpack_from("<HHIIHH", fmt)
  if not align or not rate or not channels:
    return "a block size, sample rate or channel count of zero"
  if dataBytes < align:
    return "no whole block of samples"
  if channels > MAX_CHANNELS or not MIN_RATE <= rate <= MAX_RATE:
    return f"{channels} channels at {rate} Hz, outside XAudio2's limits"
  if tag == EXTENSIBLE:
    if len(fmt) < 40:
      return "WAVE_FORMAT_EXTENSIBLE in a fmt chunk under 40 bytes"
    valid = struct.unpack_from("<H", fmt, 18)[0]
    subformat = struct.unpack_from("<I", fmt, 24)[0]
    if fmt[28:40] != SUBFORMAT_TAIL or subformat not in (PCM, IEEE_FLOAT):
      return "WAVE_FORMAT_EXTENSIBLE with a subformat other than PCM or IEEE float"
    if valid > bits:
      return f"{valid} valid bits in a {bits}-bit sample"
    tag = subformat
  if tag == PCM:
    if bits not in (8, 16, 24, 32):
      return f"{bits}-bit PCM"
    return None if align == channels * bits // 8 else f"a block of {align} bytes for {channels} x {bits}-bit PCM"
  if tag == IEEE_FLOAT:
    if bits != 32:
      return f"{bits}-bit IEEE float"
    return None if align == channels * 4 else f"a block of {align} bytes for {channels} x 32-bit float"
  if tag == ADPCM:
    if channels not in (1, 2) or bits != 4 or len(fmt) < 50:
      return "Microsoft ADPCM that is not 4-bit mono or stereo with its coefficient table"
    samplesPerBlock, count = struct.unpack_from("<HH", fmt, 18)
    coefficients = [struct.unpack_from("<hh", fmt, 22 + 4 * i) for i in range(7)]
    if count != 7 or coefficients != ADPCM_COEFFICIENTS:
      return "Microsoft ADPCM with non-standard coefficients"
    expected = (align - 7 * channels) * 8 // (4 * channels) + 2
    if samplesPerBlock != expected or (channels == 1 and samplesPerBlock % 2):
      return f"Microsoft ADPCM with {samplesPerBlock} samples in a {align}-byte block"
    return None
  return f"format 0x{tag:04X}" + (f" ({REFUSED[tag]})" if tag in REFUSED else "") + ", which XAudio2 does not play"


def OggSource(_root, _name):
  """The Ogg file a missing WAV is to be converted from (MIGRATION_NOTES.md O10), if there is one."""
  stem = _name[:-len(".wav")]
  ogg = (stem[:-len("_ogg")] if stem.endswith("_ogg") else stem) + ".ogg"
  return ogg if (_root / SOUNDS / ogg).is_file() else None


def main():
  parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
  parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parent.parent)
  args = parser.parse_args()
  root = args.root.resolve()
  tree = TreeFiles(root)

  names = Names(root, tree)
  missing = sorted(n for n in names if not (root / SOUNDS / n).is_file())
  print(f"The code and scripts name {len(names)} sound(s). {len(missing)} have no WAV file in {SOUNDS}, "
        f"and play silence (N6)" + (":" if missing else "."))
  for name in missing:
    ogg = OggSource(root, name)
    print(f"  {name}  ({names[name][0]}" + (f"; converts from {ogg}, O10)" if ogg else ")"))

  wavs = [f[len(SOUNDS):] for f in tree if f.startswith(SOUNDS) and f.lower().endswith(".wav")]
  unnamed = sorted(w for w in wavs if w not in names)
  print(f"{SOUNDS} holds {len(wavs)} WAV file(s). {len(unnamed)} are named nowhere" + (":" if unnamed else "."))
  for wav in unnamed:
    print(f"  {wav}")

  oggs = sorted(f[len(SOUNDS):] for f in tree if f.startswith(SOUNDS) and f.lower().endswith(".ogg"))
  needed = {OggSource(root, n) for n in missing} - {None}
  print(f"{SOUNDS} holds {len(oggs)} Ogg file(s), which the engine does not play. {len(needed)} are the "
        f"sources of named sounds, and {len(oggs) - len(needed)} are named nowhere" + (":" if oggs else "."))
  for ogg in oggs:
    if ogg not in needed:
      print(f"  {ogg}")

  broken = []
  for wav in sorted(wavs):
    problem = Problem((root / SOUNDS / wav).read_bytes())
    if problem:
      broken.append(wav)
      print(f"{SOUNDS}{wav}: {problem}")
  if broken:
    print(f"{len(broken)} WAV file(s) cannot be played, and stop the game the first time they are (N9).")
    return 1
  print(f"All {len(wavs)} WAV file(s) can be played.")
  return 0


if __name__ == "__main__":
  sys.exit(main())
