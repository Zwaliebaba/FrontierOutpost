#include "WglBridge.h"

#include "String.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>

namespace {
  /* What liblt asked SFML for: a 32-bit color buffer, and no depth, stencil or multisampling. */
  const int kColorBits = 32;
  const int kDepthBits = 0;
  const int kStencilBits = 0;

  /* SFML's score for a pixel format: lower is better. A format short of what was asked for costs
     far more than one with too much, and one without hardware acceleration is the last resort. */
  int Score(PIXELFORMATDESCRIPTOR const& format) {
    int colorDiff = kColorBits -
      (format.cRedBits + format.cGreenBits + format.cBlueBits + format.cAlphaBits);
    int depthDiff = kDepthBits - format.cDepthBits;
    int stencilDiff = kStencilBits - format.cStencilBits;
    colorDiff *= colorDiff > 0 ? 100000 : 1;
    depthDiff *= depthDiff > 0 ? 100000 : 1;
    stencilDiff *= stencilDiff > 0 ? 100000 : 1;

    int score = std::abs(colorDiff) + std::abs(depthDiff) + std::abs(stencilDiff);
    if (format.dwFlags & PFD_GENERIC_FORMAT)
      score += 100000000;
    return score;
  }

  /* The best double-buffered RGBA format that draws to a window with OpenGL, by Score, as SFML
     chose among the same formats. */
  int ChooseFormat(HDC dc) {
    PIXELFORMATDESCRIPTOR format = {};
    int count = DescribePixelFormat(dc, 1, sizeof(format), &format);
    DWORD const required = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    int best = 0;
    int bestScore = 0x7FFFFFFF;
    for (int i = 1; i <= count; ++i) {
      if (!DescribePixelFormat(dc, i, sizeof(format), &format))
        continue;
      if ((format.dwFlags & required) != required || format.iPixelType != PFD_TYPE_RGBA)
        continue;
      int score = Score(format);
      if (score < bestScore) {
        bestScore = score;
        best = i;
      }
    }
    return best;
  }

  String Failed(char const* call) {
    return String("WGL: ") + call + " failed with error " + ToString((uint)GetLastError());
  }
}

WglBridge::WglBridge() :
  window(nullptr),
  deviceContext(nullptr),
  renderContext(nullptr)
  {}

WglBridge::~WglBridge() {
  Destroy();
}

bool WglBridge::Create(void* windowHandle, String& error) {
  HWND hwnd = (HWND)windowHandle;
  HDC dc = GetDC(hwnd);
  if (!dc) {
    error = Failed("GetDC");
    return false;
  }
  window = hwnd;
  deviceContext = dc;

  int format = ChooseFormat(dc);
  if (!format) {
    error = "WGL: no double-buffered RGBA pixel format draws to a window with OpenGL";
    return false;
  }
  PIXELFORMATDESCRIPTOR descriptor = {};
  DescribePixelFormat(dc, format, sizeof(descriptor), &descriptor);
  if (!SetPixelFormat(dc, format, &descriptor)) {
    error = Failed("SetPixelFormat");
    return false;
  }

  HGLRC context = wglCreateContext(dc);
  if (!context) {
    error = Failed("wglCreateContext");
    return false;
  }
  renderContext = context;
  if (!wglMakeCurrent(dc, context)) {
    error = Failed("wglMakeCurrent");
    return false;
  }
  return true;
}

void WglBridge::Destroy() {
  if (renderContext) {
    if (wglGetCurrentContext() == (HGLRC)renderContext)
      wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext((HGLRC)renderContext);
    renderContext = nullptr;
  }
  if (deviceContext) {
    ReleaseDC((HWND)window, (HDC)deviceContext);
    deviceContext = nullptr;
  }
  window = nullptr;
}

void WglBridge::SetSwapInterval(int interval) {
  typedef BOOL (WINAPI* SwapIntervalFunction)(int);
  SwapIntervalFunction swapInterval =
    (SwapIntervalFunction)(void*)wglGetProcAddress("wglSwapIntervalEXT");
  if (swapInterval)
    swapInterval(interval);
}

void WglBridge::Swap() {
  if (deviceContext && renderContext)
    SwapBuffers((HDC)deviceContext);
}
