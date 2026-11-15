param([string]$Name = 'shot', [string]$Scenario = 'walk', [int]$After = 14, [string[]]$Extra = @(), [int]$Port = 25598, [string]$Exe = '', [string]$Out = '')
# A hidden Falcon client on the fake server (fake_server.mjs): takes a picture after $After seconds into <Out>\<Name>.png. Nothing is shown on screen.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Exe) { $Exe = Join-Path (Split-Path -Parent (Split-Path -Parent $here)) 'build\Falcon.exe' }
if (-not $Out) { $Out = Join-Path $env:TEMP 'falcon-fake-server' }
New-Item -ItemType Directory -Force $Out | Out-Null
(Get-Process -Id $PID).PriorityClass = 'BelowNormal'
$data = Get-ChildItem (Join-Path $env:APPDATA 'horizon-launcher\versions') -Directory | ForEach-Object { Join-Path $_.FullName '.local\horizon-client\26.3' } | Where-Object { Test-Path "$_\registries.json" } | Select-Object -Last 1
$scratch = Join-Path $env:TEMP 'falcon-mp-scratch'
New-Item -ItemType Directory -Force $scratch | Out-Null
$png = Join-Path $Out "$Name.png"
Remove-Item $png -ErrorAction SilentlyContinue
$srv = Start-Process -FilePath 'node' -ArgumentList @("`"$here\fake_server.mjs`"", "$Port", $Scenario, "$($After + 25)") -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $Out "$Name.server.txt") -RedirectStandardError (Join-Path $Out "$Name.server.err.txt")
Start-Sleep -Milliseconds 800
$a = @('--assets', "`"$env:LOCALAPPDATA\Falcon\game\26.3\assets`"", '--data', "`"$data`"", '--game-dir', "`"$scratch`"", '--mute', '--render-distance', '6@0.5', '--connect', "127.0.0.1:$Port", '--username', 'Viewer', '--capture', "`"$png`"", '--capture-after', "$After") + $Extra
$p = Start-Process -FilePath $Exe -ArgumentList $a -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $Out "$Name.txt") -RedirectStandardError (Join-Path $Out "$Name.err.txt")
if (-not $p.WaitForExit(120000)) { $p.Kill(); 'client timed out' }
if (-not $srv.HasExited) { $srv.Kill() }
if (Test-Path $png) { "ok $png" } else { 'no picture'; Get-Content (Join-Path $Out "$Name.txt") -Tail 12 }
