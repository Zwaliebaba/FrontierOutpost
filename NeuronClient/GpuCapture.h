// NeuronClient/GpuCapture.h
#pragma once

#include <string>
#include <string_view>

namespace Neuron
{

/// PIX GPU captures that the program takes of itself, through WinPixEventRuntime and the
/// WinPixGpuCapturer.dll of the PIX installed on the machine (Design/ADR/ADR-018). They need PIX
/// installed, and no user at the keyboard.

/// Loads PIX's newest GPU capturer into the process. It must come before the first GraphicsDevice
/// is made. Returns false, and says why in _error, when PIX is not installed.
[[nodiscard]] bool LoadGpuCapturer(std::string& _error);

/// Asks for the next frame to be captured into the .wpix file at _pathUtf8, which the capturer
/// writes once the frame is presented. Returns false, and says why in _error, when the capturer
/// refuses, or was not loaded.
[[nodiscard]] bool CaptureNextFrame(std::string_view _pathUtf8, std::string& _error);

} // namespace Neuron
