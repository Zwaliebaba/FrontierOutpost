"""What the checkers share: the tree's files, the solution, and MSBuild projects read without MSBuild.

Imported by CheckProjectFiles.py, RunClangTidy.py and CheckFormat.py; not run on its own.

A project's settings are evaluated the way MSBuild would for one configuration and platform, as
far as the checkers need: the nearest Directory.Build.props above the project, then the project,
each group kept or dropped by its Condition. Conditions are limited to comparing
'$(Configuration)', '$(Platform)' or '$(Configuration)|$(Platform)' with a literal; anything else
is reported rather than guessed at. Imports of Visual Studio's own .props and .targets are not
followed: AGENTS.md §3 wants every setting stated in the repository, where it can be read.
"""

import pathlib
import re
import subprocess
import xml.etree.ElementTree as ET

NS = "{http://schemas.microsoft.com/developer/msbuild/2003}"

# ADR-004: the game's runtime data follows the original, not AGENTS.md. _baseline_build/ holds the
# git-ignored scratch clones, which nothing builds. CompiledShaders/ is build output (AGENTS.md §2).
EXEMPT_PREFIXES = ("GameData/", "_baseline_build/")
BUILD_OUTPUT_DIRS = {"CompiledShaders"}

CONDITION = re.compile(r"^\s*'([^']*)'\s*(==|!=)\s*'([^']*)'\s*$")
ITEM_TYPES = ("ClCompile", "ClInclude", "FxCompile", "ResourceCompile", "None", "Image", "Text",
              "CopyFileToFolders", "Natvis", "Manifest")
TOOLS = ("ClCompile", "Link", "Lib", "ResourceCompile", "FxCompile", "Midl", "Manifest")

_legacy = {}


def LegacyFiles(_root):
  """ADR-015: the files of the ltheory-old import, which a project marks <Legacy>true</Legacy>, as
    paths relative to _root. They follow the original, not AGENTS.md, and the checkers leave them be."""
  key = str(_root)
  if key not in _legacy:
    found = set()
    for solution in SolutionFiles(_root):
      for relative in SolutionProjects(solution):
        path = _root / relative
        if not path.is_file():
          continue
        folder = path.parent.relative_to(_root).as_posix()
        for group in ET.parse(path).getroot().iter(NS + "ItemGroup"):
          for item in group:
            marker = item.find(NS + "Legacy")
            if item.get("Include") and marker is not None and (marker.text or "").strip().lower() == "true":
              found.add(f"{folder}/{item.get('Include').replace(chr(92), '/')}")
    _legacy[key] = found
  return _legacy[key]


def IsExempt(_root, _relative):
  if BUILD_OUTPUT_DIRS & set(_relative.split("/")):
    return True
  return _relative.startswith(EXEMPT_PREFIXES) or _relative in LegacyFiles(_root)


def TreeFiles(_root):
  """Every file in the working tree that git does not ignore, as a relative POSIX path."""
  result = subprocess.run(["git", "-C", str(_root), "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
                          capture_output=True, check=True)
  names = sorted({name for name in result.stdout.decode("utf-8").split("\0") if name})
  return [name for name in names if (_root / name).is_file()]


def SolutionFiles(_root):
  return sorted(_root.glob("*.slnx"))


def SolutionProjects(_solution):
  """The projects a .slnx names, as paths relative to its folder, with forward slashes."""
  root = ET.parse(_solution).getroot()
  return [element.get("Path").replace("\\", "/") for element in root.iter("Project") if element.get("Path")]


def SolutionPlatforms(_solution):
  root = ET.parse(_solution).getroot()
  return [element.get("Name") for element in root.iter("Platform") if element.get("Name")]


def Tag(_element):
  return _element.tag.replace(NS, "")


class Project:
  """A .vcxproj, with its items and a way to read its settings for one configuration and platform."""

  def __init__(self, _root, _relative):
    self.root = _root
    self.relative = _relative
    self.path = _root / _relative
    self.folder = self.path.parent
    self.name = self.path.stem
    self.xml = ET.parse(self.path).getroot()
    self.faults = []

  def Configurations(self):
    return sorted(e.get("Include") for e in self.xml.iter(NS + "ProjectConfiguration") if e.get("Include"))

  def Items(self, _xml=None):
    """{item type: [path relative to the project folder, with forward slashes]}."""
    items = {}
    for group in (self.xml if _xml is None else _xml).iter(NS + "ItemGroup"):
      for item in group:
        include = item.get("Include")
        if include and Tag(item) in ITEM_TYPES:
          items.setdefault(Tag(item), []).append(include.replace("\\", "/"))
    return items

  def ItemMetadata(self, _type):
    """{path, as Items() spells it: {metadata name: value}} for the items of one type."""
    metadata = {}
    for group in self.xml.iter(NS + "ItemGroup"):
      for item in group:
        if Tag(item) == _type and item.get("Include"):
          path = item.get("Include").replace("\\", "/")
          metadata[path] = {Tag(leaf): (leaf.text or "").strip() for leaf in item}
    return metadata

  def FilterItems(self):
    path = self.path.with_name(self.path.name + ".filters")
    if not path.is_file():
      return None
    return self.Items(ET.parse(path).getroot())

  def Imports(self):
    """The Directory.Build.props MSBuild would import for this project: the nearest one above it."""
    folder = self.folder
    while True:
      candidate = folder / "Directory.Build.props"
      if candidate.is_file():
        return [candidate]
      if folder == self.root or folder.parent == folder:
        return []
      folder = folder.parent

  def Settings(self, _configuration, _platform):
    """{"Name": value} for properties and {"Tool.Name": value} for item definitions, in the order
        MSBuild would apply them. %(Name) inside a value expands to the value before it."""
    settings = {}
    for source in self.Imports() + [self.path]:
      for group in ET.parse(source).getroot():
        if Tag(group) not in ("PropertyGroup", "ItemDefinitionGroup"):
          continue
        if not self.Applies(group, _configuration, _platform, source):
          continue
        for child in group:
          if not self.Applies(child, _configuration, _platform, source):
            continue
          if Tag(group) == "PropertyGroup":
            settings[Tag(child)] = (child.text or "").strip()
          elif Tag(child) in TOOLS:
            for leaf in child:
              if not self.Applies(leaf, _configuration, _platform, source):
                continue
              key = f"{Tag(child)}.{Tag(leaf)}"
              value = (leaf.text or "").strip()
              settings[key] = value.replace(f"%({Tag(leaf)})", settings.get(key, "")).strip(";")
    return settings

  def Applies(self, _element, _configuration, _platform, _source):
    condition = _element.get("Condition")
    if not condition:
      return True
    match = CONDITION.match(condition)
    if not match:
      fault = f"{_source.relative_to(self.root).as_posix()}: a Condition the checkers cannot read: {condition!r}"
      if fault not in self.faults:
        self.faults.append(fault)
      return False
    left = match[1].replace("$(Configuration)", _configuration).replace("$(Platform)", _platform)
    if "$(" in left:
      fault = (f"{_source.relative_to(self.root).as_posix()}: a Condition on a property the checkers cannot read: "
               f"{condition!r}")
      if fault not in self.faults:
        self.faults.append(fault)
      return False
    equal = left.lower() == match[3].lower()
    return equal if match[2] == "==" else not equal


def StripCode(_text):
  """C++ text with comments and string and character literals blanked, lines kept, for scanning
    what the code itself names."""
  out = []
  i = 0
  n = len(_text)
  while i < n:
    c = _text[i]
    token = re.search(r"[A-Za-z0-9_']*$", _text[max(0, i - 64):i])[0] if c in "\"'" else ""
    if _text.startswith("//", i):
      j = _text.find("\n", i)
      j = n if j < 0 else j
      out.append(" " * (j - i))
      i = j
    elif _text.startswith("/*", i):
      j = _text.find("*/", i + 2)
      j = n if j < 0 else j + 2
      out.append(re.sub(r"[^\n]", " ", _text[i:j]))
      i = j
    elif c == '"' and re.fullmatch(r"(u8|u|U|L)?R", token or "") and re.match(r'"([^()\\\s]{0,16})\(', _text[i:]):
      delimiter = re.match(r'"([^()\\\s]{0,16})\(', _text[i:])[1]
      end = _text.find(")" + delimiter + '"', i)
      j = n if end < 0 else end + len(delimiter) + 2
      out.append(re.sub(r"[^\n]", " ", _text[i:j]))
      i = j
    elif c == "'" and token[:1].isdigit():
      out.append(c)  # a digit separator, as in 1'000'000
      i += 1
    elif c in "\"'":
      j = i + 1
      while j < n and _text[j] != c and _text[j] != "\n":
        j += 2 if _text[j] == "\\" else 1
      j = min(j + 1, n)
      out.append(re.sub(r"[^\n]", " ", _text[i:j]))
      i = j
    else:
      out.append(c)
      i += 1
  return "".join(out)
