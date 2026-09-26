#include "Module/PhysicsEngine.h"
#include "Module/SoundEngine.h"

#include "LTE/Keyboard.h"
#include "LTE/Profiler.h"
#include "LTE/Program.h"
#include "LTE/OS.h"
#include "LTE/Renderer.h"
#include "LTE/Script.h"
#include "LTE/Texture2D.h"
#include "LTE/Timer.h"
#include "LTE/Window.h"

#include <cstdio>
#include <cstdlib>

// #define TIME_LTSL_COMPILE

#ifdef TIME_LTSL_COMPILE
#include "LTE/Debug.h"
#endif

struct Launcher : public Program {
  String appName;
  ScriptFunction initialize;
  ScriptFunction update;
  Data instance;
  Module physicsEngine;
  Module soundEngine;
  /* The smoke mode: how many frames the app runs and how many are left before
     the launcher quits, 0 for no limit, and where the last of them is saved,
     empty for nowhere. */
  uint frames;
  uint framesLeft;
  String capturePath;
  bool failed;
  /* From the start, for the smoke mode's timings: the smoke job's time limits
     are set from them. */
  Timer timer;

  Launcher(String const& appName, uint frames, String const& capturePath, bool warp) :
    appName(appName),
    frames(frames),
    framesLeft(frames),
    capturePath(capturePath),
    failed(false)
  {
    /* A run with a set number of frames has nobody to answer a dialog. */
    if (frames)
      OS_SetUnattended();

    /* Work from the folder that holds GameData/: the executable's own folder or
       the nearest parent of it that has one (ADR-004). Without one, stay in the
       working directory launch was started in. */
    String dir = OS_GetExecutableDir();
    while (dir.size() && !OS_IsDir(dir + "GameData")) {
      dir.pop();
      while (dir.size() && dir.back() != '\\' && dir.back() != '/')
        dir.pop();
    }
    if (dir.size())
      OS_ChangeDir(dir);
    window = Window_Create("App Launcher", V2U(1920, 1080), true, false);
    window->SetSync(false);
    /* On WARP, the smoke mode draws offscreen: it never makes a swap chain
       (plan section 7). */
    Renderer_Initialize(warp, warp);
  }

  /* The app goes before the engines it uses, which its members would outlive:
     its windows play a sound as they close. */
  ~Launcher() {
    instance.Clear();
  }

  void OnInitialize() {
    Launch();
  }

  void Launch() {
    physicsEngine = nullptr;
    soundEngine = nullptr;
    physicsEngine = CreatePhysicsEngine();
    soundEngine = SoundEngine_XAudio2();

    Script_ClearCache();

#ifdef TIME_LTSL_COMPILE
    Timer timer;
#endif
    ScriptFunction main = ScriptFunction_Load("App/" + appName + ":Main");
#ifdef TIME_LTSL_COMPILE
    dbg | "LTSL Compile Time: " | timer.GetElapsed() | endl;
#endif

    if (!main) {
      printf("ERROR: Launcher failed to load script %s\n", appName.c_str());
      failed = true;
      deleted = true;
      return;
    }

    /* NOTE : We have to raw cast here due to issues with the usual
              .Convert<ScriptType>. Due to a major design flaw in the Type
              system, Type_Get<T> returns different static storage in the exe
              vs the dll, so type equality comparisons fail when they shouldn't.
              I am not going to fix this right now. */
    ScriptType app = *(ScriptType*)main->returnType->GetAux().data;
    initialize = app->GetFunction("Initialize");
    update = app->GetFunction("Update");
    instance.Construct(app->type);
    main->VoidCall(instance.data);

    if (initialize)
      initialize->VoidCall(0, instance);
    if (frames)
      PrintTime("initialized");
    if (!update)
      deleted = true;
  }

  void OnUpdate() {
    if (Keyboard_Pressed(Key_F1))
      SaveScreenshot();
    if (Keyboard_Pressed(Key_F2))
      Profiler_Auto(1.0f);

    if (Keyboard_Pressed(Key_W) && Keyboard_Control()) {
      deleted = true;
      instance.Clear();
      return;
    }

    if (Keyboard_Down(Key_Tilde))
      debugprint;

    if (Keyboard_Pressed(Key_F5))
      Launch();

    if (update)
      update->VoidCall(0, instance);

    if (physicsEngine)
      physicsEngine->Update();
    if (soundEngine)
      soundEngine->Update();

    /* The app has drawn this frame, and it is not yet displayed. */
    if (framesLeft) {
      if (framesLeft == frames)
        PrintTime("drew its first frame");
      if (--framesLeft == 0) {
        PrintTime("drew its last frame");
        if (capturePath.size())
          Texture_ScreenCapture()->SaveTo(capturePath);
        deleted = true;
      }
    }
  }

  /* Flushed, so that the line is kept if the smoke job stops the launcher. */
  void PrintTime(char const* what) {
    printf("launch: %s %s after %.1f s\n", appName.c_str(), what, timer.GetElapsed());
    fflush(stdout);
  }

  void SaveScreenshot() {
    String prefix = OS_GetUserDataPath() + "screenshot/";
    OS_CreatePath(prefix);
    for (uint i = 0;; ++i) {
      String path = prefix + ToString(i) + ".png";
      if (!OS_FileExists(path)) {
        Texture_ScreenCapture()->SaveTo(path);
        return;
      }
    }
  }
};

/* launch <app> [--warp] [--frames N] [--capture <path>]
   With --warp, the app draws on WARP, Windows' software adapter, with the
   Direct3D 12 debug layer, and offscreen: nothing is shown. With --frames, the
   app runs N frames and the launcher quits, and nothing waits for a click on a
   dialog. With --capture as well, the last frame is saved as a PNG at path; a
   relative path starts from the folder that holds GameData/ (ADR-011). This is
   the smoke mode of Design/Plan/NeuronClient-migration.md section 5.4. The
   exit code is 1 when the arguments or the app's script cannot be used, when
   the app stops short of its frames, when the capture was not saved, or when
   a run of set frames saw the debug layer report an error. */
int main(int argc, char const* argv[]) {
  String app;
  uint frames = 0;
  String capture;
  bool warp = false;
  for (int i = 1; i < argc; ++i) {
    String arg = argv[i];
    if (arg == "--warp")
      warp = true;
    else if (arg == "--frames" && i + 1 < argc) {
      char* end = nullptr;
      unsigned long n = std::strtoul(argv[++i], &end, 10);
      if (*end || n == 0 || n > 1000000) {
        printf("ERROR: --frames takes a count from 1 to 1000000, not %s\n", argv[i]);
        return 1;
      }
      frames = (uint)n;
    }
    else if (arg == "--capture" && i + 1 < argc)
      capture = argv[++i];
    else if (arg.size() && arg[0] != '-' && app.empty())
      app = arg;
    else {
      printf("ERROR: Launcher does not understand %s\n", arg.c_str());
      return 1;
    }
  }

  if (app.empty()) {
    printf("ERROR: Launcher expects an application name\n");
    return 1;
  }
  if (capture.size() && !frames) {
    printf("ERROR: --capture needs --frames, to know which frame to save\n");
    return 1;
  }

  Launcher launcher(app, frames, capture, warp);
  /* Only this run's capture counts, so an earlier one at the path goes. */
  if (capture.size())
    std::remove(capture.c_str());
  launcher.Execute();

  if (launcher.failed)
    return 1;
  if (launcher.framesLeft) {
    printf("ERROR: %s stopped %u frames short of %u\n",
      app.c_str(), launcher.framesLeft, frames);
    return 1;
  }
  if (capture.size() && !OS_FileExists(capture)) {
    printf("ERROR: Launcher did not save %s\n", capture.c_str());
    return 1;
  }
  if (frames && Renderer_GetDeviceErrorCount()) {
    printf("ERROR: the Direct3D 12 debug layer reported %u error(s) in %s\n",
      Renderer_GetDeviceErrorCount(), app.c_str());
    return 1;
  }
  return 0;
}

#if 0
#ifdef LIBLT_WINDOWS
  #pragma comment(linker, "/SUBSYSTEM:windows")
  #include <windows.h>
  int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, PSTR cmd, int show) {
    char const* argv[] = { "program" };
    return main(0, argv);
  }
#endif
#endif
