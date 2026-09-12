// Lockstep.cpp -- process entry point, and the composition root of the main page.
//
// What this executable shows is the ops console in Design/UI: digest, map, orders. The sky it
// draws is `NeuronClient/Starfield`, the sphere of directions of ADR-032, and nothing of the
// MVP-01 ship scene ADR-015 deleted remains in the tree.
//
// This is the wizard's wWinMain reduced to what the game actually needs: one fixed-size,
// non-resizable window, no menu and no About dialog. The window is the presentation target
// described in Design/README.md -- a 1280x720 R8G8B8A8 framebuffer presented 1:1, so the client
// area is exactly the resolution the game renders and a rendered pixel is a physical one
// (ADR-011).
//
// There is no WM_PAINT handler and there must not be one: from the moment the swap chain exists
// it owns every pixel of the client area, and a BeginPaint/EndPaint pair racing it produces a
// flash and nothing else (AGENTS.md 4, NOGDI).

#include "pch.h"
#include "Lockstep.h"

#include "Color.h"
#include "Device.h"
#include "FontRenderer.h"
#include "PointerInput.h"
#include "KeyboardInput.h"
#include "SceneTarget.h"
#include "ShapeRenderer.h"

#include "HostedServer.h"
#include "MatchLog.h"
#include "MatchStore.h"
#include "MainPage.h"
#include "ConnectionDialog.h"
#include "JoinPage.h"
#include "SeatsPage.h"
#include "MatchConnection.h"
#include "SnapshotView.h"

#include "Socket.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace
{

// The screen the game presents. Not restated here: Neuron::SceneTarget owns the numbers, the
// window is created at exactly that size, and the swap chain is told the same thing -- which is
// what makes "the client area is the framebuffer" a fact rather than three constants that agree
// today (ADR-011).
constexpr int CLIENT_WIDTH = static_cast<int>(Neuron::SceneTarget::WIDTH_PIXELS);
constexpr int CLIENT_HEIGHT = static_cast<int>(Neuron::SceneTarget::HEIGHT_PIXELS);

/// How long the loop sleeps when the screen has not changed. Short enough that a tap is answered
/// within a frame of a sixty-hertz one, long enough that an idle client is not a busy one.
constexpr std::int32_t IDLE_FRAME_MILLISECONDS = 8;

constexpr wchar_t WINDOW_CLASS_NAME[] = L"LockstepWindow";
constexpr wchar_t WINDOW_TITLE[] = L"LockStep: Universe";

/// What this process is doing.
///
/// One executable, three roles (ADR-028). The client talks TCP in every one of them, including the
/// one where the server is on the next thread -- so there is no local path that works and a network
/// path nobody runs. Every launch exercises the transport.
enum class Role : std::uint8_t
{
  /// Run a server and play on it. The default, and what a host does.
  HostAndPlay,
  /// Play on somebody else's.
  Join,
  /// Run a server and draw nothing. Also the headless runner.
  Serve
};

struct Startup
{
  Role role = Role::HostAndPlay;
  std::string host = "127.0.0.1";
  std::uint16_t port = 7341;
  std::string token = "alpha";
  /// Whether `--token` was given. A resumed match offers the host its first stored seat unless the
  /// command line chose one.
  bool tokenGiven = false;
  /// `--phase0` runs the test plan's Phase 0 setup: six players, an hourly tick, forty-eight hours.
  /// Without it the defaults are the production three-week match, which nobody is going to sit
  /// through to find a broken mechanic.
  bool phaseZero = false;

  /// Whether `--join` was given. It decides whether screen 03 appears: a command line that already
  /// names a server and a token has answered the question the screen asks, and asking it again
  /// would make the flag useless.
  bool joinGiven = false;

  /// `--tick <seconds>` overrides the interval, for rehearsing a match rather than playing one.
  ///
  /// **This is how you find out the loop works before asking six people for a weekend.** At three
  /// seconds a tick a whole Phase 0 match runs in under three minutes, with the same server, the
  /// same sockets, the same store and the same log as the real thing -- everything except the
  /// waiting. The test plan is explicit that a compressed clock hides anything about session shape
  /// or retention, so this is for mechanics only, which is exactly what Phase 0 is for.
  std::uint32_t tickSeconds = 0;

  /// `--bots <n>` puts bots in the LAST n seats of a `--serve` match.
  ///
  /// **Without it the headless runner runs a match nobody plays.** `--serve` listens and ticks on
  /// schedule whether or not anybody connects, so a headless run with no clients is six absent
  /// players going into custody -- a match that resolves and proves nothing. `--serve
  /// --phase0 --tick 3 --bots 6` is a whole match, played, in under three minutes, and the
  /// instrumentation log (ADR-030) is the output.
  ///
  /// The last seats rather than the first, so `--bots 5` leaves seat one for whoever joins with
  /// `alpha`.
  std::uint32_t bots = 0;

  /// `--store <name>` names the files this match is written under, without an extension.
  ///
  /// **Empty means "derive it from the port", and that is the fix rather than the flag.** Both
  /// files used to be called `lockstep-match`, so two servers beside one executable overwrote each
  /// other's store and each other's log -- and since a store is now a match that can be resumed
  /// (ADR-042), the second server did not merely lose a log, it ate a match. A port is already
  /// unique per server on one machine, so the default collides only when the servers could not
  /// both have started.
  ///
  /// The log takes the same name. A store and the log of the match it holds must not come apart:
  /// the log is how anybody works out what the store contains.
  std::string storeName;
};

/// The six Phase 0 tokens.
///
/// **Typed once into a client by six people who know each other** (ADR-029). They are not
/// authentication and this tree does not pretend otherwise: they are in the binary, they go over
/// the wire in the clear, and they exist so that two players cannot accidentally be the same
/// player. Phase 1 needs better; Phase 0 needs a login log.
[[nodiscard]] std::vector<std::string> PhaseZeroTokens()
{
  return {"alpha", "bravo", "charlie", "delta", "echo", "foxtrot"};
}

/// A file name, resolved to sit beside the executable.
///
/// **Not relative to the working directory**, which is wherever the shell happened to be. A match
/// store and a log that land somewhere different depending on how the game was launched are a match
/// that does not resume and a Phase 0 whose instrumentation nobody can find. Beside the executable
/// is where somebody looks.
[[nodiscard]] std::string BesideTheExecutable(const std::string& _name)
{
  wchar_t module[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(nullptr, module, MAX_PATH);
  if (length == 0 || length >= MAX_PATH)
  {
    return _name;
  }

  const std::wstring_view path{module, length};
  const std::size_t slash = path.find_last_of(L'\\');
  if (slash == std::wstring_view::npos)
  {
    return _name;
  }

  // UTF-8, like every path in this tree, and opened wide again at the file (MatchStore, MatchLog).
  // A folder with a diacritic in its name is an ordinary place for an executable to sit.
  return Neuron::WideToUtf8(path.substr(0, slash + 1)) + _name;
}

/// Splits `host`, `host:port`, `[v6]` or `[v6]:port` into the two.
///
/// **Brackets, because an IPv6 literal is made of colons** (ADR-046). Splitting on a colon turned
/// `::1:7371` into an empty host and a port that did not parse, and the client then connected to
/// nothing on the default port -- silently, because an address that does not resolve is the same
/// failure as a server that is not running. The bracket form is what a URL uses and what a person
/// who has typed an IPv6 address before will reach for.
///
/// An unbracketed address with more than one colon is taken whole, as a host. That is the other
/// thing somebody will type, and guessing a port out of the last group of an IPv6 address would be
/// worse than ignoring it.
void SplitHostAndPort(const std::string& _target, std::string& _outHost, std::uint16_t& _outPort)
{
  const auto asPort = [](const std::string& _digits, std::uint16_t _fallback)
  {
    const unsigned long parsed = std::strtoul(_digits.c_str(), nullptr, 10);
    return parsed > 0 && parsed <= 65535 ? static_cast<std::uint16_t>(parsed) : _fallback;
  };

  if (!_target.empty() && _target.front() == '[')
  {
    const std::size_t close = _target.find(']');
    if (close != std::string::npos)
    {
      _outHost = _target.substr(1, close - 1);
      if (close + 1 < _target.size() && _target[close + 1] == ':')
      {
        _outPort = asPort(_target.substr(close + 2), _outPort);
      }
      return;
    }
  }

  const std::size_t colon = _target.rfind(':');
  if (colon != std::string::npos && _target.find(':') == colon)
  {
    _outPort = asPort(_target.substr(colon + 1), _outPort);
    _outHost = _target.substr(0, colon);
    return;
  }

  _outHost = _target;
}

/// `--serve [port]`, `--join <host[:port]>`, `--token <token>`, `--phase0`, `--tick <seconds>`,
/// `--bots <n>`, `--store <name>`. Anything else is host-and-play.
[[nodiscard]] Startup ParseCommandLine(LPWSTR _commandLine)
{
  Startup startup;

  std::vector<std::string> words;
  {
    const std::wstring wide = _commandLine == nullptr ? std::wstring{} : std::wstring{_commandLine};
    std::string narrow;
    narrow.reserve(wide.size());
    for (const wchar_t letter : wide)
    {
      // The command line is a host name, a port and a token, all of which are ASCII by
      // construction. Anything else is not something this accepts rather than something it
      // mangles.
      narrow.push_back(letter < 128 ? static_cast<char>(letter) : '?');
    }

    std::string word;
    for (const char letter : narrow)
    {
      if (letter == ' ' || letter == '\t')
      {
        if (!word.empty())
        {
          words.push_back(word);
          word.clear();
        }
        continue;
      }
      word.push_back(letter);
    }
    if (!word.empty())
    {
      words.push_back(word);
    }
  }

  const auto asPort = [](const std::string& _text, std::uint16_t _fallback)
  {
    const unsigned long value = std::strtoul(_text.c_str(), nullptr, 10);
    return value > 0 && value < 65536 ? static_cast<std::uint16_t>(value) : _fallback;
  };

  for (std::size_t index = 0; index < words.size(); ++index)
  {
    if (words[index] == "--serve")
    {
      startup.role = Role::Serve;
      if (index + 1 < words.size() && words[index + 1].rfind("--", 0) != 0)
      {
        startup.port = asPort(words[++index], startup.port);
      }
    }
    else if (words[index] == "--join" && index + 1 < words.size())
    {
      startup.role = Role::Join;
      startup.joinGiven = true;
      SplitHostAndPort(words[++index], startup.host, startup.port);
    }
    else if (words[index] == "--token" && index + 1 < words.size())
    {
      startup.token = words[++index];
      startup.tokenGiven = true;
    }
    else if (words[index] == "--phase0")
    {
      startup.phaseZero = true;
    }
    else if (words[index] == "--tick" && index + 1 < words.size())
    {
      const unsigned long seconds = std::strtoul(words[++index].c_str(), nullptr, 10);
      startup.tickSeconds = seconds > 0 ? static_cast<std::uint32_t>(seconds) : 0;
    }
    else if (words[index] == "--bots" && index + 1 < words.size())
    {
      startup.bots = static_cast<std::uint32_t>(std::strtoul(words[++index].c_str(), nullptr, 10));
    }
    else if (words[index] == "--store" && index + 1 < words.size())
    {
      startup.storeName = words[++index];
    }
  }

  return startup;
}

HINSTANCE g_instance = nullptr;
bool g_quitRequested = false;

/// The window procedure runs on the client thread and needs to reach the input state, which lives
/// in RunGame. A file-scope pointer is the plain Win32 answer and is what the rest of this file
/// already does with g_instance; it is set once, before the first message is dispatched.
Neuron::PointerInput* g_pointerInput = nullptr;

/// The keyboard, which only the join screen uses (ADR-034). Null everywhere else, and the window
/// procedure checks: a game that types nothing should not be holding a keyboard.
Neuron::KeyboardInput* g_keyboardInput = nullptr;

LRESULT CALLBACK WndProc(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam)
{
  if (g_keyboardInput != nullptr &&
      g_keyboardInput->HandleMessage(_message, static_cast<std::uint64_t>(_wParam), static_cast<std::int64_t>(_lParam)))
  {
    return 0;
  }

  if (g_pointerInput != nullptr && g_pointerInput->HandleMessage(_message, _wParam, _lParam))
  {
    return 0;
  }

  switch (_message)
  {
  case WM_DESTROY:
    g_quitRequested = true;
    PostQuitMessage(0);
    return 0;

  default:
    return DefWindowProcW(_window, _message, _wParam, _lParam);
  }
}

bool RegisterWindowClass(HINSTANCE _instance)
{
  WNDCLASSEXW windowClass = {};
  windowClass.cbSize = sizeof(WNDCLASSEXW);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = WndProc;
  windowClass.hInstance = _instance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  // No background brush. NeuronCore.h defines NOGDI, so GetStockObject is not even declared here
  // -- which is the right answer rather than an obstacle: the swap chain owns every pixel of the
  // client area, and a GDI brush painting under it only produces a flash on resize.
  windowClass.hbrBackground = nullptr;
  windowClass.lpszClassName = WINDOW_CLASS_NAME;

  return RegisterClassExW(&windowClass) != 0;
}

// Sizes for the CLIENT area, not the window: AdjustWindowRect adds the border and caption, so the
// framebuffer is presented 1:1 rather than a few rows short of it.
//
// AdjustWindowRect assumes 96 DPI, and under per-monitor awareness the caption on a scaled
// display is not 96 DPI, so its answer is close rather than right. Rather than reach for
// AdjustWindowRectExForDpi and a DPI to pass it, the window is created and then measured: if the
// client area came out anything other than exact, the difference is added back. That is correct
// on every DPI, theme and Windows version without knowing anything about any of them -- and this
// has to be exact, because the client area IS the framebuffer: a row short is a row of the
// picture the player never sees.
HWND CreateMainWindow(HINSTANCE _instance, int _showCommand)
{
  constexpr DWORD STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

  RECT bounds = {0, 0, CLIENT_WIDTH, CLIENT_HEIGHT};
  AdjustWindowRect(&bounds, STYLE, FALSE);

  HWND window = CreateWindowExW(0, WINDOW_CLASS_NAME, WINDOW_TITLE, STYLE, CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
                                bounds.bottom - bounds.top, nullptr, nullptr, _instance, nullptr);
  if (window == nullptr)
  {
    return nullptr;
  }

  RECT clientArea = {};
  RECT outerArea = {};
  if (GetClientRect(window, &clientArea) != 0 && GetWindowRect(window, &outerArea) != 0)
  {
    const int widthShortfall = CLIENT_WIDTH - (clientArea.right - clientArea.left);
    const int heightShortfall = CLIENT_HEIGHT - (clientArea.bottom - clientArea.top);
    if (widthShortfall != 0 || heightShortfall != 0)
    {
      SetWindowPos(window, nullptr, 0, 0, (outerArea.right - outerArea.left) + widthShortfall,
                   (outerArea.bottom - outerArea.top) + heightShortfall, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }

  ShowWindow(window, _showCommand);
  UpdateWindow(window);
  return window;
}

/// PeekMessage, not GetMessage: the loop now has a frame to render whether or not the window has
/// anything to say. Returns false when the queue produced WM_QUIT.
bool PumpMessages()
{
  MSG message = {};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
  {
    if (message.message == WM_QUIT)
    {
      return false;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return !g_quitRequested;
}

/// The match store beside the executable, if there is a match in it to resume (ADR-042).
///
/// Three answers. No file means a new match. A file that is not a store, or is cut short, is a
/// fatal: ADR-024 is explicit that a corrupt store must fail loudly rather than start an empty match
/// on top of a real one, and the file is left where it is for somebody to look at. A store whose
/// match has finished is the record of that match, not something to resume into: it is moved
/// aside under a `.finished` name and a new match starts.
/// Gives `--serve` somewhere to speak, and says whether it got one.
///
/// **The dedicated server was mute.** This is a Windows-subsystem binary, so it has no console
/// unless it asks for one, and every line it produced went to `DebugTrace` -- which is
/// `OutputDebugStringA` in Debug and `__noop` in Release. A person running the server the way a
/// server is run therefore saw nothing at all: not the port, not a login, not the reason it
/// stopped. The match log had it, which is no help to somebody watching a window to see whether
/// their friends have arrived.
///
/// Attached to the launching shell's console when there is one, because that is where the person
/// who typed the command is looking. A fresh console when there is not -- a double-click, or a
/// service -- because a window that appears is better than silence, even though it closes with the
/// process.
/// **A redirected handle is honored before a console is asked for**, because `--serve > today.txt`
/// is how anybody would keep a day of it, and a process that attached a console instead would write
/// to a window and leave the file empty. A Windows-subsystem process inherits its parent's standard
/// handles exactly as a console one does; what it does not get is a console of its own.
[[nodiscard]] bool OpenConsole()
{
  const HANDLE inherited = GetStdHandle(STD_OUTPUT_HANDLE);
  if (inherited != nullptr && inherited != INVALID_HANDLE_VALUE)
  {
    return true;
  }

  if (AttachConsole(ATTACH_PARENT_PROCESS) == FALSE && AllocConsole() == FALSE)
  {
    return false;
  }

  const HANDLE opened = GetStdHandle(STD_OUTPUT_HANDLE);
  return opened != nullptr && opened != INVALID_HANDLE_VALUE;
}

/// One line to whoever is watching. The match log keeps the permanent copy (ADR-030); this is the
/// live one, and it is flushed because a server that crashes must not take its last words with it.
void Say(bool _console, std::string_view _line)
{
  // Written to the handle rather than through the CRT's `stdout`. A GUI-subsystem process starts
  // with the CRT's streams pointing at nothing, and reopening them onto `CONOUT$` would write to a
  // console even when the caller asked for a file.
  const HANDLE out = _console ? GetStdHandle(STD_OUTPUT_HANDLE) : INVALID_HANDLE_VALUE;
  if (out != nullptr && out != INVALID_HANDLE_VALUE)
  {
    const std::string text = std::string{_line} + "\r\n";
    DWORD written = 0;
    (void)WriteFile(out, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
  }
  Neuron::DebugTrace("{}\n", _line);
}

/// Where this match's store and log live. One function, because they have to agree: a store found
/// under one name and a log written under another is a match nobody can read the history of.
struct MatchPaths
{
  std::string store;
  std::string log;
};

[[nodiscard]] MatchPaths PathsFor(const Startup& _startup)
{
  // The stem the owner asked for, or the port. Not a constant: see `Startup::storeName`.
  const std::string stem = _startup.storeName.empty() ? std::format("lockstep-{}", _startup.port) : _startup.storeName;
  return MatchPaths{BesideTheExecutable(stem + ".store"), BesideTheExecutable(stem + ".log")};
}

[[nodiscard]] std::optional<Neuron::MatchStore::Contents> LoadStoredMatch(const std::string& _storePath)
{
  Neuron::MatchStore::Contents contents;
  const Neuron::MatchStore::Problem problem = Neuron::MatchStore::Load(_storePath, contents);

  if (problem == Neuron::MatchStore::Problem::NotFound)
  {
    return std::nullopt;
  }
  if (problem != Neuron::MatchStore::Problem::None)
  {
    Neuron::Fatal("The match store at {} could not be read: {}. It has been left as it is; move it aside to start a new match.", _storePath,
                  Neuron::MatchStore::Describe(problem));
  }

  if (contents.finished)
  {
    const std::wstring from = Neuron::Utf8ToWide(_storePath);
    const std::wstring to = from + L".finished";
    (void)_wremove(to.c_str());
    if (_wrename(from.c_str(), to.c_str()) != 0)
    {
      Neuron::Fatal("The match store at {} holds a finished match and could not be moved aside.", _storePath);
    }
    Neuron::DebugTrace("The stored match had finished; it is kept as {}.finished and a new match starts.\n", _storePath);
    return std::nullopt;
  }

  return contents;
}

/// Builds the match server. One function because two callers need it and they must not drift: the
/// headless `--serve` path starts one before any window exists, and the host's path starts one
/// after the seats screen has decided how many seats there are (ADR-036).
[[nodiscard]] std::unique_ptr<Lockstep::HostedServer> StartHostedServer(const Startup& _startup, std::vector<std::string> _tokens)
{
  constexpr std::uint64_t GALAXY_SEED = 0x4652'4F4E'5449'4552ULL;

  // A stored match resumes, whatever the command line said about rules: the rules are in the store
  // and the match was played under them (ADR-042).
  const MatchPaths paths = PathsFor(_startup);
  const std::string& storePath = paths.store;
  const std::string& logPath = paths.log;
  if (std::optional<Neuron::MatchStore::Contents> stored = LoadStoredMatch(storePath))
  {
    return std::make_unique<Lockstep::HostedServer>(_startup.port, std::move(*stored), storePath, logPath);
  }

  Lockstep::MatchRules rules = _startup.phaseZero ? Lockstep::PhaseZeroRules() : Lockstep::MatchRules{};
  if (_startup.tickSeconds > 0)
  {
    rules.tickIntervalSeconds = _startup.tickSeconds;
  }

  // The seats screen is what makes this true of a hosted match: the galaxy is generated for the
  // number of seats somebody chose, not for a constant.
  if (!_tokens.empty())
  {
    rules.playerCount = static_cast<std::uint32_t>(_tokens.size());
  }

  // The LAST n seats (ADR-037). One style, because a command line asking for six bots is asking for
  // a match to happen at all; the seats screen is where a style is chosen deliberately.
  std::vector<std::optional<Lockstep::BotPolicy>> bots(rules.playerCount);
  const std::uint32_t botCount = std::min(_startup.bots, rules.playerCount);
  for (std::uint32_t seat = rules.playerCount - botCount; seat < rules.playerCount; ++seat)
  {
    bots[seat] = Lockstep::BotPolicy::ExpandNear;
  }

  return std::make_unique<Lockstep::HostedServer>(_startup.port, std::move(_tokens), storePath, logPath, GALAXY_SEED, rules,
                                                  std::move(bots));
}

/// The seats screen, in its own frame loop, until the host enters the match or closes the window.
///
/// **The lobby is already listening; the match is not** (ADR-036 as amended). That split is what
/// lets this screen be live: the server has the tokens and reports who has presented one, while the
/// galaxy still does not exist because the number of seats it needs is what the host is settling
/// here. `ENTER MATCH` is what creates it.
///
/// Fills `_outTokens` and `_outBots` and returns the host's seat, or -1 when the window closed
/// first.
[[nodiscard]] std::int32_t RunSeatsScreen(Neuron::Device& _device, Neuron::SceneTarget& _screen, Neuron::ShapeRenderer& _shapes,
                                          Neuron::FontRenderer& _text, Neuron::PointerInput& _pointer, Neuron::KeyboardInput& _keyboard,
                                          HWND _window, Lockstep::HostedServer& _lobby, const std::vector<std::string>& _tokens,
                                          std::vector<std::string>& _outTokens, std::vector<std::optional<Lockstep::BotPolicy>>& _outBots,
                                          Lockstep::SeatsPage::Entry& _outEntry)
{
  Lockstep::SeatsPage page{_tokens};

  while (PumpMessages())
  {
    // Who has arrived, straight from the server this process is running. Not a protocol message:
    // the host owns the object, and this is the same lock `TakeLog` already uses (ADR-028).
    page.SetConnected(_lobby.SeatsConnected());

    for (const Neuron::KeyboardInput::Key key : _keyboard.TakeKeys())
    {
      page.HandleKey(key);
    }
    (void)_keyboard.TakeTyped();

    float tapXPixels = 0.0F;
    float tapYPixels = 0.0F;
    if (_pointer.TakeClick(tapXPixels, tapYPixels))
    {
      (void)page.HandleTap(tapXPixels, tapYPixels);
    }

    // COPY puts a token on the clipboard. It is the only thing this screen does to the machine
    // outside its own window, and it is what the host needs: twelve tokens have to reach twelve
    // people somehow, and retyping them off a screen is how a seat gets typed wrong.
    const std::string copy = page.TakeCopyRequest();
    if (!copy.empty() && OpenClipboard(_window) != 0)
    {
      EmptyClipboard();
      const HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, copy.size() + 1);
      if (handle != nullptr)
      {
        void* memory = GlobalLock(handle);
        if (memory != nullptr)
        {
          std::memcpy(memory, copy.c_str(), copy.size() + 1);
          GlobalUnlock(handle);
          SetClipboardData(CF_TEXT, handle);
        }
      }
      CloseClipboard();
    }

    if (const std::optional<Lockstep::SeatsPage::Entry> entry = page.TakeEnterRequest())
    {
      _outEntry = *entry;
      _outTokens = page.PlayingTokens();

      // After the request, never before: entering is what turns a seat nobody came to into a bot.
      _outBots = page.Roster();
      return page.HostSeat();
    }

    ID3D12GraphicsCommandList* commandList = _device.BeginFrame();
    _screen.BeginScene(commandList, _device.BackBufferView());

    _shapes.BeginFrame(_device.FrameIndex());
    _text.BeginFrame(_device.FrameIndex());

    page.DrawWorld(_shapes, _text);
    _shapes.Flush(commandList);
    _text.Flush(commandList);

    page.DrawInterface(_shapes, _text);
    _shapes.Flush(commandList);
    _text.Flush(commandList);

    _device.EndFrameAndPresent();
    _device.DrainDebugMessages();
  }

  return -1;
}

/// Screen 03, in its own frame loop, until the server welcomes this client or the window closes.
///
/// **A loop of its own rather than a mode inside the match loop.** The two screens share no state:
/// this one has no match, no orders and no camera the player drives, and folding it into `RunGame`
/// would put a `if (joined)` around every line of a function that is already the longest in the
/// tree. It returns true when there is a match to show.
[[nodiscard]] bool RunJoinScreen(Neuron::Device& _device, Neuron::SceneTarget& _screen, Neuron::ShapeRenderer& _shapes,
                                 Neuron::FontRenderer& _text, Neuron::PointerInput& _pointer, Neuron::KeyboardInput& _keyboard,
                                 Lockstep::MatchConnection& _connection, const std::string& _server, const std::string& _token,
                                 std::uint16_t _defaultPort, std::chrono::steady_clock::time_point _startedAt)
{
  Lockstep::JoinPage page;
  page.Offer(_server, _token);

  // Screen 05 over screen 03. The card underneath keeps saying what the player typed, because the
  // dialog is about to send them back to it.
  Lockstep::ConnectionDialog dialog;

  auto lastFrame = std::chrono::steady_clock::now();

  while (PumpMessages())
  {
    const auto now = std::chrono::steady_clock::now();
    const double elapsedSeconds = std::chrono::duration<double>(now - lastFrame).count();
    lastFrame = now;
    page.Update(elapsedSeconds);

    // ---- What the player did -------------------------------------------------------------------
    page.HandleTyped(_keyboard.TakeTyped());
    for (const Neuron::KeyboardInput::Key key : _keyboard.TakeKeys())
    {
      page.HandleKey(key);
    }

    float tapXPixels = 0.0F;
    float tapYPixels = 0.0F;
    if (_pointer.TakeClick(tapXPixels, tapYPixels))
    {
      // The dialog first, and it swallows whatever it does not use. A tap that fell through to the
      // fields behind a refusal would edit a token the player cannot see.
      if (!dialog.HandleTap(tapXPixels, tapYPixels))
      {
        (void)page.HandleTap(tapXPixels, tapYPixels);
      }
    }

    // ---- What the dialog was told to do --------------------------------------------------------
    switch (dialog.TakeAction())
    {
    case Lockstep::ConnectionDialog::Action::Quit:
      return false;

    case Lockstep::ConnectionDialog::Action::EditToken:
      page.FocusToken();
      [[fallthrough]];

    case Lockstep::ConnectionDialog::Action::Cancel:
    case Lockstep::ConnectionDialog::Action::Back:
      // A refusal is final for the connection that earned it, so going back means putting the
      // connection back to where it was before the attempt rather than merely hiding the dialog.
      _connection.Reset();
      page.SetStatus(Lockstep::JoinPage::Status::Ready);
      break;

    case Lockstep::ConnectionDialog::Action::Retry:
      _connection.Reset();
      page.SetStatus(Lockstep::JoinPage::Status::Ready);
      page.AskToJoin();
      break;

    case Lockstep::ConnectionDialog::Action::ViewLastDigest:
    case Lockstep::ConnectionDialog::Action::None:
    default:
      break;
    }

    // ---- What the player asked for -------------------------------------------------------------
    if (page.TakeJoinRequest())
    {
      // `host:port`, split here rather than in the field, because a field that validated as you
      // typed would refuse a half-typed address and there is nothing useful to say about one.
      std::string host;
      std::uint16_t port = _defaultPort;
      SplitHostAndPort(page.Server(), host, port);

      if (_connection.Open(host, port, page.Token()))
      {
        page.SetStatus(Lockstep::JoinPage::Status::Connecting);
      }
      else
      {
        page.SetStatus(Lockstep::JoinPage::Status::Refused, "No answer from that server.");
      }
    }

    // ---- What the server said ------------------------------------------------------------------
    _connection.Pump(std::chrono::duration<double>(now - _startedAt).count());

    const double secondsSinceStart = std::chrono::duration<double>(now - _startedAt).count();

    Lockstep::ConnectionDialog::Kind kind = Lockstep::ConnectionDialog::Kind::None;
    Lockstep::ConnectionDialog::Facts facts;
    facts.server = _connection.Server();
    facts.reason = _connection.Refusal();
    facts.reconnects = _connection.Reconnects();
    facts.secondsToNextAttempt = _connection.SecondsToNextAttempt(secondsSinceStart);

    // There IS a screen behind this dialog, and it is the one that asks the question the refusal is
    // an answer to. This is the only place that is true.
    facts.canGoBack = true;
    facts.greeted = _connection.State() == Lockstep::MatchConnection::Status::Greeting;

    switch (_connection.State())
    {
    case Lockstep::MatchConnection::Status::Playing:
      // Welcomed. The seat is shown for a moment before the match replaces this screen, because a
      // player who typed a token wants to see which empire it bought.
      page.SetSeat(_connection.Player(), 0, Lockstep::OwnerColor(_connection.Player(), _connection.Player()));
      page.SetStatus(Lockstep::JoinPage::Status::Joined);
      return true;

    case Lockstep::MatchConnection::Status::Refused:
      kind = Lockstep::ConnectionDialog::Kind::Refused;
      break;

    case Lockstep::MatchConnection::Status::Lost:
      kind = Lockstep::ConnectionDialog::Kind::Lost;
      break;

    case Lockstep::MatchConnection::Status::Connecting:
    case Lockstep::MatchConnection::Status::Greeting:
      // One dialog for both halves of getting in: reaching the peer, and waiting to be welcomed by
      // it. A player cannot tell them apart and does not need to -- and until the connect stopped
      // blocking (ADR-043) the first half was not a state anything could draw.
      kind = Lockstep::ConnectionDialog::Kind::Connecting;
      break;

    case Lockstep::MatchConnection::Status::Idle:
    default:
      break;
    }

    dialog.Update(kind, facts, elapsedSeconds);

    // ---- The frame -----------------------------------------------------------------------------
    ID3D12GraphicsCommandList* commandList = _device.BeginFrame();
    _screen.BeginScene(commandList, _device.BackBufferView());

    _shapes.BeginFrame(_device.FrameIndex());
    _text.BeginFrame(_device.FrameIndex());

    page.DrawWorld(_shapes, _text);
    _shapes.Flush(commandList);
    _text.Flush(commandList);

    page.DrawInterface(_shapes, _text);
    _shapes.Flush(commandList);
    _text.Flush(commandList);

    // A third layer, for the same reason there is a second: each renderer is one batch, so the
    // dialog's scrim would be drawn under the card it is meant to dim if it shared a flush.
    dialog.Draw(_shapes, _text);
    _shapes.Flush(commandList);
    _text.Flush(commandList);

    _device.EndFrameAndPresent();
    _device.DrainDebugMessages();
  }

  return false;
}

int RunGame(HWND _window, const Startup& _startup)
{
  Neuron::Device device;
  device.Create(_window, Neuron::SceneTarget::WIDTH_PIXELS, Neuron::SceneTarget::HEIGHT_PIXELS);

  // One shader-visible descriptor heap for the whole client: only one can be bound at a time, so
  // every renderer allocates its slots out of this (DescriptorHeap.h).
  Neuron::DescriptorHeap shaderVisibleHeap;
  shaderVisibleHeap.Create(device.Handle(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 16, true);

  // Space is black, and on this screen it is also the colour of every rail behind every card.
  Neuron::SceneTarget screen;
  screen.Create(device.Handle(), Neuron::BLACK);

  // The two renderers the interface is made of, and the whole of what it needs: rectangles and
  // glyphs. There is no widget tree, no retained scene and no texture atlas beyond the font
  // (ADR-014).
  Neuron::ShapeRenderer shapes;
  shapes.Create(device.Handle());

  Neuron::FontRenderer text;
  text.Create(device, shaderVisibleHeap);

  // ---- The match, over a socket ------------------------------------------------------------------
  //
  // THE CLIENT TALKS TCP EVEN WHEN THE SERVER IS ON THE NEXT THREAD (ADR-028). That is deliberate:
  // a local path that bypassed the wire would be a path that works and a network path nobody runs
  // until six people are waiting. The host's own client is a socket client like everybody else.
  const auto startedAt = std::chrono::steady_clock::now();

  // Input first: the join screen reads both, and it runs before the match does.
  Neuron::PointerInput pointer;
  pointer.Create(_window);
  g_pointerInput = &pointer;

  Neuron::KeyboardInput keyboard;
  g_keyboardInput = &keyboard;

  Lockstep::MatchConnection connection;

  // ---- The lobby, when this process is the host --------------------------------------------------
  //
  // **Open before anybody logs in, and with no match in it.** The order is log in, then start a
  // game, and it cannot be anything else: how many are playing is not knowable until they have
  // arrived, and the galaxy cannot be generated until it is known. So the server listens first and
  // the match is created later, on the same thread, from a seed and a struct of numbers.
  std::unique_ptr<Lockstep::HostedServer> hosted;
  std::vector<std::string> seatTokens;
  std::string hostToken = _startup.token;

  if (_startup.role == Role::HostAndPlay)
  {
    const MatchPaths paths = PathsFor(_startup);
    const std::string& storePath = paths.store;
    const std::string& logPath = paths.log;

    // A match already in the store resumes, with the seats it was played with (ADR-042). The host
    // takes the first stored seat unless the command line named one; there is no seats screen to
    // choose from, because the seats were chosen when the match began.
    if (std::optional<Neuron::MatchStore::Contents> stored = LoadStoredMatch(storePath))
    {
      if (!_startup.tokenGiven && !stored->tokens.empty())
      {
        hostToken = stored->tokens.front();
      }
      hosted = std::make_unique<Lockstep::HostedServer>(_startup.port, std::move(*stored), storePath, logPath);
    }
    else
    {
      seatTokens = Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT);
      hostToken = seatTokens.front();
      hosted = std::make_unique<Lockstep::HostedServer>(_startup.port, seatTokens, storePath, logPath);
    }

    // The lobby has to be listening before there is any point drawing a screen that asks people to
    // join it. A port that could not be bound is a fatal here, where the composition root turns it
    // into a message box, rather than a join screen that says "no answer" about the host's own
    // machine.
    for (std::int32_t attempt = 0; attempt < 200 && !hosted->Listening() && !hosted->Failed(); ++attempt)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (hosted->Failed())
    {
      Neuron::Fatal("{}", hosted->Failure());
    }
  }

  // ---- Screen 03, unless the command line already answered it ----------------------------------
  //
  // `--join host:port --token x` names a server and a seat, which is exactly what the join screen
  // asks for, so a client given both goes straight to the match. Everything else -- including the
  // host's own client -- starts here, because the host needs a seat too (ADR-029's first open
  // question, closed by ADR-036).
  //
  // **A `--join` that cannot reach anything falls through to the screen rather than to a message
  // box.** A message box tells a player their address was wrong and takes away the only place they
  // could fix it; the join screen is pre-filled with what the command line asked for, so the fix
  // is one character and a tap.
  bool askForAServer = !_startup.joinGiven;

  if (_startup.joinGiven)
  {
    // ---- Connect, and wait long enough to be told no --------------------------------------------
    //
    // **An open socket is not an accepted token, and since ADR-043 it is not even a connection.**
    // `Open` starts a handshake; the peer answering, and then the refusal or the welcome, both
    // arrive on later `Pump` calls. So this waits for a settled answer rather than for a return
    // value, and re-opens whenever an attempt dies -- the server may still be binding its port,
    // which used to be handled by retrying `Open` until it stopped failing.
    //
    // Bounded, because a client that spins forever on a server that will never come up is a window
    // that never draws and never says why. What it does instead is open the join screen, where the
    // address it could not reach is in a field the player can edit.
    using namespace std::chrono_literals;
    const auto giveUpAt = std::chrono::steady_clock::now() + 2s;
    for (;;)
    {
      using Status = Lockstep::MatchConnection::Status;
      const Status status = connection.State();
      if (status == Status::Playing || status == Status::Refused)
      {
        break;
      }
      if (std::chrono::steady_clock::now() >= giveUpAt)
      {
        break;
      }
      if (status == Status::Idle || status == Status::Lost)
      {
        (void)connection.Open(_startup.host, _startup.port, _startup.token);
      }

      connection.Pump(std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count());
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    askForAServer = connection.State() != Lockstep::MatchConnection::Status::Playing;

    // Deliberately NOT reset here. The join screen reads the refusal off the connection and puts up
    // screen 05 over its own card, so the player lands on the field they need to edit AND is told
    // why they are looking at it. Resetting first put them on a blank join screen with no
    // explanation, which is a worse answer than the message box it replaced.
  }

  if (askForAServer)
  {
    const std::string offered = std::format("{}:{}", _startup.host, _startup.port);
    const std::string offeredToken = _startup.joinGiven ? _startup.token : hostToken;
    if (!RunJoinScreen(device, screen, shapes, text, pointer, keyboard, connection, offered, offeredToken, _startup.port, startedAt))
    {
      return EXIT_SUCCESS;
    }
  }

  // ---- The host arranges the match, now that they are logged in ---------------------------------
  //
  // Seats after login, which is the order the owner asked for and the only one in which the screen
  // can say who is here. `ENTER MATCH` waits for every human seat to be connected. A resumed match
  // has its seats already and skips this.
  if (hosted != nullptr && !hosted->Resumed())
  {
    std::vector<std::string> playing;
    std::vector<std::optional<Lockstep::BotPolicy>> bots;
    Lockstep::SeatsPage::Entry entry = Lockstep::SeatsPage::Entry::Match;
    const std::int32_t hostSeat =
      RunSeatsScreen(device, screen, shapes, text, pointer, keyboard, _window, *hosted, seatTokens, playing, bots, entry);
    if (hostSeat < 0)
    {
      return EXIT_SUCCESS;
    }

    constexpr std::uint64_t GALAXY_SEED = 0x4652'4F4E'5449'4552ULL;

    // **The screen chooses the preset and the command line still wins** (ADR-051). `PRACTICE MATCH`
    // is `--phase0`'s argument made reachable by somebody who has never seen a command line; an
    // explicit `--tick` after it is still the thing that was typed on purpose.
    Lockstep::MatchRules rules = entry == Lockstep::SeatsPage::Entry::Practice ? Lockstep::PracticeRules()
                                 : _startup.phaseZero                          ? Lockstep::PhaseZeroRules()
                                                                               : Lockstep::MatchRules{};
    if (_startup.tickSeconds > 0)
    {
      rules.tickIntervalSeconds = _startup.tickSeconds;
    }
    rules.playerCount = static_cast<std::uint32_t>(playing.size());

    hosted->Begin(GALAXY_SEED, rules, std::move(bots));

    // The server builds the match on its own thread; this waits for it rather than racing it, so
    // that the first state the client asks for is a state that exists.
    for (std::int32_t attempt = 0; attempt < 400 && !hosted->Started(); ++attempt)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  // **Empty until the server says otherwise.** A player who joins before the host taps ENTER MATCH
  // is welcomed immediately and sent no state, and a fixture in that gap would be a fake match in
  // front of somebody waiting for a real one, indistinguishable from it. `ConnectionDialog` says
  // what is actually happening instead.
  Lockstep::MainPage page;
  page.Create(Lockstep::MatchState{});

  Lockstep::ConnectionDialog dialog;

  /// **This screen is redrawn when it changes, not sixty times a second** (ADR-047).
  ///
  /// A 4X at four locks a day spends nearly all of its time showing a picture that is not moving.
  /// Presenting it again at vsync costs a GPU and a laptop battery for no pixel anybody can see --
  /// and it was the one real performance cost in the tree, which is otherwise resolving ticks in
  /// microseconds.
  ///
  /// What counts as a change: a state from the server, a tap, a drag, a key, a dialog appearing or
  /// going, and the countdown's displayed second turning over. The last of those is what keeps the
  /// idle rate at one frame a second rather than none, and it is a real change: the number on the
  /// top bar is different.
  bool redraw = true;
  std::int64_t drawnSecond = -1;

  /// Whether any state has ever arrived.
  ///
  /// **Not `drawnTick != 0`, which is what this used to be and which was wrong at tick zero**
  /// (ADR-047). A match that has started but not yet locked its first tick sends a perfectly good
  /// state whose tick IS zero, so the test could not tell "the host has not started" from "the
  /// first tick has not resolved" -- and told a player looking at a drawn galaxy that there was
  /// nothing to show yet. It flashed past at a rehearsal tick and would have sat there for six
  /// hours at the authored one.
  bool everHadState = false;
  Lockstep::ConnectionDialog::Kind drawnDialog = Lockstep::ConnectionDialog::Kind::None;

  /// Whether the player has dismissed the MATCH FINISHED dialog to look at the last digest. Once,
  /// and it stays dismissed -- a dialog that came back every frame would make the digest unreadable.
  bool finishedDismissed = false;

  auto lastPing = std::chrono::steady_clock::now();
  bool wasLive = false;
  /// The tick this process last put on the screen. Zero until the first state arrives.
  std::uint32_t drawnTick = 0;

  auto previousFrame = std::chrono::steady_clock::now();

  while (PumpMessages())
  {
    const auto now = std::chrono::steady_clock::now();
    const double elapsedSeconds = std::chrono::duration<double>{now - previousFrame}.count();
    previousFrame = now;

    // ---- The wire ----------------------------------------------------------------------------
    //
    // The client never asks for a resolution and could not provoke one. It reads what arrived and
    // redraws when the server says the tick moved.
    const double secondsSinceStart = std::chrono::duration<double>(now - startedAt).count();
    connection.Pump(secondsSinceStart);

    if (connection.Live() != wasLive)
    {
      redraw = true;
      wasLive = connection.Live();
      Lockstep::MatchState state = page.State();
      state.connected = wasLive;
      page.Create(std::move(state));
    }

    if (connection.TakeFreshState() && !connection.Snapshot().empty())
    {
      Neuron::ByteReader reader{connection.Snapshot()};
      const Lockstep::Snapshot snapshot = Lockstep::Snapshot::Read(reader);

      // ---- Everything since this client last looked --------------------------------------------
      //
      // **The digests are concatenated, oldest first, and the ones already read are dropped**
      // (ADR-044). SCREENS.md 01 asks for exactly this -- "digest from `TickLog` + previous unread
      // ticks" -- and until the server kept more than one there was nothing to concatenate, so a
      // player who closed a lid overnight was told how many ticks they had missed and shown the
      // events of only the last of them.
      //
      // `drawnTick` is this process's memory, and R13 leaves the client nothing to write: a
      // restarted client has read nothing and takes the lot. That is honest rather than wrong --
      // it has not looked at any of this.
      std::vector<Lockstep::DigestEntry> digest;
      bool digestsDecoded = true;
      for (const Neuron::Protocol::TickDigest& carried : connection.Digests())
      {
        if (carried.tick <= drawnTick)
        {
          continue;
        }

        Neuron::ByteReader digestReader{carried.bytes};
        for (Lockstep::DigestEntry& entry : Lockstep::Snapshot::ReadDigest(digestReader))
        {
          digest.push_back(std::move(entry));
        }
        digestsDecoded = digestsDecoded && !digestReader.Failed() && digestReader.AtEnd();
      }

      // A state that did not decode is not a state. The reader fills a short record with zeros and
      // refuses a byte that names no enumerator, and either way what came out is not what the
      // server sent -- so the screen keeps the last state it could trust rather than drawing this.
      if (reader.Failed() || !reader.AtEnd() || !digestsDecoded)
      {
        Neuron::DebugTrace("A state message from the server did not decode; keeping the last one.\n");
      }
      else
      {
        Lockstep::MatchState state = Lockstep::ViewOf(snapshot, digest, connection.SecondsToLock());
        state.connected = true;

        // ---- How much happened while nobody was looking --------------------------------------
        //
        // **The composition root is the only thing that can know this**, because it is the only
        // thing that sees one state replaced by the next. A client that stayed connected gets
        // every tick as it resolves and is never behind; one that closed its lid for a night comes
        // back to a tick several later than the one it last drew, and the difference is what it
        // missed.
        //
        // It cannot survive a restart. R13 leaves the client nothing to write, so a fresh process
        // opens at zero however long the player was away -- which is honest rather than wrong:
        // this process has not looked at anything yet.
        if (drawnTick != 0 && state.match.tick > drawnTick + 1)
        {
          state.unreadTicks = state.match.tick - drawnTick;
          state.lastSeenTick = drawnTick;
        }
        drawnTick = state.match.tick;

        everHadState = true;
        redraw = true;
        page.Create(std::move(state));
      }
    }

    // ---- What is wrong, if anything (screens 04 and 05) ----------------------------------------
    //
    // Chosen from the connection and the state together, in one place, so that two of these can
    // never be true at once on the screen. The order is the order of severity: a refusal is final,
    // a lost link is not, a match with no first state has not started yet, and a finished match is
    // the only one of the four that is not a problem.
    Lockstep::ConnectionDialog::Kind kind = Lockstep::ConnectionDialog::Kind::None;
    Lockstep::ConnectionDialog::Facts facts;
    facts.server = connection.Server();
    facts.reason = connection.Refusal();
    facts.seat = connection.Player();
    facts.reconnects = connection.Reconnects();
    facts.secondsToNextAttempt = connection.SecondsToNextAttempt(secondsSinceStart);

    // No `BACK`: the join screen is behind the seats screen and a whole match, and for the host
    // there is no join screen to return to at all. `QUIT` is the honest button here.
    facts.canGoBack = false;

    if (connection.State() == Lockstep::MatchConnection::Status::Refused)
    {
      kind = Lockstep::ConnectionDialog::Kind::Refused;
    }
    else if (connection.State() == Lockstep::MatchConnection::Status::Lost ||
             connection.State() == Lockstep::MatchConnection::Status::Connecting)
    {
      // A reconnect passes through `Connecting` on its way back, and from the player's side that
      // is still the link being down. Letting the dialog blink out for the length of a handshake
      // and back in would read as the connection returning and going again.
      kind = Lockstep::ConnectionDialog::Kind::Lost;
      facts.lockCountdown = !everHadState ? std::string{} : Lockstep::MainPage::FormatCountdown(page.State().match.secondsToLock);
    }
    else if (!everHadState)
    {
      // Welcomed, and nothing has ever arrived. Either the host has not started the match or the
      // first state is still in flight -- and from where the player is sitting those are the same
      // thing, so one screen covers both and it resolves the moment a state arrives.
      kind = Lockstep::ConnectionDialog::Kind::Waiting;
    }
    else if (page.State().match.finished && !finishedDismissed)
    {
      kind = Lockstep::ConnectionDialog::Kind::Finished;
      facts.standings = std::format("{} OF {} - SCORE {} - LEADER {} {}", page.State().player.placement, page.State().player.playerCount,
                                    page.State().player.score, page.State().player.leader.name, page.State().player.leader.score);
    }

    dialog.Update(kind, facts, elapsedSeconds);
    if (kind != drawnDialog)
    {
      redraw = true;
      drawnDialog = kind;
    }

    switch (dialog.TakeAction())
    {
    case Lockstep::ConnectionDialog::Action::Quit:
      return EXIT_SUCCESS;

    case Lockstep::ConnectionDialog::Action::Retry:
      // Two different retries, because the two states they come from are different. A LOST link is
      // already being retried on a timer and the player is only saying "now"; a REFUSED one is not
      // being retried at all, and asking for it again means opening a new connection.
      if (connection.State() == Lockstep::MatchConnection::Status::Refused)
      {
        (void)connection.Reopen();
      }
      else
      {
        connection.RetryNow();
      }
      break;

    case Lockstep::ConnectionDialog::Action::ViewLastDigest:
      finishedDismissed = true;
      break;

    case Lockstep::ConnectionDialog::Action::Cancel:
    case Lockstep::ConnectionDialog::Action::Back:
    case Lockstep::ConnectionDialog::Action::EditToken:
    case Lockstep::ConnectionDialog::Action::None:
    default:
      break;
    }

    // Presence, once a second. It is a fact about being seen rather than about submitting, and it
    // is what keeps a player who is sitting and thinking out of custody.
    if (std::chrono::steady_clock::now() - lastPing > std::chrono::seconds(1))
    {
      connection.SendPing();
      lastPing = std::chrono::steady_clock::now();
    }

    // The countdown is the only thing on this screen that moves on its own. Everything else
    // changes because the player did something or because a tick resolved.
    page.Update(elapsedSeconds);

    // Drag before tap. They are mutually exclusive by construction -- PointerInput decides which
    // a press was, and reports only that one -- so the order is about reading rather than about
    // correctness: the rotation is applied before the frame that a tap would be tested against.
    Neuron::PointerInput::Drag drag = {};
    if (pointer.TakeDrag(drag) && !dialog.Visible())
    {
      redraw = true;
      page.HandleDrag(drag);
    }

    float tapXPixels = 0.0F;
    float tapYPixels = 0.0F;
    if (pointer.TakeClick(tapXPixels, tapYPixels))
    {
      redraw = true;

      // The dialog swallows everything it is over, the map included. A tap that reached the board
      // behind a CONNECTION LOST dialog would be an order edit this client cannot send, and the
      // player would have no way to tell which of their taps counted.
      if (!dialog.HandleTap(tapXPixels, tapYPixels) && page.HandleTap(tapXPixels, tapYPixels))
      {
        // Every edit goes over the wire at once and the server keeps the latest. That is what makes
        // "editable until the lock" work without the client having to know when the lock is.
        Lockstep::OrderSet orders = Lockstep::OrdersOf(page.State());
        orders.player = Lockstep::PlayerId{connection.Player()};

        Neuron::ByteWriter writer;
        orders.Write(writer);
        connection.SendOrders(writer.Bytes());
      }
    }

    // The countdown is the only thing here that moves on its own, and it moves once a second. Its
    // DISPLAYED second is what matters -- `FormatCountdown` rounds up, so this is the number on the
    // bar rather than the float behind it.
    const std::int64_t second = static_cast<std::int64_t>(std::ceil(std::max(0.0, page.State().match.secondsToLock)));
    if (second != drawnSecond)
    {
      redraw = true;
      drawnSecond = second;
    }

    if (!redraw)
    {
      // Nothing has changed. The swap chain still holds the last frame, so there is nothing to
      // present -- and a short sleep is what keeps this loop from spinning a core to do it.
      std::this_thread::sleep_for(std::chrono::milliseconds(IDLE_FRAME_MILLISECONDS));
      continue;
    }
    redraw = false;

    ID3D12GraphicsCommandList* commandList = device.BeginFrame();
    screen.BeginScene(commandList, device.BackBufferView());

    shapes.BeginFrame(device.FrameIndex());
    text.BeginFrame(device.FrameIndex());

    // Two layers, flushed apart. See `MainPage::DrawWorld`: one flush per frame would put the map's
    // labels on top of the panels drawn over them.
    page.DrawWorld(shapes, text);
    shapes.Flush(commandList);
    text.Flush(commandList);

    page.DrawInterface(shapes, text);

    // Shapes first, then text, in two draw calls rather than interleaved. Painter's order still
    // holds within each pass, and the one place it matters across them -- a caption on a card --
    // is fine because every glyph is drawn after every rectangle.
    shapes.Flush(commandList);
    text.Flush(commandList);

    // A third layer. The dialog dims everything above it, so it cannot share a flush with it.
    dialog.Draw(shapes, text);
    shapes.Flush(commandList);
    text.Flush(commandList);

    device.EndFrameAndPresent();
    device.DrainDebugMessages();
  }

  g_pointerInput = nullptr;
  g_keyboardInput = nullptr;

  // Drain the GPU here, not in ~Device. Destructors run in reverse declaration order, so the
  // SceneTarget's depth buffer would otherwise be released while the last submitted command list
  // still referenced it -- which the debug layer reports as OBJECT_DELETED_WHILE_STILL_IN_USE and
  // a release build turns into a use-after-free.
  device.WaitForGpu();
  device.DrainDebugMessages();

  return EXIT_SUCCESS;
}
} // namespace

int APIENTRY wWinMain(_In_ HINSTANCE _instance, _In_opt_ HINSTANCE _previousInstance, _In_ LPWSTR _commandLine, _In_ int _showCommand)
{
  UNREFERENCED_PARAMETER(_previousInstance);
  UNREFERENCED_PARAMETER(_commandLine);

  // Before the window, before anything. A process that is not DPI-aware gets its window
  // *bitmap-stretched* by Windows on a scaled display -- on a 125% desktop the 1280x720 client
  // this game asks for is blown up to 1600x900 by the compositor, with bilinear filtering. That
  // is precisely the resample Design/README.md section 1 rules out, and it is invisible from
  // inside the process: every D3D12 call still reports 1280x720 and every pixel we write is still
  // exact.
  //
  // Not an error check: a Windows build without the call is one where the manifest default
  // applies, and there is nothing useful to do about it here.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // Before the window too. With this on, a mouse click arrives as WM_POINTERDOWN exactly as a
  // finger does, so touch and mouse are one code path rather than two (MVP-01 section 2). If it
  // ever fails, the game gets no pointer messages from a mouse at all -- which is a thing to
  // report rather than to paper over with a WM_LBUTTONDOWN handler.
  if (!Neuron::PointerInput::EnableMouseAsPointer())
  {
    Neuron::DebugTrace("Input: EnableMouseInPointer failed; a mouse will not produce pointer messages.\n");
  }

  g_instance = _instance;

  const Startup startup = ParseCommandLine(_commandLine);

  // ---- The server half, when this process is one -----------------------------------------------
  //
  // R13 binds the shipped client and names the match store as its one exception (ADR-024). With one
  // executable in two roles the rule is about the ROLE and not the binary: a process acting as the
  // server writes a store, and a process that is only a client never does.
  // ---- Headless -------------------------------------------------------------------------------
  //
  // `--serve` draws nothing and runs until it is killed. It is the dedicated server and it is also
  // the headless runner: a match resolving on a schedule with nobody watching.
  //
  // A fatal on this path has no window to be shown in, so it goes to the match log -- the one
  // place somebody running a headless server is going to look.
  if (startup.role == Role::Serve)
  {
    const bool console = OpenConsole();
    try
    {
      // Headless has no seats screen to choose with, so a new match keeps the fixed list. ADR-036
      // replaced those for a host who can see a screen; `--serve` is the dedicated server and
      // whoever runs it reads the tokens out of the source exactly as they did before. A resumed
      // match uses the seats it was stored with.
      const MatchPaths paths = PathsFor(startup);
      Say(console, std::format("Lockstep server on port {}", startup.port));
      Say(console, std::format("store {}", paths.store));
      Say(console, std::format("log   {}", paths.log));

      const std::unique_ptr<Lockstep::HostedServer> hosted = StartHostedServer(startup, PhaseZeroTokens());

      while (true)
      {
        for (const std::string& line : hosted->TakeLog())
        {
          Say(console, line);
        }

        // A server thread that has stopped on a fatal has already written why to the match log.
        // What is left is to not sit here forever looking alive: the process ends, with a failing
        // exit code, so whatever started it can see that it did.
        if (hosted->Failed())
        {
          Say(console, hosted->Failure());
          return EXIT_FAILURE;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    }
    catch (const std::exception& error)
    {
      Neuron::MatchLog log{PathsFor(startup).log};
      log.Write(std::string("FATAL ") + error.what());
      Say(console, std::string("FATAL ") + error.what());
      return EXIT_FAILURE;
    }
  }

  if (!RegisterWindowClass(_instance))
  {
    return EXIT_FAILURE;
  }

  HWND window = CreateMainWindow(_instance, _showCommand);
  if (window == nullptr)
  {
    return EXIT_FAILURE;
  }

  // The composition root is the one place that catches. Debug.h routes every HRESULT that had to
  // succeed, every failed Win32 call and every broken invariant into one exception precisely so
  // that there is one place to tell a person about it.
  try
  {
    return RunGame(window, startup);
  }
  catch (const winrt::hresult_error& error)
  {
    MessageBoxW(nullptr, error.message().c_str(), WINDOW_TITLE, MB_OK | MB_ICONERROR);
    return EXIT_FAILURE;
  }
  catch (const std::exception& error)
  {
    MessageBoxA(nullptr, error.what(), "LockStep: Universe", MB_OK | MB_ICONERROR);
    return EXIT_FAILURE;
  }
}
