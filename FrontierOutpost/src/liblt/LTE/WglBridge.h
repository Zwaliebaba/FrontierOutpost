#ifndef LTE_WglBridge_h__
#define LTE_WglBridge_h__

#include "Common.h"

/* OpenGL on NeuronClient's window, through WGL (ADR-012). The window reaches it as an opaque
   handle, its HWND, so that no Windows header is included here. It is temporary: Phase 4 of the
   NeuronClient plan deletes it with OpenGL. */
struct WglBridge {
  void* window;
  void* deviceContext;
  void* renderContext;

  WglBridge();
  ~WglBridge();

  /* Gives the window the pixel format SFML chose for it, and makes an OpenGL context on it
     current. On failure, returns false and says why in 'error'. */
  bool Create(void* window, String& error);

  void Destroy();

  /* 0 presents at once; 1 waits for the vertical blank. */
  void SetSwapInterval(int interval);

  void Swap();
};

#endif
