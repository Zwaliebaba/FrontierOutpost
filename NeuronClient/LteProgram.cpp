#include "LteProgram.h"

#include "Keyboard.h"
#include "Module.h"
#include "Mouse.h"
#include "OS.h"
#include "StackFrame.h"
#include "LteWindow.h"

#include <ctime>

/* PERF HARNESS (local, uncommitted) */
#include "Profiler.h"
#include "Scheduler.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
double g_perfPresentMs = 0, g_perfEndFrameMs = 0, g_perfBeginFrameMs = 0;
extern double g_perfWaitMs, g_perfWaitIdleMs;
extern long long g_perfWaitCount, g_perfWaitIdleCount;
extern long long g_perfDraws, g_perfPolys;
namespace {
  std::chrono::steady_clock::time_point perfOrigin;
  double PerfNow() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - perfOrigin).count();
  }
}

namespace  {
  bool destructed = false;
}

Program::Program() : deleted(false) {
  perfOrigin = std::chrono::steady_clock::now();
  srand((uint)time(0));
  OS_ConfigureSignalHandlers();
}

Program::~Program() {
  AUTO_FRAME;
  destructed = true;
}

void Program::Delete() {
  deleted = true;
}

void Program::Execute() {
  FRAME("Initialize") {
    Window_Push(window);
    OnInitialize();
    Window_Pop();
  }

  FILE* perfCsv = nullptr;
  if (char const* path = getenv("PERF_CSV")) {
    perfCsv = fopen(path, "w");
    if (perfCsv)
      fprintf(perfCsv, "frame,start_ms,frame_ms,input_ms,update_ms,modules_ms,display_ms,present_ms,endframe_ms,beginframe_ms,wait_ms,waits,waitidle_ms,waitidles,draws,polys,serial,threaded\n");
  }
  /* Frames at which the engine profiler takes one second, comma-separated. */
  char const* perfProfile = getenv("PERF_PROFILE");
  long long perfFrame = 0;

  while (window->IsOpen()) {
    if (deleted)
      break;
    Window_Push(window);

    if (perfProfile) {
      char const* p = perfProfile;
      while (*p) {
        long long n = strtoll(p, (char**)&p, 10);
        if (n == perfFrame) {
          printf("PERF: profiler starts at frame %lld (%.3f s)\n", perfFrame, PerfNow() / 1000.0);
          fflush(stdout);
          Profiler_Auto(1.0f);
        }
        if (*p == ',') ++p; else if (*p) break;
      }
    }

    g_perfPresentMs = g_perfEndFrameMs = g_perfBeginFrameMs = 0;
    g_perfWaitMs = g_perfWaitIdleMs = 0;
    g_perfWaitCount = g_perfWaitIdleCount = 0;
    g_perfDraws = g_perfPolys = 0;
    double t0 = PerfNow();

    FRAME("InputUpdate") {
      Mouse_Update();
      Keyboard_Update(window->HasFocus());
    }

    FRAME("WindowUpdate") {
      Window_Pop();
      window->Update();
      Window_Push(window);
    }
    double t1 = PerfNow();

    OnUpdate();
    double t2 = PerfNow();

    Module_UpdateGlobal();
    double t3 = PerfNow();

    FRAME("Display")
      window->Display();
    Window_Pop();
    double t4 = PerfNow();

    if (perfCsv) {
      Scheduler* scheduler = Scheduler_Get();
      fprintf(perfCsv, "%lld,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%lld,%.3f,%lld,%lld,%lld,%d,%d\n",
        perfFrame, t0, t4 - t0, t1 - t0, t2 - t1, t3 - t2, t4 - t3,
        g_perfPresentMs, g_perfEndFrameMs, g_perfBeginFrameMs,
        g_perfWaitMs, g_perfWaitCount, g_perfWaitIdleMs, g_perfWaitIdleCount,
        g_perfDraws, g_perfPolys,
        scheduler->HasSerialJobs() ? 1 : 0, scheduler->HasThreadedJobs() ? 1 : 0);
    }
    ++perfFrame;
  }
  if (perfCsv)
    fclose(perfCsv);
}

/* TODO : Fix this ugly mess. */
bool Program_InStaticSection() {
  return destructed;
}
