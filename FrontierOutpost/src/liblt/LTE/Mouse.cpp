#include "Mouse.h"
#include "Timer.h"
#include "Window.h"

#include "SFML/Window.hpp"

const float kDoubleClickThresh = 0.1f;

namespace {
  struct Mouse {
    int x;
    int y;
    int lastX;
    int lastY;
    float lastClickInterval;
    float scrollDelta;

    Timer downTimer[MouseButton_SIZE];
    bool down[MouseButton_SIZE];
    bool lastDown[MouseButton_SIZE];

    Timer releaseTimer[MouseButton_SIZE];

    Mouse() :
      x(0),
      y(0),
      lastX(0),
      lastY(0),
      scrollDelta(0)
    {
      for (MouseButton i = 0; i < MouseButton_SIZE; ++i) {
        down[i] = false;
        lastDown[i] = false;
      }
    }
  } gMouse;
}

namespace LTE {
  DefineFunction(Mouse_DoubleClicked) {
    return Mouse_LeftPressed()
      && gMouse.releaseTimer[MouseButton_Left].GetElapsed() < kDoubleClickThresh;
  }

  bool Mouse_Down(MouseButton button) {
    return gMouse.down[button];
  }

  bool Mouse_Pressed(MouseButton button) {
    return gMouse.down[button] && !gMouse.lastDown[button];
  }

  bool Mouse_Released(MouseButton button) {
    return !gMouse.down[button] && gMouse.lastDown[button];
  }

  float Mouse_GetDownTime(MouseButton button) {
    return gMouse.downTimer[button].GetElapsed();
  }

  DefineFunction(Mouse_GetScrollDelta) {
    return gMouse.scrollDelta;
  }

  DefineFunction(Mouse_GetPos) {
    return V2((float)gMouse.x, (float)gMouse.y);
  }

  DefineFunction(Mouse_GetPosImmediate) {
    sf::Vector2i p = sf::Mouse::getPosition(
      *(sf::Window*)Window_Get()->GetImplData());
    return V2((float)p.x, (float)p.y);
  }

  DefineFunction(Mouse_GetPosLast) {
    return V2((float)gMouse.lastX, (float)gMouse.lastY);
  }


  void Mouse_SetPressed(MouseButton button, bool pressed) {
    gMouse.down[button] = pressed;
  }

  void Mouse_SetScrollDelta(float ds) {
    gMouse.scrollDelta = ds;
  }

  void Mouse_Update() {
    gMouse.lastX = gMouse.x;
    gMouse.lastY = gMouse.y;

    for (MouseButton button = 0; button < MouseButton_SIZE; ++button) {
      if (gMouse.down[button] && !gMouse.lastDown[button])
        gMouse.downTimer[button].Reset();
      if (!gMouse.down[button] && gMouse.lastDown[button])
        gMouse.releaseTimer[button].Reset();
      gMouse.lastDown[button] = gMouse.down[button];
    }

    gMouse.scrollDelta = 0;
  }

  void Mouse_UpdatePos(V2I const& p) {
    gMouse.x = p.x;
    gMouse.y = p.y;
  }

  DefineFunction(Mouse_LeftDown) {
    return Mouse_Down(MouseButton_Left);
  }

  DefineFunction(Mouse_LeftPressed) {
    return Mouse_Pressed(MouseButton_Left);
  }

  DefineFunction(Mouse_LeftReleased) {
    return Mouse_Released(MouseButton_Left);
  }

  DefineFunction(Mouse_RightDown) {
    return Mouse_Down(MouseButton_Right);
  }


  DefineFunction(Mouse_RightPressed) {
    return Mouse_Pressed(MouseButton_Right);
  }

  DefineFunction(Mouse_RightReleased) {
    return Mouse_Released(MouseButton_Right);
  }
}
