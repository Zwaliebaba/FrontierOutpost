#include "OS.h"
#include "Profiler.h"
#include "ProgramLog.h"
#include "StackFrame.h"
#include "LteString.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <sys/stat.h>

#ifdef LIBLT_WINDOWS

  #include "process.h"
  #include "Shlobj.h"
  #include "DbgHelp.h"
  #include "windirent.h"
  #include "windows.h"
  #include "Tchar.h"
  #include "Direct.h"

  #include <crtdbg.h>
  #include <cstdlib>
  #include <intrin.h>

  #undef CreateDirectory
  #undef MessageBox

#else

  #include <dirent.h>
  #include <spawn.h>
  #include <unistd.h>

  extern char **environ;

#endif

void IntHandler(int value) {
  StackFrame_Print();
  exit(0);
}

void SegHandler(int value) {
  error("Access Violation");
  StackFrame_Print();
  exit(0);
}

#ifdef LIBLT_WINDOWS
  LONG _stdcall Win32SegHandler(EXCEPTION_POINTERS* pExcept) {
    SegHandler(0);
    return 0;
  }
#endif

bool OS_ChangeDir(const String& path) {
#ifdef LIBLT_WINDOWS
  return SetCurrentDirectoryA(path) != 0;
#else
  return chdir(path) == 0;
#endif
}

void OS_ConfigureSignalHandlers() {
#ifndef LIBLT_WINDOWS
  signal(SIGABRT, IntHandler);
  signal(SIGINT, IntHandler);
  signal(SIGSEGV, SegHandler);
#endif
}

bool OS_CreateDir(const String& path) {
#ifdef LIBLT_WINDOWS
  return CreateDirectoryA(path, NULL) != 0;
#else
  return mkdir(path, 0777) == 0;
#endif
}

void OS_CreatePath(const String& path) {
  if (!path.size())
    return;

  std::stringstream stream(path);
  String buf;
  String partialPath;
  std::vector<String> partials;
  while (getline(stream, buf, '/'))
    partials.push_back(buf);
  for (size_t i = 0; i+ 1 < partials.size(); ++i) {
    partialPath += partials[i] + '/';
    OS_CreateDir(partialPath);
  }

  if (path.back() == '/')
    OS_CreateDir(path);
}

bool OS_FileExists(String const& path) {
  struct stat s;
  return stat(path, &s) == 0;
}

String OS_GetAppDir() {
#ifdef LIBLT_WINDOWS
  char path[MAX_PATH];
  SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, path);
  return path;
#else
  // TODO
  return "/usr/bin";
#endif
}

String OS_GetDocumentsDir() {
#ifdef LIBLT_WINDOWS
  char path[MAX_PATH];
  SHGetFolderPath(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, path);
  return path;
#else
  return "./data";
#endif
}

/* The folder of the running executable, with a trailing separator, or "" if it
   cannot be told (on Windows, also when the path does not fit in MAX_PATH). */
String OS_GetExecutableDir() {
#ifdef LIBLT_WINDOWS
  char path[MAX_PATH];
  DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
  if (length == 0 || length == MAX_PATH)
    return "";
  String result = path;
  return result.substr(0, result.find_last_of("\\/") + 1);
#else
  char path[1024];
  ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (length <= 0)
    return "";
  path[length] = 0;
  String result = path;
  return result.substr(0, result.rfind('/') + 1);
#endif
}

String OS_GetUserDataPath() {
  static bool created = false;
  if (!created) {
    created = true;
    OS_CreateDir("./cache/");
  }
  return "./cache/";
}

String OS_GetWorkingDir() {
  char path[1024];
#ifdef LIBLT_WINDOWS
  if (!_getcwd(path, sizeof(path)))
    return "";
#else
  if (!getcwd(path, sizeof(path)))
    return "";
#endif
  return path;
}

bool OS_IsDir(String const& path) {
  struct stat s;
  return stat(path, &s) == 0 && (s.st_mode & S_IFDIR);
}

bool OS_IsFile(String const& path) {
  struct stat s;
  return stat(path, &s) == 0 && (s.st_mode & S_IFREG);
}

Vector<String> OS_ListDir(String const& path) {
  Vector<String> result;
  DIR* dir = opendir(path);
  if (!dir)
    return result;

  dirent* entry;
  while ((entry = readdir(dir)))
    result.push(entry->d_name);
  closedir(dir);
  return result;
}

namespace {
  bool gUnattended = false;

#ifdef LIBLT_WINDOWS
  /* Unattended, nobody can attach a debugger either, so a crash prints what it
     was and where: what failed, then each frame of the stack, with its function
     and line where the PDBs beside the executable have them. The process then
     ends as it would have. */
  void PrintStack(CONTEXT const& start) {
    HANDLE const process = GetCurrentProcess();
    HANDLE const thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, OS_GetExecutableDir().c_str(), TRUE);

    CONTEXT context = start;
    STACKFRAME64 frame = {};
  #ifdef _M_ARM64
    DWORD const machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = context.Pc;
    frame.AddrFrame.Offset = context.Fp;
    frame.AddrStack.Offset = context.Sp;
  #else
    DWORD const machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
  #endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int i = 0; i < 64; ++i) {
      if (!StackWalk64(machine, process, thread, &frame, &context, nullptr,
            SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
          !frame.AddrPC.Offset)
        break;
      /* A return address is the instruction after the call, which can be on
         the next line: the call is the byte before it. */
      DWORD64 const address = frame.AddrPC.Offset;
      DWORD64 const lookup = i ? address - 1 : address;
      std::cout << "  " << i << ": ";
      DWORD64 const base = SymGetModuleBase64(process, address);
      char module[MAX_PATH];
      if (base && GetModuleFileNameA((HMODULE)base, module, MAX_PATH)) {
        char const* name = strrchr(module, '\\');
        std::cout << (name ? name + 1 : module) << "+0x" << std::hex << (address - base);
      }
      else
        std::cout << "0x" << std::hex << address;
      std::cout << std::dec;

      alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
      SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
      symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
      symbol->MaxNameLen = 256;
      DWORD64 displacement = 0;
      if (SymFromAddr(process, lookup, &displacement, symbol))
        std::cout << ' ' << symbol->Name;
      IMAGEHLP_LINE64 line = {};
      line.SizeOfStruct = sizeof(line);
      DWORD column = 0;
      if (SymGetLineFromAddr64(process, lookup, &column, &line))
        std::cout << " (" << line.FileName << ':' << line.LineNumber << ')';
      std::cout << '\n';
    }
    std::cout << std::flush;
    StackFrame_Print();
  }

  /* An exception nothing handled, which ends the process with its code. */
  LONG WINAPI PrintCrash(EXCEPTION_POINTERS* pointers) {
    EXCEPTION_RECORD const& record = *pointers->ExceptionRecord;
    std::cout << "CRASH: exception 0x" << std::hex << record.ExceptionCode
      << " at 0x" << (DWORD64)record.ExceptionAddress;
    if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record.NumberParameters >= 2)
    {
      ULONG_PTR const access = record.ExceptionInformation[0];
      std::cout << (access == 1 ? ", writing" : access == 8 ? ", executing" : ", reading")
        << " 0x" << record.ExceptionInformation[1];
    }
    std::cout << std::dec << '\n';
    PrintStack(*pointers->ContextRecord);
    return EXCEPTION_CONTINUE_SEARCH;
  }

  void PrintWide(wchar_t const* text) {
    for (; text && *text; ++text)
      std::cout << (char)(*text < 128 ? *text : L'?');
  }

  /* A check of the C runtime's or the standard library's, a subscript out of
     range among them, which fails fast: no exception filter sees it. The debug
     CRT names the check and where it failed; the release CRT names nothing. */
  void PrintInvalidParameter(
    wchar_t const* expression,
    wchar_t const* function,
    wchar_t const* file,
    unsigned int line,
    uintptr_t)
  {
    std::cout << "CRASH: a check of the C runtime's failed";
    if (expression) {
      std::cout << ": ";
      PrintWide(expression);
      std::cout << " in ";
      PrintWide(function);
      std::cout << " (";
      PrintWide(file);
      std::cout << ':' << line << ')';
    }
    std::cout << '\n';
    CONTEXT context;
    RtlCaptureContext(&context);
    PrintStack(context);
    __fastfail(FAST_FAIL_INVALID_ARG);
  }

#ifdef _DEBUG
  /* The debug CRT's report of a failed check, before it acts on it. The
     standard library's checks, a subscript out of range among them, fail fast
     after their report without calling the invalid-parameter handler, so the
     stack is printed here, where the check failed. */
  int __cdecl PrintReport(int type, char*, int*) {
    if (type == _CRT_ASSERT || type == _CRT_ERROR) {
      std::cout << "The C runtime reports a failed check (on stderr) here:\n";
      CONTEXT context;
      RtlCaptureContext(&context);
      PrintStack(context);
    }
    return FALSE;
  }
#endif

  /* abort(), from assert() or std::terminate, which ends the process with 3. */
  void PrintAbort(int) {
    std::cout << "CRASH: abort\n";
    CONTEXT context;
    RtlCaptureContext(&context);
    PrintStack(context);
    OS_ExitImmediately(3);
  }
#endif
}

bool OS_IsUnattended() {
  return gUnattended;
}

void OS_ExitImmediately(int code) {
  std::cout << std::flush;
#ifdef LIBLT_WINDOWS
  TerminateProcess(GetCurrentProcess(), (UINT)code);
#endif
  std::_Exit(code);
}

void OS_SetUnattended() {
  gUnattended = true;
#ifdef LIBLT_WINDOWS
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  SetUnhandledExceptionFilter(PrintCrash);
  _set_invalid_parameter_handler(PrintInvalidParameter);
  signal(SIGABRT, PrintAbort);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  /* The debug CRT's own dialogs, which the release CRT does not have. */
  #ifdef _DEBUG
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, PrintReport);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  #endif
#endif
}

void OS_MessageBox(String const& title, String const& message) {
#ifdef LIBLT_WINDOWS
  if (gUnattended) {
    std::cout << "[" << title << "] : " << message << '\n' << std::flush;
    return;
  }
  MessageBoxA(NULL, message, title, MB_OK);
#else
  std::cout << "[" << title << "] : " << message << '\n';
  std::cerr << "[" << title << "] : " << message << '\n';
#endif
}

bool OS_Spawn(String const& path) {
  char* const argv[] = {strdup(path), NULL};
#ifdef LIBLT_WINDOWS
  STARTUPINFO info={sizeof(info)};
  PROCESS_INFORMATION processInfo;
  char commandLine[] = "";
  return CreateProcess(
    path, commandLine, NULL, NULL, TRUE, 0, NULL, NULL, &info, &processInfo)
    != 0;
#else
  pid_t pid;
  return posix_spawn(&pid, path, NULL, NULL, argv, environ) == 0;
#endif
}

void OS_WriteDump(String const& path) {
#ifdef LIBLT_WINDOWS
  typedef BOOL (_stdcall *pDumpFn)
    (HANDLE hProcess, DWORD ProcessId, HANDLE hFile, MINIDUMP_TYPE DumpType,
     PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
     PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
     PMINIDUMP_CALLBACK_INFORMATION CallbackParam);

  HANDLE hFile = CreateFileA(path,
    GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
    FILE_ATTRIBUTE_NORMAL, NULL);

  HMODULE h = LoadLibrary(_T("DbgHelp.dll"));
  LTE_ASSERT(h);
  if (!h)
    return;

  pDumpFn dumpFn = (pDumpFn)GetProcAddress(h, "MiniDumpWriteDump");
  if (!dumpFn)
    return;

  if(hFile && hFile != INVALID_HANDLE_VALUE) {
    BOOL rv = (*dumpFn)(GetCurrentProcess(), GetCurrentProcessId(),
                        hFile, MiniDumpNormal, 0, 0, 0);
    CloseHandle(hFile);
  }
  FreeLibrary(h);
#else
  /* TODO. */
#endif
}
