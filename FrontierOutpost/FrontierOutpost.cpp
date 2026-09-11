// FrontierOutpost.cpp -- process entry point, and the composition root of the main page.
//
// WHAT THIS EXECUTABLE SHOWS, as of 2026-09-10, is the ops console in Design/Screens: digest,
// map, orders. It used to show the MVP-01 isometric ship scene, and that code -- MeshRenderer,
// IsometricCamera, Starfield, ShipMesh, StationMesh, ShipView, World, Session -- is still in the
// tree and still built and tested. It is not reachable from here, because the main page is a
// different screen of the same game rather than a mode of that one, and a mode switch is not
// something this task was asked for (ADR-014).
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
#include "FrontierOutpost.h"

#include "Color.h"
#include "Device.h"
#include "FontRenderer.h"
#include "PointerInput.h"
#include "SceneTarget.h"
#include "ShapeRenderer.h"

#include "HostedServer.h"
#include "MainPage.h"
#include "MatchConnection.h"
#include "MatchFixture.h"
#include "SnapshotView.h"

#include "Socket.h"

#include <chrono>
#include <memory>
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

constexpr wchar_t WINDOW_CLASS_NAME[] = L"FrontierOutpostWindow";
constexpr wchar_t WINDOW_TITLE[] = L"Frontier Outpost";

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

/// `--serve [port]`, `--join <host[:port]>`, `--token <token>`. Anything else is host-and-play.
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
      std::string target = words[++index];
      const std::size_t colon = target.find(':');
      if (colon != std::string::npos)
      {
        startup.port = asPort(target.substr(colon + 1), startup.port);
        target = target.substr(0, colon);
      }
      startup.host = target;
    }
    else if (words[index] == "--token" && index + 1 < words.size())
    {
      startup.token = words[++index];
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

LRESULT CALLBACK WndProc(HWND _window, UINT _message, WPARAM _wParam, LPARAM _lParam)
{
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
  Frontier::MatchConnection connection;

  // The server may still be binding its port when we get here, so this retries rather than
  // assuming. A bounded retry, because a client that spins forever on a server that will never
  // come up is a window that never draws and never says why.
  constexpr std::int32_t CONNECT_ATTEMPTS = 200;
  for (std::int32_t attempt = 0; attempt < CONNECT_ATTEMPTS; ++attempt)
  {
    if (connection.Open(_startup.host, _startup.port, _startup.token))
    {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (connection.State() == Frontier::MatchConnection::Status::Idle)
  {
    MessageBoxA(nullptr, "Could not reach the match server.", "Frontier Outpost", MB_OK | MB_ICONERROR);
    return EXIT_FAILURE;
  }

  // Something has to be on screen before the first state arrives. The design reference's fixture is
  // exactly that and nothing more -- it is a test fixture now, and this is the one place it is
  // still drawn: for the fraction of a second between connecting and being welcomed.
  Frontier::MainPage page;
  page.Create(Frontier::MakeReferenceMatch());

  auto lastPing = std::chrono::steady_clock::now();

  Neuron::PointerInput pointer;
  pointer.Create(_window);
  g_pointerInput = &pointer;

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
    connection.Pump();

    if (connection.State() == Frontier::MatchConnection::Status::Refused)
    {
      MessageBoxA(nullptr, Neuron::Describe(connection.Refusal()), "Frontier Outpost", MB_OK | MB_ICONERROR);
      return EXIT_FAILURE;
    }

    if (connection.TakeFreshState() && !connection.Snapshot().empty())
    {
      Neuron::ByteReader reader{connection.Snapshot()};
      const Frontier::Snapshot snapshot = Frontier::Snapshot::Read(reader);

      Neuron::ByteReader digestReader{connection.Digest()};
      page.Create(Frontier::ViewOf(snapshot, Frontier::Snapshot::ReadDigest(digestReader), connection.SecondsToLock()));
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
    if (pointer.TakeDrag(drag))
    {
      page.HandleDrag(drag);
    }

    float tapXPixels = 0.0F;
    float tapYPixels = 0.0F;
    if (pointer.TakeClick(tapXPixels, tapYPixels))
    {
      if (page.HandleTap(tapXPixels, tapYPixels))
      {
        // Every edit goes over the wire at once and the server keeps the latest. That is what makes
        // "editable until the lock" work without the client having to know when the lock is.
        Frontier::OrderSet orders = Frontier::OrdersOf(page.State());
        orders.player = Frontier::PlayerId{connection.Player()};

        Neuron::ByteWriter writer;
        orders.Write(writer);
        connection.SendOrders(writer.Bytes());
      }
    }

    ID3D12GraphicsCommandList* commandList = device.BeginFrame();
    screen.BeginScene(commandList, device.BackBufferView());

    shapes.BeginFrame(device.FrameIndex());
    text.BeginFrame(device.FrameIndex());

    page.Draw(shapes, text);

    // Shapes first, then text, in two draw calls rather than interleaved. Painter's order still
    // holds within each pass, and the one place it matters across them -- a caption on a card --
    // is fine because every glyph is drawn after every rectangle.
    shapes.Flush(commandList);
    text.Flush(commandList);

    device.EndFrameAndPresent();
    device.DrainDebugMessages();
  }

  g_pointerInput = nullptr;

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
  std::unique_ptr<Frontier::HostedServer> hosted;
  if (startup.role != Role::Join)
  {
    constexpr std::uint64_t GALAXY_SEED = 0x4652'4F4E'5449'4552ULL;
    hosted = std::make_unique<Frontier::HostedServer>(startup.port, PhaseZeroTokens(), std::string{"frontier-match.store"}, GALAXY_SEED);
  }

  // ---- Headless -------------------------------------------------------------------------------
  //
  // `--serve` draws nothing and runs until it is killed. It is the dedicated server and it is also
  // the headless runner: a match resolving on a schedule with nobody watching.
  if (startup.role == Role::Serve)
  {
    while (true)
    {
      for (const std::string& line : hosted->TakeLog())
      {
        Neuron::DebugTrace("{}\n", line);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
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
    MessageBoxA(nullptr, error.what(), "Frontier Outpost", MB_OK | MB_ICONERROR);
    return EXIT_FAILURE;
  }
}
