<#
  Makes a world with the real game's own terrain generator, to compare Falcon's terrain with (`Falcon --compare-vanilla <world>`).

  It runs Minecraft's dedicated server from the 26.3 jar the launcher installed, headless, in a folder of its own, on one seed, forces a
  16 x 16 chunk area round the origin to be generated, saves, and stops. Nothing in your .minecraft folder is touched.

  NOTE: the server insists on an eula.txt with eula=true. Pass -AcceptEula to say you agree to Mojang's EULA
  (https://aka.ms/MinecraftEULA) for that scratch folder; without it the script stops.

  usage:  powershell -File tools\vanilla-reference.ps1 -OutDir D:\scratch\vanilla -Seed -2979490181791285901 -AcceptEula
#>
param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$Seed = "12345",
    [string]$McDir = "$env:APPDATA\horizon-launcher\minecraft",
    [string]$Version = "26.3",
    [string]$Java = "C:\Program Files\Java\jdk-25\bin\java.exe",
    [int]$MemoryMB = 3072,
    [switch]$AcceptEula
)
$ErrorActionPreference = 'Stop'
if (-not $AcceptEula) { throw "The server will not start without eula=true, which is YOUR agreement to Mojang's EULA (https://aka.ms/MinecraftEULA). Read it, and pass -AcceptEula if you agree." }
New-Item -ItemType Directory -Force $OutDir | Out-Null

# The libraries the version needs on Windows, from its json.
$versionDir = Join-Path $McDir "versions\$Version"
$json = Get-Content (Join-Path $versionDir "$Version.json") -Raw | ConvertFrom-Json
$classpath = @(Join-Path $versionDir "$Version.jar")
foreach ($lib in $json.libraries) {
    if (-not $lib.downloads.artifact) { continue }
    $allowed = $true
    if ($lib.rules) {
        $allowed = $false
        foreach ($rule in $lib.rules) {
            if ((-not $rule.os) -or ($rule.os.name -eq 'windows')) { $allowed = ($rule.action -eq 'allow') }
        }
    }
    $path = Join-Path $McDir ("libraries\" + ($lib.downloads.artifact.path -replace '/', '\'))
    if ($allowed -and (Test-Path $path)) { $classpath += $path }
}

"eula=true" | Set-Content (Join-Path $OutDir "eula.txt") -Encoding ascii
@"
level-seed=$Seed
level-name=world
online-mode=false
server-port=25599
spawn-protection=0
view-distance=10
simulation-distance=5
max-tick-time=-1
enable-status=false
generate-structures=true
gamemode=creative
difficulty=peaceful
spawn-monsters=false
spawn-animals=false
spawn-npcs=false
sync-chunk-writes=false
region-file-compression=deflate
pause-when-empty-seconds=-1
"@ | Set-Content (Join-Path $OutDir "server.properties") -Encoding ascii

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Java
$psi.Arguments = "-Xms512M -Xmx${MemoryMB}M -cp `"$($classpath -join ';')`" net.minecraft.server.Main --nogui"
$psi.WorkingDirectory = $OutDir
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$server = [System.Diagnostics.Process]::Start($psi)
try { $server.PriorityClass = 'BelowNormal' } catch {}
$lines = [System.Collections.Concurrent.ConcurrentQueue[string]]::new()
$null = Register-ObjectEvent -InputObject $server -EventName OutputDataReceived -Action { if ($EventArgs.Data) { $Event.MessageData.Enqueue($EventArgs.Data) } } -MessageData $lines
$server.BeginOutputReadLine()
function Wait-ForLine($pattern, $seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    $seen = @()
    while ((Get-Date) -lt $deadline -and -not $server.HasExited) {
        $line = $null
        while ($lines.TryDequeue([ref]$line)) { $seen += $line; if ($line -match $pattern) { return $true } }
        Start-Sleep -Milliseconds 300
    }
    return $false
}
if (-not (Wait-ForLine 'Done \(' 300)) { throw "the server did not start" }
$server.StandardInput.WriteLine("forceload add -128 -128 127 127")   # 16 x 16 chunks: the most one command takes
Start-Sleep -Seconds 150                                              # they generate on the server's own pace
$server.StandardInput.WriteLine("save-all flush")
$null = Wait-ForLine 'Saved the game' 180
Start-Sleep -Seconds 5
$server.StandardInput.WriteLine("stop")
if (-not $server.WaitForExit(120000)) { $server.Kill() }
"world is in $(Join-Path $OutDir 'world'); compare with:  Falcon.exe --compare-vanilla `"$(Join-Path $OutDir 'world')`""
