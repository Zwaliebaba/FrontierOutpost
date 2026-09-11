# TapRehearsal.ps1 -- drive the real client with real taps.
#
# Usage: powershell -ExecutionPolicy Bypass -File Build\TapRehearsal.ps1 -ProcessId <pid> -Taps 3
#
# Start the game yourself first, with a compressed clock:
#   Start-Process x64\Release\FrontierOutpost.exe -ArgumentList '--tick','30' -PassThru
#
# RUN IT WITH `powershell.exe`, NOT `pwsh` -- it needs System.Drawing, which PowerShell 7 does not
# load. IT MOVES THE REAL CURSOR: the client takes input through the Windows Pointer API
# (EnableMouseInPointer + WM_POINTERDOWN), so a posted WM_LBUTTONDOWN reaches nothing -- pointer
# messages carry ids the system owns. SendInput is the only way to produce a tap the game believes,
# and that means a real pointer on a real desktop. The window must come to the foreground or
# nothing is clicked, and the cursor is put back where it was.
#
# It finds the digest's filled action button and taps it, N times.
#
# WHY IT LOOKS FOR THE BUTTON INSTEAD OF BEING TOLD WHERE IT IS.
#
# The digest is ranked and re-laid out every tick, so the button moves: with one event it sits at
# y=117, with six it sits at y=322. Capturing a screenshot, reading it, and then sending a tap is a
# race against the tick -- and it is a race a human or a model loses, because a round trip takes
# longer than a tick does at rehearsal speed. Three attempts were wasted tapping where the button
# had been a tick ago.
#
# So: grab, scan, tap, all inside one process, between two frames. The filled button is the only
# thing in the digest column painted solid `you` blue (94,196,255), and column x=40 runs through it
# and misses the event dots, which stop at x=22.

param(
  [Parameter(Mandatory = $true)][int]$ProcessId,
  [int]$Taps = 3,
  [int]$GapMilliseconds = 900
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;

public class TapBuild {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);

  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT {
    public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo;
  }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public MOUSEINPUT mi; }
  const uint LEFTDOWN = 0x0002, LEFTUP = 0x0004;

  /// The vertical middle of the filled button, in client pixels, or -1 when there is not one.
  public static int FindButton(IntPtr window) {
    RECT client; GetClientRect(window, out client);
    RECT frame; GetWindowRect(window, out frame);
    POINT origin; origin.X = 0; origin.Y = 0; ClientToScreen(window, ref origin);
    int offsetX = origin.X - frame.Left, offsetY = origin.Y - frame.Top;

    using (Bitmap whole = new Bitmap(frame.Right - frame.Left, frame.Bottom - frame.Top))
    using (Graphics graphics = Graphics.FromImage(whole)) {
      IntPtr dc = graphics.GetHdc();
      PrintWindow(window, dc, 2);
      graphics.ReleaseHdc(dc);

      int first = -1, last = -1;
      for (int y = 60; y < client.Bottom - 1; y++) {
        Color c = whole.GetPixel(offsetX + 40, offsetY + y);
        bool blue = c.R == 94 && c.G == 196 && c.B == 255;
        if (blue) { if (first < 0) first = y; last = y; }
        else if (first >= 0) break;
      }
      return first < 0 ? -1 : (first + last) / 2;
    }
  }

  public static bool Foreground(IntPtr window) {
    SetForegroundWindow(window);
    System.Threading.Thread.Sleep(350);
    return GetForegroundWindow() == window;
  }

  public static void ClickClient(IntPtr window, int clientX, int clientY) {
    POINT p; p.X = clientX; p.Y = clientY;
    ClientToScreen(window, ref p);
    SetCursorPos(p.X, p.Y);
    System.Threading.Thread.Sleep(120);
    INPUT[] inputs = new INPUT[2];
    inputs[0].type = 0; inputs[0].mi.dwFlags = LEFTDOWN;
    inputs[1].type = 0; inputs[1].mi.dwFlags = LEFTUP;
    SendInput(2, inputs, Marshal.SizeOf(typeof(INPUT)));
  }

  public static POINT Cursor() { POINT p; GetCursorPos(out p); return p; }
  public static void PutCursor(int x, int y) { SetCursorPos(x, y); }
}
"@ -ReferencedAssemblies System.Drawing

[TapBuild]::SetProcessDPIAware() | Out-Null
$process = Get-Process -Id $ProcessId -ErrorAction Stop
$window = $process.MainWindowHandle
if ($window -eq [IntPtr]::Zero) { Write-Output "TapRehearsal: no window"; exit 1 }
if (-not [TapBuild]::Foreground($window)) { Write-Output "TapRehearsal: window would not come forward -- nothing clicked"; exit 1 }

$before = [TapBuild]::Cursor()
for ($i = 0; $i -lt $Taps; $i++) {
  $y = [TapBuild]::FindButton($window)
  if ($y -lt 0) { Write-Output "TapRehearsal: no filled button on screen right now"; continue }
  Write-Output "TapRehearsal: tap $($i + 1) at client (98,$y)"
  [TapBuild]::ClickClient($window, 98, $y)
  Start-Sleep -Milliseconds $GapMilliseconds
}
[TapBuild]::PutCursor($before.X, $before.Y)
Write-Output "TapRehearsal: done"
