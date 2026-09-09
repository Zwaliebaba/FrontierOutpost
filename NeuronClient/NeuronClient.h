#pragma once

// NeuronClient.h -- the umbrella header for the client library.
//
// NeuronCore.h owns the Windows macro family and pulls in <windows.h>; it therefore has to come
// first, because d3d12.h and dxgi1_6.h both expect <windows.h> to have been included already
// (AGENTS.md 4). Every other client translation unit gets D3D12 through here rather than
// repeating this order, which is the point: an include order that is load-bearing is written
// once.
//
// winrt::com_ptr is the RAII COM handle this tree uses and winrt::check_hresult the single
// error path for an HRESULT that had to succeed (NeuronCore/Debug.h). C++/WinRT ships with the
// Windows SDK, so it costs nothing against R14.

#include "NeuronCore.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <winrt/base.h>

#include <array>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

// RuntimeObject.lib, and only for one symbol. winrt::hresult_error unconditionally calls
// RoOriginateLanguageException so that a thrown HRESULT carries its message to a debugger; the
// import lives here and not in WindowsApp.lib, which is the same entry point plus 1.4 MB of
// WinRT surface this game has no use for. Both ship with the Windows SDK, so neither costs
// anything against R14.
#pragma comment(lib, "RuntimeObject.lib")
