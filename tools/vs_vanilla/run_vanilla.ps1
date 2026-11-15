param(
    [int]$RenderDistance = 8,
    [string]$Name = 'vanilla8',
    [string]$WorldFrom = (Join-Path $env:TEMP 'falcon-vs-vanilla\world-vanilla'),            # a world folder made by make_world.ps1 (the seed Falcon uses too)
    [int]$Settle = 30,                                                                          # seconds in the world before it is measured
    [int]$Shots = 3,
    [string]$Out = (Join-Path $env:TEMP 'falcon-vs-vanilla')
)
# Starts vanilla Minecraft 26.3 (a VISIBLE window, 854x480) in a world that has not been generated yet and writes <Out>\<Name>.json: the time from starting the process until the game's
# log says the player is in the world ("logged in with entity id"; the picture follows a moment later, so this favours vanilla a little), the memory of the process once it has
# settled, and some of the game's own screenshots (F2; the debug screen is on) to read the frame rate and the position from. Then it closes the game. Nothing of the desktop is captured.
. (Join-Path $PSScriptRoot 'common.ps1')
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $Out | Out-Null
$gameDir = Join-Path $env:TEMP "falcon-vanilla-bench\$Name"
Remove-Item $gameDir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$gameDir\saves" | Out-Null
Copy-Item $WorldFrom "$gameDir\saves\New World" -Recurse
Write-BenchOptions (Join-Path $gameDir 'options.txt') $RenderDistance
$command = node (Join-Path $PSScriptRoot 'vanilla_command.mjs') $gameDir | ConvertFrom-Json
$quoted = $command.args | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }
$stdout = Join-Path $Out "$Name.stdout.txt"
$clock = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $command.java -ArgumentList $quoted -PassThru -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $Out "$Name.stderr.txt")
$hwnd = Get-WindowHandle $p 120000
if ($hwnd -eq [IntPtr]::Zero) { throw 'no window' }
$windowMs = $clock.ElapsedMilliseconds
[FalconBenchWin]::Topmost($hwnd)   # (it would open behind the window in use, and then it is not drawn)
$readyMs = Wait-LogLine $stdout 'logged in with entity id' $clock 240000 $p
if ($readyMs -eq $null) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; throw 'the world did not come up' }
Start-Sleep -Seconds $Settle
$memory = Get-ProcessMemoryMB $p
Send-KeyFocused $hwnd 0x72 0x3D   # F3: the debug screen
Start-Sleep -Seconds 3
$files = Save-GameShots $hwnd (Join-Path $gameDir 'screenshots') $Out $Name $Shots
$memoryAfter = Get-ProcessMemoryMB $p
Close-Window $hwnd
if (-not $p.WaitForExit(30000)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
$spawn = Select-String -Path $stdout -Pattern 'logged in with entity id' -Encoding ascii | Select-Object -First 1 -ExpandProperty Line
[pscustomobject]@{ name = $Name; renderDistance = $RenderDistance; windowMs = $windowMs; readyMs = $readyMs; memorySettled = $memory; memoryAtEnd = $memoryAfter; spawn = $spawn; shots = $files } |
    ConvertTo-Json -Depth 4 | Set-Content (Join-Path $Out "$Name.json")
Get-Content (Join-Path $Out "$Name.json")
