param(
    [int]$RenderDistance = 8,
    [string]$Name = 'falcon8',
    [string]$WorldFrom = (Join-Path $env:TEMP 'falcon-vs-vanilla\world-falcon'),             # a world folder made by make_world.ps1 (the seed vanilla uses too)
    [string]$Exe = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'build\Falcon.exe'),
    [string]$Assets = (Join-Path $env:LOCALAPPDATA 'Falcon\game\26.3\assets'),
    [string]$Data = '',                                                                         # the folder with the tables and data/ (default: laid out from the tables zip, as the launcher does)
    [string]$Pose = '9.5,63,1.5,0,0',                                                           # where the player stands (x,y,z,yaw,pitch): the ocean where vanilla starts
    [int]$Settle = 30,
    [int]$Shots = 3,
    [switch]$Hidden,                                                                            # a hidden, unfocused window (nothing on the screen): start-up and memory only
    [switch]$Warm,                                                                              # keep the game folder's caches from an earlier run of the same -Name
    [string]$Out = (Join-Path $env:TEMP 'falcon-vs-vanilla')
)
# The same measurement as run_vanilla.ps1 for Falcon, with the same options and the same seed: the time from starting the process until the game says the world is ready ("spawn area
# loaded"), and its memory once it has settled. In a visible 854x480 window it also takes the game's own screenshots (F2) with the debug screen on; with -Hidden the window is
# hidden (nothing is shown on the screen), the game takes one picture and ends by itself.
. (Join-Path $PSScriptRoot 'common.ps1')
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null
if (-not $Data) { $Data = Join-Path $env:TEMP 'falcon-cleanroom\26.3' }   # (made from the tables zip and the game's own data/ folder, the way the launcher lays them out)
$gameDir = Join-Path $env:TEMP "falcon-falcon-bench\$Name"
if (-not $Warm) { Remove-Item $gameDir -Recurse -Force -ErrorAction SilentlyContinue }   # -Warm keeps the caches of the game folder (what a second start has), the world is fresh either way
Remove-Item "$gameDir\saves" -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$gameDir\saves" | Out-Null
Copy-Item $WorldFrom "$gameDir\saves\New World" -Recurse
Write-BenchOptions (Join-Path $gameDir 'falcon_options.txt') $RenderDistance
$stdout = Join-Path $Out "$Name.stdout.txt"
$arguments = @('--assets', "`"$Assets`"", '--data', "`"$Data`"", '--game-dir', "`"$gameDir`"", '--play', '"New World"', '--size', '854x480', '--mute', '--cam', $Pose, '--username', 'Bench')
if ($Hidden) {
    $png = Join-Path $Out "$Name.png"
    $arguments += @('--background', '--capture', "`"$png`"", '--capture-after', [string]($Settle + 25))   # (the game takes its picture and ends by itself)
}
$clock = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $Exe -ArgumentList $arguments -PassThru -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $Out "$Name.stderr.txt")
$hwnd = [IntPtr]::Zero
$windowMs = $null
if (-not $Hidden) {
    $hwnd = Get-WindowHandle $p 120000
    if ($hwnd -eq [IntPtr]::Zero) { throw 'no window' }
    $windowMs = $clock.ElapsedMilliseconds
    [FalconBenchWin]::Topmost($hwnd)
}
$readyMs = Wait-LogLine $stdout 'spawn area loaded' $clock 240000 $p
if ($readyMs -eq $null) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; throw 'the world did not come up' }
Start-Sleep -Seconds $Settle
$memory = Get-ProcessMemoryMB $p
$files = @()
if ($Hidden) {
    $memoryAfter = Get-ProcessMemoryMB $p
    [void]$p.WaitForExit(120000)
} else {
    Send-KeyFocused $hwnd 0x72 0x3D   # F3: the debug screen
    Start-Sleep -Seconds 3
    $files = Save-GameShots $hwnd (Join-Path $gameDir 'screenshots') $Out $Name $Shots
    $memoryAfter = Get-ProcessMemoryMB $p
    Close-Window $hwnd
    if (-not $p.WaitForExit(30000)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
}
[pscustomobject]@{ name = $Name; renderDistance = $RenderDistance; hidden = [bool]$Hidden; windowMs = $windowMs; readyMs = $readyMs; memorySettled = $memory; memoryAtEnd = $memoryAfter; pose = $Pose; shots = $files } |
    ConvertTo-Json -Depth 4 | Set-Content (Join-Path $Out "$Name.json")
Get-Content (Join-Path $Out "$Name.json")
