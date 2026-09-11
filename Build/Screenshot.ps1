# Screenshot.ps1 -- capture the game's CLIENT AREA, for looking at what it actually drew.
#
# Usage:  powershell -ExecutionPolicy Bypass -File Build\Screenshot.ps1 -Exe x64\Release\FrontierOutpost.exe -Out shot.png
#
# RUN IT WITH `powershell.exe`, NOT `pwsh`. Windows PowerShell 5.1 loads System.Drawing; PowerShell
# 7 does not, and the Add-Type below fails there with eight CS1069s about types "forwarded to
# System.Drawing.Common". Nothing here needs 7.
#
# WHY THIS EXISTS, AND WHY IT IS NOT THREE LINES OF PrintWindow.
#
# `PrintWindow` draws the WHOLE WINDOW -- caption bar and borders included -- into the device
# context at its own top-left. Saving that into a bitmap the size of the client area does not give
# you the client area: it gives you the window's first 1280x720 pixels, which is the client area
# shifted down by the caption and across by the border, with the far edges cropped off. On this
# machine the window is 1298x767 and the client origin sits at (9, 38), so a naive capture loses
# nine pixels of the right-hand rail and thirty-eight of the bottom, and moves everything else.
#
# **Two "defects" were reported off images taken that way and neither was real.** One was a label
# said to be jammed against the window edge, which in the client area has its full fourteen pixels
# of padding and only looked flush because the capture had cut the last nine away. The other was a
# missing legend that was simply below the crop. Both cost a round trip to disprove, and a
# screenshot that is subtly wrong is worse than no screenshot, because it is evidence.
#
# So: capture the window, then crop to where the client area actually starts. `ClientToScreen` on
# (0,0) against `GetWindowRect` is what says where that is.
#
# It runs the executable itself rather than attaching to a running one, because the thing being
# looked at is usually the build that was just made. PW_RENDERFULLCONTENT (flag 2) is what makes
# this work without the window being in the foreground -- a capture that needs focus is a capture
# that races every other window on the machine.

param(
  [Parameter(Mandatory = $true)][string]$Exe,
  [Parameter(Mandatory = $true)][string]$Out,
  [int]$SettleMilliseconds = 2000,
  # Arguments for the executable, as ONE string: `-Arguments "--tick 4"`.
  #
  # One string and split here, rather than a `[string[]]` the caller builds, because this script is
  # invoked from several shells and only PowerShell parses `"a","b"` as two arguments -- from bash
  # the same text arrives as the single token `a,b`, which reaches the game as one unrecognised
  # word and is silently ignored. That cost a round of "why is the tick still six hours".
  #
  # `--tick 4` is the flag worth knowing: it runs the match at four seconds a tick, which is how you
  # reach a screen state that has anything on it without playing for six hours. Pair it with a
  # -SettleMilliseconds longer than the tick.
  [string]$Arguments = ''
)

$argumentList = @($Arguments -split '\s+' | Where-Object { $_ -ne '' })

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Drawing;
using System.Runtime.InteropServices;

public class ClientAreaGrab {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);

  public static string Shot(IntPtr window, string path) {
    RECT client; GetClientRect(window, out client);
    RECT frame; GetWindowRect(window, out frame);

    POINT origin; origin.X = 0; origin.Y = 0;
    ClientToScreen(window, ref origin);
    int offsetX = origin.X - frame.Left;
    int offsetY = origin.Y - frame.Top;

    int frameWidth = frame.Right - frame.Left;
    int frameHeight = frame.Bottom - frame.Top;

    using (Bitmap whole = new Bitmap(frameWidth, frameHeight))
    using (Graphics graphics = Graphics.FromImage(whole)) {
      IntPtr dc = graphics.GetHdc();
      PrintWindow(window, dc, 2);
      graphics.ReleaseHdc(dc);

      Rectangle area = new Rectangle(offsetX, offsetY, client.Right, client.Bottom);
      using (Bitmap cropped = whole.Clone(area, whole.PixelFormat))
        cropped.Save(path, System.Drawing.Imaging.ImageFormat.Png);
    }

    return client.Right + "x" + client.Bottom + " client area, cropped from a "
         + frameWidth + "x" + frameHeight + " window at " + offsetX + "," + offsetY;
  }
}
"@ -ReferencedAssemblies System.Drawing

[ClientAreaGrab]::SetProcessDPIAware() | Out-Null

$process = if ($argumentList.Count -gt 0) { Start-Process -FilePath $Exe -ArgumentList $argumentList -PassThru }
           else { Start-Process -FilePath $Exe -PassThru }
$window = [IntPtr]::Zero
for ($attempt = 0; $attempt -lt 40 -and $window -eq [IntPtr]::Zero; $attempt++) {
  Start-Sleep -Milliseconds 250
  $process.Refresh()
  $window = $process.MainWindowHandle
}

if ($process.HasExited) {
  Write-Output "Screenshot: the executable exited before it drew anything, code $($process.ExitCode)"
  exit 1
}
if ($window -eq [IntPtr]::Zero) {
  Write-Output "Screenshot: no window appeared within ten seconds"
  if (-not $process.HasExited) { $process.Kill() }
  exit 1
}

Start-Sleep -Milliseconds $SettleMilliseconds
Write-Output ("Screenshot: " + [ClientAreaGrab]::Shot($window, $Out) + " -> " + $Out)

if (-not $process.HasExited) {
  $process.CloseMainWindow() | Out-Null
  Start-Sleep -Milliseconds 800
  if (-not $process.HasExited) { $process.Kill() }
}
