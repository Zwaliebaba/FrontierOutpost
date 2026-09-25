#include "Keyboard.h"

#include "String.h"
#include "Thread.h"
#include "Vector.h"

#define KEY_X2                                                                 \
  X(A)                                                                         \
  X(B)                                                                         \
  X(C)                                                                         \
  X(D)                                                                         \
  X(E)                                                                         \
  X(F)                                                                         \
  X(G)                                                                         \
  X(H)                                                                         \
  X(I)                                                                         \
  X(J)                                                                         \
  X(K)                                                                         \
  X(L)                                                                         \
  X(M)                                                                         \
  X(N)                                                                         \
  X(O)                                                                         \
  X(P)                                                                         \
  X(Q)                                                                         \
  X(R)                                                                         \
  X(S)                                                                         \
  X(T)                                                                         \
  X(U)                                                                         \
  X(V)                                                                         \
  X(W)                                                                         \
  X(X)                                                                         \
  X(Y)                                                                         \
  X(Z)                                                                         \
  X(F1)                                                                        \
  X(F2)                                                                        \
  X(F3)                                                                        \
  X(F4)                                                                        \
  X(F5)                                                                        \
  X(F6)                                                                        \
  X(F7)                                                                        \
  X(F8)                                                                        \
  X(F9)                                                                        \
  X(F10)                                                                       \
  X(F11)                                                                       \
  X(F12)                                                                       \
  X(F13)                                                                       \
  X(F14)                                                                       \
  X(F15)

namespace {
  struct Keyboard {
    bool down[Key_SIZE];
    bool downLast[Key_SIZE];
    bool released[Key_SIZE];
    Vector<uchar> chars;
    Vector<Key> pressed;

    Keyboard() {
      for (Key key = 0; key < Key_SIZE; ++key) {
        down[key] = downLast[key] = released[key] = false;
      }
    }

  } gKeyboard;
}

namespace LTE {
  void Keyboard_AddDown(Key key) {
    gKeyboard.down[key] = true;
    gKeyboard.released[key] = false;
    gKeyboard.pressed.push(key);
  }

  void Keyboard_AddUp(Key key) {
    gKeyboard.released[key] = true;
  }

  void Keyboard_AddText(uchar c) {
    gKeyboard.chars.push(c);
  }

  bool Keyboard_Down(Key key) {
    return gKeyboard.down[key];
  }

  Vector<Key> const& Keyboard_GetKeysPressed() {
    return gKeyboard.pressed;
  }

  void Keyboard_ModifyString(String& str, int& cursor) {
    for (size_t i = 0; i < gKeyboard.chars.size(); ++i)
      str.insert(str.begin() + (cursor++), gKeyboard.chars[i]);

    if (Keyboard_Pressed(Key_BackSpace)) {
      if (Keyboard_Control()) 
        while (cursor && str[(size_t)(cursor - 1)] != ' ')
          str.erase((cursor--) - 1, 1);

      if (str.size())
        str.erase((cursor--) - 1, 1);
    }
  }

  bool Keyboard_Pressed(Key key) {
    return gKeyboard.down[key] && !gKeyboard.downLast[key];
  }

  bool Keyboard_Released(Key key) {
    return !gKeyboard.down[key] && gKeyboard.downLast[key];
  }

  void Keyboard_Update(bool hasFocus) {
    for (Key key = 0; key < Key_SIZE; ++key) {
      gKeyboard.downLast[key] = gKeyboard.down[key];

      /* A key that was down stays down until its release arrives or the window loses focus. */
      if (gKeyboard.downLast[key])
        gKeyboard.down[key] = hasFocus && !gKeyboard.released[key];
      gKeyboard.released[key] = false;
    }

    gKeyboard.chars.clear();
    gKeyboard.pressed.clear();
  }

  const char* Key_Name(Key key) {
    switch (key) {
      #define X(x) case Key_##x: return #x;
      KEY_X2
      #undef X
      case Key_N1:         return "1";
      case Key_N2:         return "2";
      case Key_N3:         return "3";
      case Key_N4:         return "4";
      case Key_N5:         return "5";
      case Key_N6:         return "6";
      case Key_N7:         return "7";
      case Key_N8:         return "8";
      case Key_N9:         return "9";
      case Key_N0:         return "0";
      case Key_NP1:        return "Numpad 1";
      case Key_NP2:        return "Numpad 2";
      case Key_NP3:        return "Numpad 3";
      case Key_NP4:        return "Numpad 4";
      case Key_NP5:        return "Numpad 5";
      case Key_NP6:        return "Numpad 6";
      case Key_NP7:        return "Numpad 7";
      case Key_NP8:        return "Numpad 8";
      case Key_NP9:        return "Numpad 9";
      case Key_NP0:        return "Numpad 0";
      case Key_Add:        return "Add";
      case Key_BackSpace:  return "Backspace";
      case Key_BackSlash:  return "Backslash";
      case Key_Comma:      return "Comma";
      case Key_Dash:       return "Dash";
      case Key_Delete:     return "Delete";
      case Key_Divide:     return "Divide";
      case Key_Down:       return "Down";
      case Key_End:        return "End";
      case Key_Equal:      return "Equal";
      case Key_Escape:     return "Escape";
      case Key_Home:       return "Home";
      case Key_Insert:     return "Insert";
      case Key_LBracket:   return "Left Bracket";
      case Key_Left:       return "Left";
      case Key_Menu:       return "Menu";
      case Key_Multiply:   return "Multiply";
      case Key_PageDown:   return "PageDown";
      case Key_PageUp:     return "PageUp";
      case Key_Pause:      return "Pause";
      case Key_Period:     return "Period";
      case Key_Quote:      return "Quote";
      case Key_RBracket:   return "Right Bracket";
      case Key_Return:     return "Return";
      case Key_Right:      return "Right";
      case Key_SemiColon:  return "Semicolon";
      case Key_Slash:      return "Slash";
      case Key_Space:      return "Space";
      case Key_Subtract:   return "Subtract";
      case Key_Tab:        return "Tab";
      case Key_Tilde:      return "Tilde";
      case Key_Up:         return "Up";

      case Key_LAlt:       return "Left Alt";
      case Key_LControl:   return "Left Control";
      case Key_LShift:     return "Left Shift";
      case Key_LSystem:    return "Left System";
      case Key_RAlt:       return "Right Alt";
      case Key_RControl:   return "Right Control";
      case Key_RShift:     return "Right Shift";
      case Key_RSystem:    return "Right System";

      default:              return "Unknown";
    }
  }
}
