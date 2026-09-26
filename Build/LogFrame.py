#!/usr/bin/env python3
"""Prints a PNG into the log, downscaled and in base64, for whoever cannot fetch CI's artefacts.

The smoke job (Design/Plan/NeuronClient-migration.md, Phase 4 step 7 and N15) uploads each app's
capture whole for the owner. The session that ports liblt cannot fetch artefacts, so this puts a
smaller copy in the log itself, between a BEGIN line and an END line that name the file:

    python Build/LogFrame.py <capture.png> [--width 320]

The copy is RGB, each pixel the average of a square of the capture's, at most --width pixels wide.
After it come the mean colour, the share of pixels that are not black, and a thumbnail in text,
its luminance scaled to the frame's brightest, so that the frame can be judged from the log alone. It reads the PNGs the smoke mode writes (ADR-011):
8-bit RGB or RGBA, not interlaced. The standard library does all of it, so CI installs nothing for it.
"""

import argparse
import base64
import pathlib
import struct
import sys
import zlib

SIGNATURE = b"\x89PNG\r\n\x1a\n"
LINE_CHARACTERS = 76
THUMBNAIL_COLUMNS = 96
RAMP = " .:-=+*#%@"


def Paeth(_left, _up, _upLeft):
  estimate = _left + _up - _upLeft
  toLeft = abs(estimate - _left)
  toUp = abs(estimate - _up)
  toUpLeft = abs(estimate - _upLeft)
  if toLeft <= toUp and toLeft <= toUpLeft:
    return _left
  return _up if toUp <= toUpLeft else _upLeft


def Unfilter(_filter, _row, _previous, _texelBytes):
  """Undoes one row's PNG filter in place, from the row above it."""
  if _filter == 0:
    return
  for index in range(len(_row)):
    left = _row[index - _texelBytes] if index >= _texelBytes else 0
    up = _previous[index]
    upLeft = _previous[index - _texelBytes] if index >= _texelBytes else 0
    if _filter == 1:
      predicted = left
    elif _filter == 2:
      predicted = up
    elif _filter == 3:
      predicted = (left + up) // 2
    elif _filter == 4:
      predicted = Paeth(left, up, upLeft)
    else:
      raise ValueError(f"filter {_filter} is not one of PNG's")
    _row[index] = (_row[index] + predicted) & 0xFF


def ReadPng(_path):
  """(width, height, channels, rows), rows top first."""
  data = pathlib.Path(_path).read_bytes()
  if data[:8] != SIGNATURE:
    raise ValueError(f"{_path} is not a PNG")
  position = 8
  compressed = bytearray()
  header = None
  while position < len(data):
    length, kind = struct.unpack(">I4s", data[position:position + 8])
    body = data[position + 8:position + 8 + length]
    position += 12 + length
    if kind == b"IHDR":
      header = struct.unpack(">IIBBBBB", body)
    elif kind == b"IDAT":
      compressed += body
    elif kind == b"IEND":
      break
  if header is None:
    raise ValueError(f"{_path} has no header")
  width, height, depth, color, _, _, interlace = header
  if depth != 8 or color not in (2, 6) or interlace:
    raise ValueError(f"{_path} is not 8-bit RGB or RGBA without interlacing")
  channels = 4 if color == 6 else 3
  raw = zlib.decompress(bytes(compressed))
  stride = width * channels
  rows = []
  previous = bytearray(stride)
  offset = 0
  for _ in range(height):
    row = bytearray(raw[offset + 1:offset + 1 + stride])
    Unfilter(raw[offset], row, previous, channels)
    rows.append(row)
    previous = row
    offset += 1 + stride
  return width, height, channels, rows


def Downscale(_width, _height, _channels, _rows, _maxWidth):
  """RGB rows of squares of factor by factor pixels averaged, and the size they make."""
  factor = max(1, -(-_width // _maxWidth))
  width = _width // factor
  height = _height // factor
  out = []
  area = factor * factor
  for y in range(height):
    row = bytearray(width * 3)
    for x in range(width):
      sums = [0, 0, 0]
      for dy in range(factor):
        source = _rows[y * factor + dy]
        for dx in range(factor):
          at = (x * factor + dx) * _channels
          sums[0] += source[at]
          sums[1] += source[at + 1]
          sums[2] += source[at + 2]
      row[x * 3:x * 3 + 3] = bytes(total // area for total in sums)
    out.append(row)
  return width, height, out


def Describe(_width, _height, _rows):
  """The mean colour and the share of pixels that are not black, of RGB rows."""
  sums = [0, 0, 0]
  lit = 0
  for row in _rows:
    for x in range(_width):
      r, g, b = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
      sums[0] += r
      sums[1] += g
      sums[2] += b
      lit += 1 if r or g or b else 0
  count = max(1, _width * _height)
  mean = ", ".join(str(total // count) for total in sums)
  return f"mean RGB ({mean}), {100 * lit // count}% not black"


def Thumbnail(_width, _height, _rows):
  """Text rows of luminance, black as a space and the brightest cell as the last of RAMP; a
  character is about twice as tall as it is wide."""
  columns = min(THUMBNAIL_COLUMNS, _width)
  lines = max(1, _height * columns // _width // 2)
  cells = []
  for line in range(lines):
    y0, y1 = line * _height // lines, max(line * _height // lines + 1, (line + 1) * _height // lines)
    cells.append([])
    for column in range(columns):
      x0, x1 = column * _width // columns, max(column * _width // columns + 1, (column + 1) * _width // columns)
      total = 0
      for y in range(y0, y1):
        row = _rows[y]
        for x in range(x0, x1):
          total += 2 * row[x * 3] + 5 * row[x * 3 + 1] + row[x * 3 + 2]
      cells[-1].append(total // (8 * (y1 - y0) * (x1 - x0)))
  brightest = max(1, max(max(line) for line in cells))
  return ["".join(RAMP[level * (len(RAMP) - 1) // brightest] for level in line).rstrip() for line in cells]


def Chunk(_kind, _body):
  return struct.pack(">I", len(_body)) + _kind + _body + struct.pack(">I", zlib.crc32(_kind + _body) & 0xFFFFFFFF)


def WritePng(_width, _height, _rows):
  raw = b"".join(b"\x00" + bytes(row) for row in _rows)
  header = struct.pack(">IIBBBBB", _width, _height, 8, 2, 0, 0, 0)
  return SIGNATURE + Chunk(b"IHDR", header) + Chunk(b"IDAT", zlib.compress(raw, 9)) + Chunk(b"IEND", b"")


def main():
  parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
  parser.add_argument("capture")
  parser.add_argument("--width", type=int, default=320, help="the widest the copy may be, in pixels")
  args = parser.parse_args()

  name = pathlib.Path(args.capture).name
  width, height, channels, rows = ReadPng(args.capture)
  smallWidth, smallHeight, small = Downscale(width, height, channels, rows, args.width)
  encoded = base64.b64encode(WritePng(smallWidth, smallHeight, small)).decode("ascii")
  print(f"----- BEGIN FRAME {name} {smallWidth}x{smallHeight} of {width}x{height} -----")
  for start in range(0, len(encoded), LINE_CHARACTERS):
    print(encoded[start:start + LINE_CHARACTERS])
  print(f"----- END FRAME {name} -----")
  print(f"{name}: {Describe(smallWidth, smallHeight, small)}")
  for line in Thumbnail(smallWidth, smallHeight, small):
    print(f"|{line}")
  return 0


if __name__ == "__main__":
  sys.exit(main())
