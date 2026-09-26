# ADR-012: The window and input are plain Win32

- **Status:** Accepted (owner, 2026-09-25, at the start of the NeuronClient migration's Phase 2)
- **Scope:** NeuronClient's `Window` and `Key`, and `lt.dll`'s window, keyboard and mouse
  (`LTE/Window.cpp`, `Keyboard.cpp` and `Mouse.cpp` in `FrontierOutpost/src/liblt/`)
- **Detail:** `Design/Archive/NeuronClient-migration.md` §4.1, §4.3, §5.2, §5.3, §5.4, §8, §9 B,
  §11, Phase 2 step 6 and N7

## Context

SFML gives liblt its window and events, keyboard and mouse polling, and joysticks (plan §4.1).
What the game gets from it today (plan §4.3):

- a 1920×1080 client area, windowed, with vsync off and no frame cap
  (`FrontierOutpost/src/launch/launch.cpp:40-41`);
- the OS cursor hidden, since scripts draw their own;
- system DPI awareness, which is SFML's choice;
- nothing on `WM_CLOSE`: SFML swallows it, so the close button and Alt+F4 do nothing.

The engine's `Key` enumeration (101 keys) is its own. SFML's codes are converted on arrival, and
never stored, persisted or shown to scripts, which name keys (`Key_T`). So a Win32 mapping has no
compatibility to keep.

No kept code reads a joystick. `Joystick.*`, the joystick buttons and axes, and the per-frame
polling in `LTE/Program.cpp` are unreachable (plan §9 B).

## Decision

1. **`Neuron::Window` is a Win32 window** (user32, plan §5.2): an HWND with a given client size and
   title, and its message pump. It handles focus, close and resize, cursor visibility, and mouse
   capture while a button is held. It queues input events: key down and up with repeat,
   characters, mouse buttons, moves and wheel.
2. **`Neuron::Key` is NeuronClient's own enumeration,** mapped from virtual keys and scan codes,
   left and right Shift, Ctrl and Alt included. liblt maps it to the engine's `Key` through a
   table (plan §5.4).
3. **`WM_CLOSE` quits** (plan §8). The close button and Alt+F4 end the program.
4. **System DPI awareness stays,** as SFML set it (plan §8).
5. **There is no joystick.** NeuronClient has none, and liblt's joystick code goes as unused
   (plan §5.2, §9 B; ADR-013).
6. **The window keeps what the game asks of it today:** a 1920×1080 client area, windowed, with
   the OS cursor hidden and vsync off (plan §4.3; N7, ADR-007).
7. **The window moves before the renderer does** (plan Phase 2 step 6). While OpenGL still
   renders, a temporary WGL bridge in liblt, about 150 lines, keeps it drawing on the new window
   through an opaque handle, so window and input faults stay apart from renderer faults. Then SFML
   leaves the tree. Phase 4 deletes the bridge with OpenGL. Its step 6 did (`57c67bb`), with the
   window class's `CS_OWNDC`, which only OpenGL wanted.

## What this forecloses

- **Joystick input,** until a later decision brings it back.
- **Per-monitor DPI and borderless fullscreen** in this migration: both are backlog (plan §11).
  Exclusive fullscreen is not supported, and the dead window setters for icon, position,
  fullscreen, cursor and capture go (plan §5.3, §9 B).
- **Any constraint from SFML's key codes.** Nothing stored them, so the Win32 table owes them
  nothing.
- **A close button that does nothing.**
