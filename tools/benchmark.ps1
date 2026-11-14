# Runs Falcon's built-in benchmark (a fixed flight over the terrain, see GameSession::startBenchmark) with a hidden window and prints its report,
# or, with -Compare, captures the same frames with two builds and compares them pixel by pixel.
#
#   tools\benchmark.ps1 -Exe dist\Falcon.exe -RenderDistance 32            # one benchmark run
#   tools\benchmark.ps1 -Exe dist\Falcon.exe -RenderDistance 32 -Env @{ FALCON_NO_OCCLUSION = '1' }   # with a feature switched off
#   tools\benchmark.ps1 -Compare -Exe old\Falcon.exe -OtherExe dist\Falcon.exe                      # same frames, two builds
#
# The switches the renderer understands (all are "set to anything to turn off"):
#   FALCON_NO_OCCLUSION   cave culling (draw everything in view, as before)
#   FALCON_NO_MDI         one draw call per run instead of one multi-draw per pass
#   FALCON_GL33           ask for an OpenGL 3.3 context (no multi-draw), as an older graphics card would give
#   FALCON_NO_LIGHTLUT    the light map as arithmetic per pixel instead of a 16 x 16 picture
#   FALCON_NO_FLATNORMAL  the face direction from screen derivatives instead of from the mesh (only matters while a light is held)
#   FALCON_NO_FACING_CULL draw the faces that turn away too (they are thrown away by the card): slower, same picture
#   FALCON_GL_CHECK       print the first OpenGL error the world drawing causes
param(
    [Parameter(Mandatory)] [string] $Exe,
    [string] $OtherExe,
    [switch] $Compare,
    [int] $RenderDistance = 16,
    [int] $Seconds = 30,
    [hashtable] $Env = @{},
    [string[]] $Extra = @(),
    # The folders of the game's files: the launcher's own by default, and the newest horizon-launcher export for the data tables.
    [string] $Assets = "$env:LOCALAPPDATA\Falcon\game\26.3\assets",
    [string] $Data,
    # A throw-away game folder holding a world called "New World" (made from -Level when it does not exist). The world's chunks are cached in it.
    [string] $GameDir = "$env:TEMP\falcon-benchmark",
    [string] $Level
)
$ErrorActionPreference = 'Stop'
if (-not $Data) {
    $Data = Get-ChildItem (Join-Path $env:APPDATA "horizon-launcher\versions") -Directory | ForEach-Object { Join-Path $_.FullName ".local\horizon-client\26.3" } |
        Where-Object { Test-Path "$_\registries.json" } | Select-Object -Last 1
}
if (-not (Test-Path "$GameDir\saves\New World\level.dat")) {
    if (-not $Level) { throw "No world in $GameDir yet: pass -Level <a level.dat> to make one." }
    New-Item -ItemType Directory -Force "$GameDir\saves\New World" | Out-Null
    Copy-Item $Level "$GameDir\saves\New World\level.dat"
}
$switches = 'FALCON_NO_OCCLUSION', 'FALCON_NO_MDI', 'FALCON_GL33', 'FALCON_NO_LIGHTLUT', 'FALCON_NO_FLATNORMAL', 'FALCON_NO_FACING_CULL', 'FALCON_GL_CHECK'

function Start-Falcon([string] $program, [string[]] $arguments, [hashtable] $environment, [string] $stdout, [int] $timeoutSeconds) {
    foreach ($n in $switches) { Remove-Item "Env:\$n" -ErrorAction SilentlyContinue }
    foreach ($k in $environment.Keys) { Set-Item "Env:\$k" $environment[$k] }
    $common = @('--assets', "`"$Assets`"", '--data', "`"$Data`"", '--game-dir', "`"$GameDir`"", '--play', '"New World"', '--mute')
    $p = Start-Process -FilePath $program -ArgumentList ($common + $arguments) -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout
    if (-not $p.WaitForExit($timeoutSeconds * 1000)) { $p.Kill(); Write-Warning "timed out: $program" }
    foreach ($n in $switches) { Remove-Item "Env:\$n" -ErrorAction SilentlyContinue }
}

if (-not $Compare) {
    $stdout = Join-Path $env:TEMP "falcon-benchmark-$RenderDistance.txt"
    Start-Falcon $Exe (@('--background', '--render-distance', "$RenderDistance@0.5", '--bench', "$Seconds") + $Extra) $Env $stdout ($Seconds + 240)
    Get-Content $stdout | Where-Object { $_ -match 'world drawing|benchmark:|frame time|CPU work|GPU time|headroom|RAM \(|chunks,|could not' }
    return
}

# --- pixel comparison -------------------------------------------------------------------------------------------------------------------------
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
public static class FrameDiff {
  public static string Run(byte[] a, byte[] b) {
    long differing = 0; int max = 0;
    for (int i = 0; i + 3 < a.Length; i += 4) {
      int d = 0;
      for (int c = 0; c < 3; c++) { int x = System.Math.Abs(a[i + c] - b[i + c]); if (x > d) d = x; }
      if (d > 0) differing++;
      if (d > max) max = d;
    }
    return string.Format("{0} of {1} pixels differ, largest channel difference {2}/255", differing, a.Length / 4, max);
  }
}
"@
function Read-Pixels([string] $path) {
    $bmp = New-Object System.Drawing.Bitmap($path)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $bmp.Width, $bmp.Height)
    $d = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bytes = New-Object byte[] ($d.Stride * $d.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($d.Scan0, $bytes, 0, $bytes.Length)
    $bmp.UnlockBits($d); $bmp.Dispose()
    return , $bytes
}
$poses = @('0,100,0,0,15', '0,75,40,135,5', '100,110,-60,250,-25', '60,48,60,45,0', '0,22,0,90,0', '0,40,0,0,0', '150,60,150,300,0', '-100,90,-100,120,-30')
$dir = Join-Path $env:TEMP 'falcon-frames'; New-Item -ItemType Directory -Force $dir | Out-Null
$i = 0
foreach ($pose in $poses) {
    $shots = foreach ($variant in @(@{ name = 'a'; exe = $Exe }, @{ name = 'b'; exe = $OtherExe })) {
        $png = Join-Path $dir "pose$i-$($variant.name).png"; Remove-Item $png -ErrorAction SilentlyContinue
        Start-Falcon $variant.exe (@('--cam', $pose, '--render-distance', "$RenderDistance@0.5", '--capture', "`"$png`"", '--capture-after', '12') + $Extra) $Env (Join-Path $dir 'out.txt') 120
        $png
    }
    "pose $pose : " + [FrameDiff]::Run((Read-Pixels $shots[0]), (Read-Pixels $shots[1]))
    ++$i
}
