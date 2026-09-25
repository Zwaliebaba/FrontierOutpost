#include "Window.h"

#include "Array.h"
#include "Keyboard.h"
#include "Matrix.h"
#include "Mouse.h"
#include "Pointer.h"
#include "Renderer.h"
#include "String.h"
#include "Texture2D.h"
#include "Viewport.h"

#include "SFML/Window.hpp"

namespace {
  Vector<Window>& GetStack() {
    static Vector<Window> stack;
    return stack;
  }

  void ProcessMouseEvent(sf::Mouse::Button button, bool pressed) {
    switch (button) {
      case sf::Mouse::Left:
        Mouse_SetPressed(MouseButton_Left, pressed); break;
      case sf::Mouse::Right:
        Mouse_SetPressed(MouseButton_Right, pressed); break;
      case sf::Mouse::Middle:
        Mouse_SetPressed(MouseButton_Middle, pressed); break;
      case sf::Mouse::XButton1:
        Mouse_SetPressed(MouseButton_X1, pressed); break;
      case sf::Mouse::XButton2:
        Mouse_SetPressed(MouseButton_X2, pressed); break;
      default: break;
    }
  }

  struct WindowImpl : public WindowT {
    sf::Window impl;
    String title;
    Viewport viewport;
    V2U size;
    uint bpp;
    bool hasFocus;

    WindowImpl(
        String const& title,
        V2U const& size,
        bool border,
        bool fullscreen) :
      title(title),
      size(size),
      bpp(32),
      hasFocus(true)
    {
      viewport = Viewport_Create(0, size, 1, true);
      impl.create(
        sf::VideoMode(size.x, size.y, bpp),
        title,
        fullscreen
          ? sf::Style::Fullscreen 
          : border
            ? sf::Style::Default
            : sf::Style::None);
      impl.setMouseCursorVisible(false);
      viewport->size = size;

      // sf::Vector2i p = sf::Mouse::getPosition(impl);
      // sf::Mouse::setPosition(p, impl);
    }

    void Close() {
      impl.close();
    }

    void Display() {
      impl.display();
    }

    void* GetImplData() {
      return &impl;
    }

    V2U GetSize() const {
      return size;
    }

    bool HasFocus() const {
      return hasFocus;
    }

    bool IsOpen() const {
      return impl.isOpen();
    }

    void SetSync(bool sync) {
      impl.setVerticalSyncEnabled(sync);
    }

    void Update() {
      sf::Event e;
      while (impl.pollEvent(e)) {
        if (e.type == sf::Event::Resized) {
          float w = (float)e.size.width;
          float h = (float)e.size.height;
          size.x = e.size.width;
          size.y = e.size.height;
          viewport->size = V2(w, h);
        }

        else if (e.type == sf::Event::KeyPressed) {
          if (e.key.code != sf::Keyboard::Unknown)
            Keyboard_AddDown((int)e.key.code);
        }

        else if (e.type == sf::Event::MouseButtonPressed)
          ProcessMouseEvent(e.mouseButton.button, true);

        else if (e.type == sf::Event::MouseButtonReleased)
          ProcessMouseEvent(e.mouseButton.button, false);

        else if (e.type == sf::Event::MouseMoved) {
          V2I p(e.mouseMove.x, e.mouseMove.y);
          Mouse_UpdatePos(p);  
        }

        else if (e.type == sf::Event::MouseWheelMoved && hasFocus) {
          /* TODO : Improve precision on Windows. */
          Mouse_SetScrollDelta((float)e.mouseWheel.delta);
        }

        else if (e.type == sf::Event::GainedFocus)
          hasFocus = true;

        else if (e.type == sf::Event::LostFocus)
          hasFocus = false;

        else if (e.type == sf::Event::TextEntered) {
          if (e.text.unicode >= 32 && e.text.unicode <= 126)
            Keyboard_AddText((char)e.text.unicode);
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
