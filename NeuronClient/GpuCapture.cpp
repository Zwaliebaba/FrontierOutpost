// NeuronClient/GpuCapture.cpp
#include "pch.h"

// PIX's programmatic capture in every configuration, as DrawContext.cpp has its events
// (Design/ADR/ADR-018).
#define USE_PIX
#include <d3d12.h>
#include <pix3.h>

#include "GpuCapture.h"
#include "Unicode.h"

#include <format>

namespace Neuron
{

bool LoadGpuCapturer(std::string& _error)
{
  if (PIXLoadLatestWinPixGpuCapturerLibrary() == nullptr)
  {
    _error = "PIX: no WinPixGpuCapturer.dll was found; a GPU capture needs PIX installed";
    return false;
  }
  return true;
}

bool CaptureNextFrame(std::string_view _pathUtf8, std::string& _error)
{
  const std::wstring path = Utf8ToUtf16(_pathUtf8);
  const HRESULT result = PIXGpuCaptureNextFrames(path.c_str(), 1);
  if (FAILED(result))
  {
    _error = std::format("PIX: PIXGpuCaptureNextFrames failed with 0x{:08X}", static_cast<unsigned>(result));
    return false;
  }
  return true;
}

} // namespace Neuron
