#!/usr/bin/env python3
"""Test the checkers against small trees that keep, and then break, each rule (AGENTS.md §6).

Usage: python Build/TestCheckers.py [--clang-format PATH] [--clang-tidy PATH]

A checker that passes over a tree with nothing to check proves nothing, and the repository's
first first-party project, NeuronClient, lands only in Phase 2 of the NeuronClient migration. So
each checker runs here against a small tree that keeps every rule and must pass, and against
copies of it that each break one rule, which must fail and say which.

CheckProjectFiles.py also runs against a tree that adds a legacy lt, whose Shaders/ folder is
carved out of the exemption (N11), with a shader registry to hold to ADR-008. CheckSounds.py runs
against a small GameData of its own, with WAV files that keep and break XAudio2's formats. The
clang-format and clang-tidy cases need those tools, and are reported as skipped without them.
On Windows, one more case includes <windows.h>, which takes RunClangTidy.py through Visual
Studio's vcvarsall.bat and the Windows SDK headers. Exit 0 when every case behaves, 1 otherwise.
"""

import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
REPOSITORY = HERE.parent

SOLUTION = """<Solution>
  <Configurations>
    <BuildType Name="Debug" />
    <BuildType Name="Release" />
    <Platform Name="x64" />
    <Platform Name="ARM64" />
  </Configurations>
  <Project Path="NeuronClient/NeuronClient.vcxproj" />
</Solution>
"""

PROJECT = """<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64" />
    <ProjectConfiguration Include="Release|x64" />
    <ProjectConfiguration Include="Debug|ARM64" />
    <ProjectConfiguration Include="Release|ARM64" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>StaticLibrary</ConfigurationType>
    <PlatformToolset>v145</PlatformToolset>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Debug'">
    <UseDebugLibraries>true</UseDebugLibraries>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Release'">
    <UseDebugLibraries>false</UseDebugLibraries>
    <WholeProgramOptimization>true</WholeProgramOptimization>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <LanguageStandard>stdcpplatest</LanguageStandard>
      <ConformanceMode>true</ConformanceMode>
      <WarningLevel>Level4</WarningLevel>
      <TreatWarningAsError>true</TreatWarningAsError>
      <FloatingPointModel>Precise</FloatingPointModel>
      <AdditionalIncludeDirectories>$(SolutionDir)Shared;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Platform)'=='x64'">
    <ClCompile><EnableEnhancedInstructionSet>StreamingSIMDExtensions2</EnableEnhancedInstructionSet></ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Platform)'=='ARM64'">
    <ClCompile><EnableEnhancedInstructionSet>NotSet</EnableEnhancedInstructionSet></ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Debug'">
    <ClCompile>
      <Optimization>Disabled</Optimization>
      <RuntimeLibrary>MultiThreadedDebugDLL</RuntimeLibrary>
      <PreprocessorDefinitions>_DEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)'=='Release'">
    <ClCompile>
      <Optimization>MaxSpeed</Optimization>
      <FunctionLevelLinking>true</FunctionLevelLinking>
      <IntrinsicFunctions>true</IntrinsicFunctions>
      <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
      <PreprocessorDefinitions>NDEBUG;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClInclude Include="Window.h" />
  </ItemGroup>
  <ItemGroup>
    <ClCompile Include="Window.cpp" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
"""

FILTERS = """<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <ClInclude Include="Window.h" />
    <ClCompile Include="Window.cpp" />
  </ItemGroup>
</Project>
"""

HEADER = """#pragma once

namespace Neuron
{
class Window
{
public:
  void Show(int _widthPixels);

private:
  int m_widthPixels = 0;
};
} // namespace Neuron
"""

SOURCE = """#include "Window.h"

namespace Neuron
{
void Window::Show(int _widthPixels)
{
  m_widthPixels = _widthPixels;
}
} // namespace Neuron
"""

# A legacy file (ADR-015): registered in its project like any other, marked, and exempt from R7.
LEGACY_ITEM = """    <ClCompile Include="old_style.cxx">
      <Legacy>true</Legacy>
    </ClCompile>"""

# NeuronClient's shader folder: one shader scripts name, held by the registry under its legacy name,
# one include, and the registry itself, which is legacy code (ADR-008, ADR-015).
SHADER_DEFINITIONS = """  <ItemDefinitionGroup>
    <FxCompile>
      <ShaderModel>5.1</ShaderModel>
      <HeaderFileOutput>$(ProjectDir)CompiledShaders\\%(Filename).h</HeaderFileOutput>
    </FxCompile>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="ShaderRegistry.cpp">
      <Legacy>true</Legacy>
    </ClCompile>
  </ItemGroup>
  <ItemGroup>
    <FxCompile Include="Shaders\\PostBlurPS.hlsl">
      <ShaderType>Pixel</ShaderType>
      <VariableName>POST_BLUR_PS</VariableName>
    </FxCompile>
    <None Include="Shaders\\Common.hlsli" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />"""

SHADER_FILTERS = """    <ClCompile Include="ShaderRegistry.cpp" />
    <FxCompile Include="Shaders\\PostBlurPS.hlsl" />
    <None Include="Shaders\\Common.hlsli" />
  </ItemGroup>"""

REGISTRY = """namespace {
  Entry const kPixel[] = {
    {"post/blur.jsl", Bytes(POST_BLUR_PS)},
  };
}
"""

BLOOM_ITEM = """    <FxCompile Include="Shaders\\PostBloomPS.hlsl">
      <ShaderType>Pixel</ShaderType>
      <VariableName>POST_BLOOM_PS</VariableName>
    </FxCompile>
    <None Include="Shaders\\Common.hlsli" />"""

UNFORMATTED_SOURCE = SOURCE.replace("{\n  m_widthPixels = _widthPixels;\n}", "{ m_widthPixels=_widthPixels; }")


def Write(_root, _relative, _text):
  path = _root / _relative
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(_text, encoding="utf-8", newline="\n")


def Replace(_root, _relative, _old, _new):
  path = _root / _relative
  text = path.read_text(encoding="utf-8")
  if _old not in text:
    raise SystemExit(f"test fixture: {_old!r} not in {_relative}")
  path.write_text(text.replace(_old, _new, 1), encoding="utf-8", newline="\n")


def Conforming(_root):
  for name in (".clang-format", ".clang-tidy"):
    Write(_root, name, (REPOSITORY / name).read_text(encoding="utf-8"))
  Write(_root, "FrontierOutpost.slnx", SOLUTION)
  Write(_root, "NeuronClient/NeuronClient.vcxproj", PROJECT)
  Write(_root, "NeuronClient/NeuronClient.vcxproj.filters", FILTERS)
  Write(_root, "NeuronClient/Window.h", HEADER)
  Write(_root, "NeuronClient/Window.cpp", SOURCE)


def ShaderTree(_root):
  Conforming(_root)
  Replace(_root, "NeuronClient/NeuronClient.vcxproj", '  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />',
          SHADER_DEFINITIONS)
  Replace(_root, "NeuronClient/NeuronClient.vcxproj.filters", "  </ItemGroup>", SHADER_FILTERS)
  Write(_root, "NeuronClient/ShaderRegistry.cpp", REGISTRY)
  Write(_root, "NeuronClient/Shaders/PostBlurPS.hlsl", "float4 main() : SV_Target0 { return 0.0; }\n")
  Write(_root, "NeuronClient/Shaders/Common.hlsli", "static const float kPI = 3.1415926536;\n")


def Legacy(_root, _name, _text):
  """A legacy file in NeuronClient, marked and registered (ADR-015)."""
  Write(_root, f"NeuronClient/{_name}", _text)
  Replace(_root, "NeuronClient/NeuronClient.vcxproj", '<ClCompile Include="Window.cpp" />',
          '<ClCompile Include="Window.cpp" />\n' + LEGACY_ITEM.replace("old_style.cxx", _name))
  Replace(_root, "NeuronClient/NeuronClient.vcxproj.filters", '<ClCompile Include="Window.cpp" />',
          f'<ClCompile Include="Window.cpp" />\n    <ClCompile Include="{_name}" />')

def WriteBytes(_root, _relative, _bytes):
  path = _root / _relative
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_bytes(_bytes)


def Wav(_tag=1, _channels=2, _rate=44100, _bits=16, _align=None, _extra=b"", _dataBytes=16):
  """A small RIFF WAVE file. The defaults make 16-bit stereo PCM; _extra follows the 16-byte format."""
  align = _channels * _bits // 8 if _align is None else _align
  fmt = struct.pack("<HHIIHH", _tag, _channels, _rate, _rate * align, align, _bits) + _extra
  chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
  if _dataBytes is not None:
    chunks += b"data" + struct.pack("<I", _dataBytes) + bytes(_dataBytes)
  return b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks


SCRIPT = """type App
  function Void Initialize ()
    Sound_Play "ui/click.wav" 1
"""
ADPCM_TABLE = struct.pack("<14h", 256, 0, 512, -256, 0, 0, 192, 64, 240, 0, 460, -208, 392, -232)
FLOAT_EXTENSIBLE = struct.pack("<HHII", 22, 32, 3, 3) + bytes.fromhex("00001000800000aa00389b71")


def SoundTree(_root):
  Conforming(_root)
  Write(_root, "GameData/script/App/test.lts", SCRIPT)
  WriteBytes(_root, "GameData/sound/ui/click.wav", Wav())


def Add(_root):
  subprocess.run(["git", "init", "-q", str(_root)], check=True)
  subprocess.run(["git", "-C", str(_root), "add", "-A"], check=True)


# (name, change, expected exit code, text the output must contain)
PROJECT_CASES = [
    ("a tree that keeps every rule", None, 0, "0 fault(s)"),
    ("a project the solution does not name",
     lambda r: Replace(r, "FrontierOutpost.slnx", '  <Project Path="NeuronClient/NeuronClient.vcxproj" />\n', ""),
     1, "is not in FrontierOutpost.slnx"),
    ("a Win32 configuration",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj", '<ItemGroup Label="ProjectConfigurations">',
                       '<ItemGroup Label="ProjectConfigurations">\n    <ProjectConfiguration Include="Debug|Win32" />'),
     1, "configurations are"),
    ("Release at /W3",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj", "<Optimization>MaxSpeed</Optimization>",
                       "<Optimization>MaxSpeed</Optimization><WarningLevel>Level3</WarningLevel>"),
     1, "WarningLevel is 'Level3'"),
    ("ARM64 without a stated /arch",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj",
                       "<EnableEnhancedInstructionSet>NotSet</EnableEnhancedInstructionSet>", ""),
     1, "does not state EnableEnhancedInstructionSet"),
    ("x64 at /arch:AVX2, which N10 dropped",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj",
                       "<EnableEnhancedInstructionSet>StreamingSIMDExtensions2</EnableEnhancedInstructionSet>",
                       "<EnableEnhancedInstructionSet>AdvancedVectorExtensions2</EnableEnhancedInstructionSet>"),
     1, "expected 'StreamingSIMDExtensions2'"),
    ("Debug and Release disagreeing on a setting",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj", "<Optimization>Disabled</Optimization>",
                       "<Optimization>Disabled</Optimization><SDLCheck>true</SDLCheck>"),
     1, "disagree on ClCompile.SDLCheck"),
    ("a Condition the checker cannot read",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj", "<PropertyGroup Label=\"Configuration\">",
                       "<PropertyGroup Condition=\"Exists('x')\"><Foo>1</Foo></PropertyGroup>\n"
                       "  <PropertyGroup Label=\"Configuration\">"),
     1, "a Condition the checkers cannot read"),
    ("a header on disk that the project does not list",
     lambda r: Write(r, "NeuronClient/Stray.h", "#pragma once\n"),
     1, "is not in NeuronClient.vcxproj"),
    ("a header the project lists and its .filters does not",
     lambda r: (Write(r, "NeuronClient/Extra.h", "#pragma once\n"),
                Replace(r, "NeuronClient/NeuronClient.vcxproj", '<ClInclude Include="Window.h" />',
                        '<ClInclude Include="Window.h" />\n    <ClInclude Include="Extra.h" />')),
     1, "which its .filters does not"),
    ("a header in a subfolder",
     lambda r: Write(r, "NeuronClient/Detail/Helper.h", "#pragma once\n"),
     1, "in a subfolder"),
    ("a .hpp file",
     lambda r: Write(r, "NeuronClient/Thing.hpp", "#pragma once\n"),
     1, "(R7)"),
    ("a file name that is not PascalCase",
     lambda r: Write(r, "NeuronClient/window_utils.h", "#pragma once\n"),
     1, "PascalCase"),
    ("an I prefix on a type",
     lambda r: Replace(r, "NeuronClient/Window.h", "class Window\n", "class ITransport;\nclass Window\n"),
     1, "ITransport carries a prefix or suffix (R2)"),
    ("an Impl suffix on a type",
     lambda r: Replace(r, "NeuronClient/Window.h", "class Window\n", "struct WindowImpl;\nclass Window\n"),
     1, "WindowImpl carries a prefix or suffix (R2)"),
    ("a British spelling in an identifier",
     lambda r: Replace(r, "NeuronClient/Window.h", "int m_widthPixels = 0;",
                       "int m_widthPixels = 0;\n  int m_colour = 0;"),
     1, "m_colour uses the British spelling (R11)"),
    ("a British spelling in a comment, which is prose",
     lambda r: Replace(r, "NeuronClient/Window.h", "class Window\n",
                       "// The window's colour and behaviour.\nclass Window\n"),
     0, "0 fault(s)"),
    ("NOMINMAX in the project file",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj", "_DEBUG;%(PreprocessorDefinitions)",
                       "_DEBUG;NOMINMAX;%(PreprocessorDefinitions)"),
     1, "defines NOMINMAX"),
    ("the project's own folder on its include path",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj", "$(SolutionDir)Shared;", "$(ProjectDir);"),
     1, "own folder on the include path"),
    ("an absolute path",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj", "$(SolutionDir)Shared;", "C:\\SDK\\include;"),
     1, "an absolute path"),
    ("a HeaderFilterRegex that misses the project",
     lambda r: Replace(r, ".clang-tidy", "|NeuronClient|", "|Nothing|"),
     1, "HeaderFilterRegex does not match NeuronClient/"),
    ("a C++ file outside every project",
     lambda r: Write(r, "Tools/Stray.cpp", "int main() {}\n"),
     1, "outside every project folder"),
    ("a legacy file, which keeps only ADR-015's rules",
     lambda r: Legacy(r, "old_style.cxx", ""),
     0, "1 legacy file(s)"),
    ("the same file without its marker",
     lambda r: (Legacy(r, "old_style.cxx", ""),
                Replace(r, "NeuronClient/NeuronClient.vcxproj",
                        '<ClCompile Include="old_style.cxx">\n      <Legacy>true</Legacy>\n    </ClCompile>',
                        '<ClCompile Include="old_style.cxx" />')),
     1, "C++ lives in .h and .cpp only (R7)"),
    ("a legacy file the project does not list, which is registered like any other",
     lambda r: Write(r, "NeuronClient/old_style.cpp", ""),
     1, "is not in NeuronClient.vcxproj"),
]

SHADER_CASES = [
    ("NeuronClient's shaders, each compiled and held by the registry", None, 0, "0 fault(s)"),
    ("a shader the project does not compile",
     lambda r: Write(r, "NeuronClient/Shaders/PostBloomPS.hlsl", ""),
     1, "Shaders/PostBloomPS.hlsl: is not in NeuronClient.vcxproj"),
    ("an include the project does not list",
     lambda r: Write(r, "NeuronClient/Shaders/Math.hlsli", ""),
     1, "Shaders/Math.hlsli: is not in NeuronClient.vcxproj"),
    ("a shader in a subfolder",
     lambda r: Write(r, "NeuronClient/Shaders/post/BlurPS.hlsl", ""),
     1, "a shader outside the project's Shaders/ folder"),
    ("a shader named against ADR-008",
     lambda r: Write(r, "NeuronClient/Shaders/post_blur.hlsl", ""),
     1, "(R7, ADR-008)"),
    ("a shader the registry does not hold",
     lambda r: (Write(r, "NeuronClient/Shaders/PostBloomPS.hlsl", ""),
                Replace(r, "NeuronClient/NeuronClient.vcxproj", '    <None Include="Shaders\\Common.hlsli" />', BLOOM_ITEM),
                Replace(r, "NeuronClient/NeuronClient.vcxproj.filters", "<None",
                        '<FxCompile Include="Shaders\\PostBloomPS.hlsl" />\n    <None')),
     1, "PostBloomPS.hlsl: is not in NeuronClient/ShaderRegistry.cpp under its legacy name"),
    ("a shader outside the registry that a source binds by its array",
     lambda r: (Write(r, "NeuronClient/Shaders/PostBloomPS.hlsl", ""),
                Replace(r, "NeuronClient/NeuronClient.vcxproj", '    <None Include="Shaders\\Common.hlsli" />', BLOOM_ITEM),
                Replace(r, "NeuronClient/NeuronClient.vcxproj.filters", "<None",
                        '<FxCompile Include="Shaders\\PostBloomPS.hlsl" />\n    <None'),
                Replace(r, "NeuronClient/Window.cpp", "namespace Neuron\n{\n",
                        "namespace Neuron\n{\nextern const unsigned char POST_BLOOM_PS[];\n")),
     0, "0 fault(s)"),
    ("a registry entry with no shader",
     lambda r: Replace(r, "NeuronClient/ShaderRegistry.cpp", "  };", '    {"post/bloom.jsl", Bytes(POST_BLOOM_PS)},\n  };'),
     1, "holds post/bloom.jsl, but NeuronClient.vcxproj compiles no Shaders/PostBloomPS.hlsl"),
    ("a registry entry with another shader's array",
     lambda r: Replace(r, "NeuronClient/ShaderRegistry.cpp", "Bytes(POST_BLUR_PS)", "Bytes(UI_TEXTURE_PS)"),
     1, "holds post/blur.jsl as UI_TEXTURE_PS"),
    ("a registry entry the checker cannot read",
     lambda r: Replace(r, "NeuronClient/ShaderRegistry.cpp", '{"post/blur.jsl", Bytes(POST_BLUR_PS)},',
                       '{ "post/blur.jsl", Bytes(POST_BLUR_PS) },'),
     1, "not one entry the checker can read"),
    ("a shader's array not named for its file",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj", "<VariableName>POST_BLUR_PS</VariableName>",
                       "<VariableName>g_blur</VariableName>"),
     1, "expected 'POST_BLUR_PS' (R3)"),
    ("a pixel shader compiled as a vertex shader",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj", "<ShaderType>Pixel</ShaderType>",
                       "<ShaderType>Vertex</ShaderType>"),
     1, "where its name says 'Pixel'"),
    ("shaders at another Shader Model in Release",
     lambda r: Replace(r, "NeuronClient/NeuronClient.vcxproj", "  <ItemGroup>\n    <ClCompile Include=\"ShaderRegistry.cpp\">",
                       "  <ItemDefinitionGroup Condition=\"'$(Configuration)'=='Release'\">\n"
                       "    <FxCompile><ShaderModel>6.0</ShaderModel></FxCompile>\n  </ItemDefinitionGroup>\n"
                       "  <ItemGroup>\n    <ClCompile Include=\"ShaderRegistry.cpp\">"),
     1, "disagree on FxCompile.ShaderModel"),
]

FORMAT_CASES = [
    ("a formatted tree", None, [], 0, "0 not formatted"),
    ("an unformatted source",
     lambda r: Write(r, "NeuronClient/Window.cpp", UNFORMATTED_SOURCE),
     [], 1, "1 not formatted"),
    ("the same source, after --fix",
     lambda r: Write(r, "NeuronClient/Window.cpp", UNFORMATTED_SOURCE),
     ["--fix"], 0, "reformatted"),
    ("an unformatted legacy file, which is left alone",
     lambda r: Legacy(r, "Legacy.cpp", "int  main( ){return 0;}\n"),
     [], 0, "0 not formatted"),
]
TIDY_CASES = [
    ("a tree that keeps the naming table", None, 0, "0 with diagnostics"),
    ("a parameter without its _",
     lambda r: (Replace(r, "NeuronClient/Window.cpp", "Show(int _widthPixels)\n{\n  m_widthPixels = _widthPixels;",
                        "Show(int widthPixels)\n{\n  m_widthPixels = widthPixels;")),
     1, "invalid case style for parameter 'widthPixels'"),
    ("a private member without its m_, in a header",
     lambda r: (Replace(r, "NeuronClient/Window.h", "int m_widthPixels = 0;",
                        "int m_widthPixels = 0;\n  int count = 0;")),
     1, "invalid case style for private member 'count'"),
]
SOUND_CASES = [
    ("sounds that are all there and playable", None, 0, "All 1 WAV file(s) can be played"),
    ("a named sound with no WAV file, which only warns",
     lambda r: Write(r, "GameData/script/App/test.lts", SCRIPT + '    Sound_Play "ui/missing.wav" 1\n'),
     0, "  ui/missing.wav  (GameData/script/App/test.lts:4)"),
    ("a missing sound whose Ogg source waits for conversion",
     lambda r: (Write(r, "GameData/script/App/test.lts", SCRIPT + '    Sound_Play "ui/missing.wav" 1\n'),
                WriteBytes(r, "GameData/sound/ui/missing.ogg", b"OggS")),
     0, "converts from ui/missing.ogg, O10"),
    ("a sound named in C++",
     lambda r: Replace(r, "NeuronClient/Window.cpp", "namespace Neuron\n{\n",
                       'namespace Neuron\n{\n\nchar const* const FIRE = "weapon/fire.wav";\n'),
     0, "  weapon/fire.wav  (NeuronClient/Window.cpp:"),
    ("sounds named only in comments, which do not count",
     lambda r: (Write(r, "GameData/script/App/test.lts", SCRIPT + '    #\n      Sound_Play "ui/gone.wav" 1\n'),
                Replace(r, "NeuronClient/Window.cpp", "namespace Neuron\n{\n",
                        'namespace Neuron\n{\n\n// Sound_Play2D("ui/old.wav");\n')),
     0, "name 1 sound(s). 0 have no WAV file"),
    ("24-bit PCM at 96 kHz, as most of the game's sounds are",
     lambda r: WriteBytes(r, "GameData/sound/ui/click.wav", Wav(_bits=24, _rate=96000, _dataBytes=12)),
     0, "All 1 WAV file(s) can be played"),
    ("32-bit float through WAVE_FORMAT_EXTENSIBLE",
     lambda r: WriteBytes(r, "GameData/sound/ui/click.wav", Wav(_tag=0xFFFE, _bits=32, _extra=FLOAT_EXTENSIBLE)),
     0, "All 1 WAV file(s) can be played"),
    ("Microsoft ADPCM with its standard coefficients",
     lambda r: WriteBytes(r, "GameData/sound/ui/click.wav",
                          Wav(_tag=2, _channels=1, _bits=4, _align=256, _dataBytes=256,
                              _extra=struct.pack("<HHH", 32, 500, 7) + ADPCM_TABLE)),
     0, "All 1 WAV file(s) can be played"),
    ("an IMA ADPCM file, which XAudio2 refuses",
     lambda r: WriteBytes(r, "GameData/sound/ui/click.wav", Wav(_tag=0x11, _bits=4, _align=512, _dataBytes=512)),
     1, "format 0x0011 (IMA ADPCM)"),
    ("Microsoft ADPCM with another coefficient table",
     lambda r: WriteBytes(r, "GameData/sound/ui/click.wav",
                          Wav(_tag=2, _channels=1, _bits=4, _align=256, _dataBytes=256,
                              _extra=struct.pack("<HHH", 32, 500, 7) + bytes(28))),
     1, "non-standard coefficients"),
    ("a WAV file without a data chunk",
     lambda r: WriteBytes(r, "GameData/sound/ui/click.wav", Wav(_dataBytes=None)),
     1, "no data chunk"),
    ("12-bit PCM",
     lambda r: WriteBytes(r, "GameData/sound/ui/click.wav", Wav(_bits=12, _align=4)),
     1, "12-bit PCM"),
]

WINDOWS_CASE = ("a source that includes <windows.h> and takes a wide IDC_ARROW, through vcvarsall.bat",
                lambda r: (Replace(r, "NeuronClient/Window.cpp", '#include "Window.h"\n',
                                   '#include "Window.h"\n\n#include <windows.h>\n'),
                           Replace(r, "NeuronClient/Window.cpp", "m_widthPixels = _widthPixels;\n",
                                   "m_widthPixels = _widthPixels;\n  SetCursor(LoadCursorW(nullptr, IDC_ARROW));\n")),
                0, "0 with diagnostics")


def Run(_script, _root, _extra):
  result = subprocess.run([sys.executable, str(HERE / _script), "--root", str(_root), *_extra],
                          capture_output=True, text=True)
  return result.returncode, result.stdout + result.stderr


def Check(_script, _cases, _extra, _failures, _withArgs=False, _base=Conforming):
  for case in _cases:
    name, change = case[0], case[1]
    extra = list(_extra) + (list(case[2]) if _withArgs else [])
    code, text = case[-2], case[-1]
    with tempfile.TemporaryDirectory() as directory:
      root = pathlib.Path(directory)
      _base(root)
      if change:
        change(root)
      Add(root)
      got, output = Run(_script, root, extra)
    verdict = "ok  " if got == code and text in output else "FAIL"
    print(f"{verdict} {_script}: {name}")
    if verdict == "FAIL":
      _failures.append(name)
      print(f"     expected exit {code} and {text!r}; got exit {got}:")
      print("     " + output.strip().replace("\n", "\n     "))


def main():
  parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
  parser.add_argument("--clang-format", dest="clangFormat")
  parser.add_argument("--clang-tidy", dest="clangTidy")
  args = parser.parse_args()
  failures = []

  Check("CheckProjectFiles.py", PROJECT_CASES, [], failures)
  Check("CheckProjectFiles.py", SHADER_CASES, [], failures, _base=ShaderTree)
  Check("CheckSounds.py", SOUND_CASES, [], failures, _base=SoundTree)

  clangFormat = args.clangFormat or shutil.which("clang-format")
  if clangFormat:
    Check("CheckFormat.py", FORMAT_CASES, ["--clang-format", clangFormat], failures, _withArgs=True)
  else:
    print("skip CheckFormat.py: no clang-format (pass --clang-format)")

  tidyArgs = ["--clang-tidy", args.clangTidy] if args.clangTidy else []
  cases = TIDY_CASES + ([WINDOWS_CASE] if os.name == "nt" else [])
  probe = subprocess.run([sys.executable, str(HERE / "RunClangTidy.py"), "--root", str(REPOSITORY), *tidyArgs],
                         capture_output=True, text=True)
  if args.clangTidy or shutil.which("clang-tidy") or os.name == "nt":
    Check("RunClangTidy.py", cases, tidyArgs, failures)
  else:
    print(f"skip RunClangTidy.py: no clang-tidy (pass --clang-tidy). {probe.stdout.strip()}")

  print(f"{len(failures)} failure(s).")
  return 1 if failures else 0


if __name__ == "__main__":
  sys.exit(main())
