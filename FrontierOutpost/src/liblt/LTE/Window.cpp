#include "Window.h"

#include "Array.h"
#include "Keyboard.h"
#include "Matrix.h"
#include "Mouse.h"
#include "Pointer.h"
#include "ProgramLog.h"
#include "Renderer.h"
#include "RendererCore.h"
#include "String.h"
#include "Texture2D.h"
#include "Viewport.h"

/* NeuronClient's Window.h, which the quoted form cannot reach from here: it finds this folder's
   own Window.h first. */
#include <Window.h>

#include <string>
#include <variant>

/* NeuronClient's keys as the engine's (ADR-012). Each of the 101 has a name in both. */
#define KEY_MAP_XY                                                             \
  XY(A, A) XY(B, B) XY(C, C) XY(D, D) XY(E, E) XY(F, F) XY(G, G) XY(H, H)      \
  XY(I, I) XY(J, J) XY(K, K) XY(L, L) XY(M, M) XY(N, N) XY(O, O) XY(P, P)      \
  XY(Q, Q) XY(R, R) XY(S, S) XY(T, T) XY(U, U) XY(V, V) XY(W, W) XY(X, X)      \
  XY(Y, Y) XY(Z, Z)                                                            \
  XY(N0, Digit0) XY(N1, Digit1) XY(N2, Digit2) XY(N3, Digit3)                  \
  XY(N4, Digit4) XY(N5, Digit5) XY(N6, Digit6) XY(N7, Digit7)                  \
  XY(N8, Digit8) XY(N9, Digit9)                                                \
  XY(NP0, Numpad0) XY(NP1, Numpad1) XY(NP2, Numpad2) XY(NP3, Numpad3)          \
  XY(NP4, Numpad4) XY(NP5, Numpad5) XY(NP6, Numpad6) XY(NP7, Numpad7)          \
  XY(NP8, Numpad8) XY(NP9, Numpad9)                                            \
  XY(F1, F1) XY(F2, F2) XY(F3, F3) XY(F4, F4) XY(F5, F5) XY(F6, F6)            \
  XY(F7, F7) XY(F8, F8) XY(F9, F9) XY(F10, F10) XY(F11, F11) XY(F12, F12)      \
  XY(F13, F13) XY(F14, F14) XY(F15, F15)                                       \
  XY(Add, NumpadAdd)                                                           \
  XY(BackSpace, Backspace)                                                     \
  XY(BackSlash, Backslash)                                                     \
  XY(Comma, Comma)                                                             \
  XY(Dash, Minus)                                                              \
  XY(Delete, Delete)                                                           \
  XY(Divide, NumpadDivide)                                                     \
  XY(Down, Down)                                                               \
  XY(End, End)                                                                 \
  XY(Equal, Equal)                                                             \
  XY(Escape, Escape)                                                           \
  XY(Home, Home)                                                               \
  XY(Insert, Insert)                                                           \
  XY(LBracket, LeftBracket)                                                    \
  XY(Left, Left)                                                               \
  XY(Menu, Menu)                                                               \
  XY(Multiply, NumpadMultiply)                                                 \
  XY(PageDown, PageDown)                                                       \
  XY(PageUp, PageUp)                                                           \
  XY(Pause, Pause)                                                             \
  XY(Period, Period)                                                           \
  XY(Quote, Apostrophe)                                                        \
  XY(RBracket, RightBracket)                                                   \
  XY(Return, Enter)                                                            \
  XY(Right, Right)                                                             \
  XY(SemiColon, Semicolon)                                                     \
  XY(Slash, Slash)                                                             \
  XY(Space, Space)                                                             \
  XY(Subtract, NumpadSubtract)                                                 \
  XY(Tab, Tab)                                                                 \
  XY(Tilde, Grave)                                                             \
  XY(Up, Up)                                                                   \
  XY(LAlt, LeftAlt)                                                            \
  XY(RAlt, RightAlt)                                                           \
  XY(LControl, LeftControl)                                                    \
  XY(RControl, RightControl)                                                   \
  XY(LShift, LeftShift)                                                        \
  XY(RShift, RightShift)                                                       \
  XY(LSystem, LeftSystem)                                                      \
  XY(RSystem, RightSystem)

namespace {
  Vector<Window>& GetStack() {
    static Vector<Window> stack;
    return stack;
  }

  /* Key_SIZE for Neuron::Key::Unknown. */
  Key Key_FromNeuron(Neuron::Key key) {
    switch (key) {
      #define XY(x, y) case Neuron::Key::y: return Key_##x;
      KEY_MAP_XY
      #undef XY
      default: return Key_SIZE;
    }
  }

  MouseButton MouseButton_FromNeuron(Neuron::MouseButton button) {
    switch (button) {
      case Neuron::MouseButton::Left: return MouseButton_Left;
      case Neuron::MouseButton::Right: return MouseButton_Right;
      case Neuron::MouseButton::Middle: return MouseButton_Middle;
      case Neuron::MouseButton::X1: return MouseButton_X1;
      default: return MouseButton_X2;
    }
  }

  struct WindowImpl : public WindowT {
    Neuron::Window impl;
    /* After the window, so that it goes first (Design/ADR/ADR-007). Made when
       the first frame is shown, since the device is made after the window. */
    Neuron::SwapChain swapChain;
    String title;
    Viewport viewport;
    V2U size;
    bool hasFocus;
    bool sync;

    WindowImpl(
        String const& title,
        V2U const& size,
        bool border,
        bool fullscreen) :
      title(title),
      size(size),
      hasFocus(true),
      sync(false)
    {
      if (fullscreen)
        Log_Critical("Window: exclusive fullscreen is not supported (ADR-012)");

      viewport = Viewport_Create(0, size, 1, true);

      Neuron::Window::Desc desc;
      desc.titleUtf8 = title;
      desc.widthPixels = size.x;
      desc.heightPixels = size.y;
      desc.border = border;
      desc.cursorVisible = false;

      std::string error;
      if (!Neuron::Window::Open(desc, impl, error))
        Log_Critical(error);

      /* Vertical sync starts off, as SFML turned it off for every new window
         (N7). */
      viewport->size = size;
    }

    void Close() {
      swapChain = Neuron::SwapChain();
      impl.Close();
    }

    /* Shows the frame liblt drew, GL's default framebuffer, through the
       present pass, which flips it once (plan section 5.5), and starts the next
       frame. Offscreen, for the smoke mode, it only starts the next. */
    void Display() {
      Neuron::GraphicsDevice& device = Renderer_Device();
      if (!Renderer_IsOffscreen()) {
        if (!swapChain) {
          Neuron::SwapChain::Desc desc;
          desc.window = &impl;
          desc.widthPixels = size.x;
          desc.heightPixels = size.y;
          desc.vsync = sync;
          swapChain = device.CreateSwapChain(desc);
        }
        swapChain.Present(Renderer_GetFrame().texture);
      }
      device.EndFrame();
      Renderer_TakeDeviceMessages();
      device.BeginFrame();
    }

    V2I GetCursorPos() const {
      Neuron::ClientPoint p = impl.CursorPosition();
      return V2I(p.xPixels, p.yPixels);
    }

    V2U GetSize() const {
      return size;
    }

    bool HasFocus() const {
      return hasFocus;
    }

    bool IsOpen() const {
      return impl.IsOpen();
    }

    void SetSync(bool sync) {
      /* The swap chain takes it when it is made, so one made already is made
         again. */
      if (sync != this->sync) {
        this->sync = sync;
        swapChain = Neuron::SwapChain();
      }
    }

    void Update() {
      Neuron::WindowEvent e;
      while (impl.PollEvent(e)) {
        if (Neuron::ResizeEvent const* resize = std::get_if<Neuron::ResizeEvent>(&e)) {
          size.x = resize->widthPixels;
          size.y = resize->heightPixels;
          viewport->size = V2((float)resize->widthPixels, (float)resize->heightPixels);
          if (swapChain)
            swapChain.Resize(resize->widthPixels, resize->heightPixels);
        }

        else if (Neuron::KeyEvent const* key = std::get_if<Neuron::KeyEvent>(&e)) {
          Key engineKey = Key_FromNeuron(key->key);
          if (engineKey != Key_SIZE) {
            if (key->down)
              Keyboard_AddDown(engineKey);
            else
              Keyboard_AddUp(engineKey);
          }
        }

        else if (Neuron::MouseButtonEvent const* button = std::get_if<Neuron::MouseButtonEvent>(&e))
          Mouse_SetPressed(MouseButton_FromNeuron(button->button), button->down);

        else if (Neuron::MouseMoveEvent const* move = std::get_if<Neuron::MouseMoveEvent>(&e))
          Mouse_UpdatePos(V2I(move->position.xPixels, move->position.yPixels));

        else if (Neuron::MouseWheelEvent const* wheel = std::get_if<Neuron::MouseWheelEvent>(&e)) {
          /* Whole notches, as SFML counted them. TODO : Improve precision on Windows. */
          if (hasFocus)
            Mouse_SetScrollDelta((float)(int)wheel->notches);
        }

        else if (Neuron::FocusEvent const* focus = std::get_if<Neuron::FocusEvent>(&e))
          hasFocus = focus->gained;

        else if (Neuron::CharacterEvent const* text = std::get_if<Neuron::CharacterEvent>(&e)) {
          if (text->codePoint >= 32 && text->codePoint <= 126)
            Keyboard_AddText((char)text->codePoint);
        }
      }
    }
  };
}

Window Window_Create(
  String const& title,
  V2U const& size,
  bool border,
  bool fullscreen)
{
  return new WindowImpl(title, size, border, fullscreen);
}

Window Window_Get() {
  return GetStack().size() ? GetStack().back() : nullptr;
}

void Window_Pop() {
  Viewport_Pop();
  GetStack().pop();
}

void Window_Push(Window const& window) {
  GetStack().push(window);
  Viewport_Push(((WindowImpl*)window.t)->viewport);
}
