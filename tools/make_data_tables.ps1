param(
    [string]$From = '',                                  # a folder that holds the tables (default: the newest horizon-launcher export of 26.3 on this PC)
    [string]$Out = (Join-Path $env:TEMP 'falcon-data-tables'),
    [string]$Version = '26.3',
    [string]$Tag = ''                                    # the release the zip will be uploaded to, e.g. v1.2.0 (default: the version in the launcher's CMakeLists)
)
# Makes the zip of Falcon's data tables that the launcher downloads (launcher/src/game_files.cpp: GameFiles::dataTableFiles) and puts its address and SHA-256 into
# launcher/src/data_tables.hpp. Run it when the tables change, then build the launcher and upload the zip to the release of that tag.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$files = @('registries.json', 'model-states.json', 'gameplay-facts.json', 'facts.json', 'items.json', 'block-behaviours.json', 'cached-collision.nbt', 'creative-inventory.json', 'entity-models.json', 'movement-properties.nbt', 'commands.json', 'biome-parameters/overworld.json')
if (-not $From) {
    $From = Get-ChildItem (Join-Path $env:APPDATA 'horizon-launcher\versions') -Directory | ForEach-Object { Join-Path $_.FullName ".local\horizon-client\$Version" } |
        Where-Object { Test-Path (Join-Path $_ 'registries.json') } | Sort-Object { (Get-Item (Join-Path $_ 'registries.json')).LastWriteTime } | Select-Object -Last 1
}
if (-not $From -or -not (Test-Path $From)) { throw "no folder with the tables: pass -From" }
foreach ($f in $files) { if (-not (Test-Path (Join-Path $From $f))) { throw "$f is not in $From" } }
if (-not $Tag) {
    $cmake = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
    if ($cmake -match 'VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') { $Tag = 'v' + $Matches[1] } else { throw 'pass -Tag' }
}
New-Item -ItemType Directory -Force $Out | Out-Null
$zip = Join-Path $Out "falcon-data-$Version.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($zip, 'Create')
try {
    foreach ($f in $files) { [void][System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, (Join-Path $From $f), $f, [System.IO.Compression.CompressionLevel]::Optimal) }
} finally { $archive.Dispose() }
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
$size = (Get-Item $zip).Length
Set-Content -Path "$zip.sha256" -Value "$hash  falcon-data-$Version.zip" -Encoding ascii
$url = "https://github.com/itarqos5/Falcon/releases/download/$Tag/falcon-data-$Version.zip"
$header = Join-Path $root 'launcher\src\data_tables.hpp'
$text = Get-Content $header -Raw
$text = [regex]::Replace($text, 'kDataTablesUrl = "[^"]*"', "kDataTablesUrl = `"$url`"")
$text = [regex]::Replace($text, 'kDataTablesSha256 = "[^"]*"', "kDataTablesSha256 = `"$hash`"")
$text = [regex]::Replace($text, 'kDataTablesSize = [0-9]+', "kDataTablesSize = $size")
Set-Content -Path $header -Value $text -NoNewline -Encoding ascii
"{0}  {1} bytes  {2}" -f $zip, $size, $hash
"launcher\src\data_tables.hpp now names $url"
