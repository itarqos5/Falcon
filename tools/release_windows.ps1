# Builds, signs and packages the Windows launcher for a release.
#
#   tools\release_windows.ps1                          # unsigned package (no certificate needed)
#   tools\release_windows.ps1 -Sign -CertThumbprint ... # signed, in the only order that keeps every signature valid
#
# The order matters, and this script is the only place that encodes it:
#   1. build the Falcon client (falcon_client_licensed)
#   2. sign build\licensed\Falcon.exe
#   3. verify that signature
#   4. build the launcher: windres copies the client into FALCON_CLIENT as bytes, so a signed client stays signed inside it
#   5. sign dist-launcher\FalconLauncher.exe
#   6. verify that signature
#   7. SHA-256 of everything, into SHA256SUMS.txt
#   8. FalconLauncher-Windows-x64.zip, holding the exe alone
# Nothing is written to either executable after it is signed: steps 7 and 8 only read it.
#
# signtool.exe comes with the Windows SDK ("Windows SDK Signing Tools for Desktop Apps", the Windows 10/11 SDK). Without -Sign it is not needed.
param(
    [switch]$Sign,
    [string]$CertThumbprint = "",
    [string]$PfxPath = "",
    [string]$PfxPassword = "",
    [string]$TimestampUrl = "http://timestamp.digicert.com",
    [string]$Publisher = "",
    [string]$OutDir = "",
    [int]$Jobs = 3
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
Push-Location $root
try {
    if (-not $OutDir) { $OutDir = Join-Path $root 'release' }

    function Find-SignTool {
        $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
        $roots = @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "${env:ProgramFiles}\Windows Kits\10\bin")
        $candidates = @()
        foreach ($base in $roots) {
            if (Test-Path $base) {
                $candidates += Get-ChildItem -Path $base -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
                    Where-Object { $_.FullName -match '\\x64\\signtool\.exe$' }
            }
        }
        if ($candidates.Count -gt 0) {
            return ($candidates | Sort-Object FullName -Descending | Select-Object -First 1).FullName
        }
        return $null
    }

    $signTool = $null
    if ($Sign) {
        $signTool = Find-SignTool
        if (-not $signTool) {
            throw 'signtool.exe was not found. Install the Windows SDK signing tools, or run without -Sign to build an unsigned package.'
        }
        if (-not $CertThumbprint -and -not $PfxPath) {
            throw '-Sign needs a certificate: -CertThumbprint <sha1 of the cert in the store> or -PfxPath <file> [-PfxPassword ...].'
        }
        Write-Host "signing with $signTool"
    }

    function Invoke-Sign([string]$File) {
        $signArgs = @('sign', '/v', '/fd', 'sha256', '/tr', $TimestampUrl, '/td', 'sha256')
        if ($CertThumbprint) { $signArgs += @('/sha1', $CertThumbprint) }
        elseif ($PfxPath) {
            $signArgs += @('/f', $PfxPath)
            if ($PfxPassword) { $signArgs += @('/p', $PfxPassword) }
        }
        $signArgs += $File
        & $script:signTool @signArgs
        if ($LASTEXITCODE -ne 0) { throw "signtool sign failed for $File" }
    }

    function Invoke-Verify([string]$File) {
        & $script:signTool verify /pa /v $File
        if ($LASTEXITCODE -ne 0) { throw "signtool verify failed for $File" }
    }

    # ---- 1. the client ------------------------------------------------------------------------------------------------------------
    $configureArgs = @('--preset', 'release')
    if ($Publisher) { $configureArgs += "-DFALCON_LAUNCHER_PUBLISHER=$Publisher" }
    Write-Host '== configure'
    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

    Write-Host '== build the client'
    & cmake --build build --target falcon_client_licensed -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'the client build failed' }

    $client = Join-Path $root 'build\licensed\Falcon.exe'
    if (-not (Test-Path $client)) { throw "the client was not built: $client" }

    # ---- 2-3. sign and verify the client, before it goes into the launcher ---------------------------------------------------------
    if ($Sign) {
        Write-Host '== sign the client'
        Invoke-Sign $client
        Write-Host '== verify the client'
        Invoke-Verify $client
    }

    # ---- 4. the launcher (embeds the client that was just signed) -----------------------------------------------------------------
    Write-Host '== build the launcher'
    & cmake --build build --target falcon_launcher -j $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'the launcher build failed' }

    $launcher = Join-Path $root 'dist-launcher\FalconLauncher.exe'
    if (-not (Test-Path $launcher)) { throw "the launcher was not built: $launcher" }

    # ---- 5-6. sign and verify the launcher ----------------------------------------------------------------------------------------
    if ($Sign) {
        Write-Host '== sign the launcher'
        Invoke-Sign $launcher
        Write-Host '== verify the launcher'
        Invoke-Verify $launcher
    }

    # ---- 7. the package -----------------------------------------------------------------------------------------------------------
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $packaged = Join-Path $OutDir 'FalconLauncher.exe'
    $zip = Join-Path $OutDir 'FalconLauncher-Windows-x64.zip'
    Copy-Item -LiteralPath $launcher -Destination $packaged -Force
    if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -LiteralPath $packaged -DestinationPath $zip -CompressionLevel Optimal

    $sums = Join-Path $OutDir 'SHA256SUMS.txt'
    $lines = @()
    foreach ($file in @($packaged, $zip)) {
        $hash = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
        $lines += "$hash  $(Split-Path -Leaf $file)"
    }
    Set-Content -LiteralPath $sums -Value $lines -Encoding ascii

    $version = (Get-Item -LiteralPath $launcher).VersionInfo.FileVersion
    Write-Host ''
    Write-Host "Falcon Launcher $version"
    Write-Host ("  {0,-34} {1,12:N0} bytes  {2}" -f 'FalconLauncher.exe', (Get-Item $packaged).Length, (Get-FileHash $packaged -Algorithm SHA256).Hash.ToLowerInvariant())
    Write-Host ("  {0,-34} {1,12:N0} bytes  {2}" -f 'FalconLauncher-Windows-x64.zip', (Get-Item $zip).Length, (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant())
    if (-not $Sign) { Write-Host '  (unsigned build: this is a development package, not a release)' }
    Write-Host ''
    Write-Host "Next: gh release upload v$version `"$packaged`" `"$zip`" `"$sums`" --clobber"
}
finally {
    Pop-Location
}
