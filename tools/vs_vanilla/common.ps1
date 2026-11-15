# Helpers shared by the scripts of tools/vs_vanilla: a key press into a window that is not in front, closing a window the way the X does, a process's memory, waiting for a line in a game's
# log, and the games' own screenshots. (Nothing of the desktop is ever captured.)
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FalconBenchWin {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] static extern bool AttachThreadInput(uint idAttach, uint idAttachTo, bool fAttach);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    // Puts a window in front (the game reads its keys only when it has the focus) and returns the window that was in front, to be given back with Focus() when done.
    [DllImport("user32.dll")] static extern void SwitchToThisWindow(IntPtr hWnd, bool altTab);
    public static bool IsForeground(IntPtr h) { return GetForegroundWindow() == h; }
    public static IntPtr Focus(IntPtr target) {
        IntPtr previous = GetForegroundWindow();
        uint pid;
        uint foreground = GetWindowThreadProcessId(previous, out pid);
        uint current = GetCurrentThreadId();
        AttachThreadInput(current, foreground, true);
        SetForegroundWindow(target);
        SwitchToThisWindow(target, true);
        AttachThreadInput(current, foreground, false);
        return previous;
    }
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    // Keeps a window above the others (a game window that opens behind another one is not drawn, and its loading stalls); the window is closed at the end of a run.
    public static void Topmost(IntPtr h) { SetWindowPos(h, new IntPtr(-1), 0, 0, 0, 0, 0x0001 | 0x0002 | 0x0040); }
    // The first visible top-level window of a process.
    public static IntPtr FindWindowOf(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate (IntPtr h, IntPtr l) {
            uint p;
            GetWindowThreadProcessId(h, out p);
            if (p == pid && IsWindowVisible(h)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@
[void][FalconBenchWin]::SetProcessDPIAware()

# The window of a process (waits until it has one).
function Get-WindowHandle([System.Diagnostics.Process] $process, [int] $timeoutMs = 60000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        $process.Refresh()
        if ($process.HasExited) { return [IntPtr]::Zero }
        $h = [FalconBenchWin]::FindWindowOf([uint32]$process.Id)
        if ($h -ne [IntPtr]::Zero) { return $h }
        Start-Sleep -Milliseconds 100
    }
    return [IntPtr]::Zero
}

# A key tapped in a window by message (the window need not be in front). $scan is the key's scan code (F2 = 0x3C, F3 = 0x3D).
function Send-KeyTap([IntPtr] $hwnd, [int] $vk, [int] $scan) {
    $down = [IntPtr]([int64]((($scan -shl 16) -bor 1)))
    $up = [IntPtr]([int64](3221225472 + ($scan -shl 16) + 1))   # (0xC0000000: the key was down and goes up)
    [void][FalconBenchWin]::PostMessage($hwnd, 0x100, [IntPtr]$vk, $down)
    Start-Sleep -Milliseconds 60
    [void][FalconBenchWin]::PostMessage($hwnd, 0x101, [IntPtr]$vk, $up)
}

# What the X of a window does: the game closes itself (and saves).
function Close-Window([IntPtr] $hwnd) { [void][FalconBenchWin]::PostMessage($hwnd, 0x10, [IntPtr]::Zero, [IntPtr]::Zero) }

function Get-ProcessMemoryMB([System.Diagnostics.Process] $process) {
    $process.Refresh()
    [pscustomobject]@{ workingSetMB = [Math]::Round($process.WorkingSet64 / 1MB, 1); privateMB = [Math]::Round($process.PrivateMemorySize64 / 1MB, 1) }
}

# The options both games read (the same keys: Falcon keeps the game's options.txt format): the video settings that make two runs comparable.
function Write-BenchOptions([string] $path, [int] $renderDistance) {
    @(
        "renderDistance:$renderDistance", "simulationDistance:$renderDistance", 'maxFps:260', 'enableVsync:false', 'renderClouds:"false"', 'particles:2', 'entityShadows:false',
        'pauseOnLostFocus:false', 'onboardAccessibility:false', 'fullscreen:false', 'tutorialStep:none', 'skipMultiplayerWarning:true', 'joinedFirstServer:true', 'lastServer:'
    ) | Set-Content -Path $path -Encoding ascii
}

# The milliseconds (on $clock) at which a line that matches $pattern shows up in a game's log file, or $null. The file is being written by the game: it is read without locking it.
function Wait-LogLine([string] $path, [string] $pattern, [Diagnostics.Stopwatch] $clock, [int] $timeoutMs, [System.Diagnostics.Process] $process) {
    while ($clock.ElapsedMilliseconds -lt $timeoutMs -and -not $process.HasExited) {
        try {
            $stream = New-Object System.IO.FileStream($path, 'Open', 'Read', 'ReadWrite')
            $reader = New-Object System.IO.StreamReader($stream)
            $text = $reader.ReadToEnd()
            $reader.Dispose()
            if ($text -match $pattern) { return $clock.ElapsedMilliseconds }
        } catch { }
        Start-Sleep -Milliseconds 50
    }
    return $null
}

# A key tapped in a game window that has to be in front to read keys (SDL ignores them otherwise): it is brought to the front for the moment, and the window that was in front is given back.
function Send-KeyFocused([IntPtr] $hwnd, [int] $vk, [int] $scan) {
    $previous = [IntPtr]::Zero
    for ($try = 0; $try -lt 8 -and -not [FalconBenchWin]::IsForeground($hwnd); ++$try) {
        $p = [FalconBenchWin]::Focus($hwnd)
        if ($previous -eq [IntPtr]::Zero) { $previous = $p }
        Start-Sleep -Milliseconds 250
    }
    Send-KeyTap $hwnd $vk $scan
    Start-Sleep -Milliseconds 400
    if ($previous -ne [IntPtr]::Zero) { [void][FalconBenchWin]::Focus($previous) }
}

# Some of the game's own screenshots (F2): taken one at a time, copied (once the game has finished writing them) to <out>\<name>-shotN.png, with their size recorded. Returns the list.
function Save-GameShots([IntPtr] $hwnd, [string] $shotDir, [string] $out, [string] $name, [int] $count) {
    $files = @()
    for ($i = 0; $i -lt $count; ++$i) {
        $before = @(Get-ChildItem $shotDir -Filter *.png -ErrorAction SilentlyContinue).Count
        Send-KeyFocused $hwnd 0x71 0x3C
        for ($w = 0; $w -lt 50 -and @(Get-ChildItem $shotDir -Filter *.png -ErrorAction SilentlyContinue).Count -eq $before; ++$w) { Start-Sleep -Milliseconds 100 }
        Start-Sleep -Milliseconds 800
        $latest = Get-ChildItem $shotDir -Filter *.png -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
        if (-not $latest) { Write-Warning "no screenshot $i (the game did not get the key)"; continue }
        $copy = Join-Path $out "$name-shot$i.png"
        Copy-Item $latest.FullName $copy
        $bitmap = New-Object System.Drawing.Bitmap $copy
        $files += [pscustomobject]@{ file = $copy; width = $bitmap.Width; height = $bitmap.Height }
        $bitmap.Dispose()
        Start-Sleep -Seconds 3
    }
    return $files
}
