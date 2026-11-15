param(
    [string]$Out = (Join-Path $env:TEMP 'falcon-vs-vanilla'),
    [string]$FalconLevel = (Join-Path $env:TEMP 'falcon-createworld-test\saves\New World\level.dat'),   # a level.dat that Falcon made: its seed is the one both games use
    # A world_gen_settings.dat made by vanilla 26.3 itself (vanilla keeps a world's generation settings there, not in level.dat). One appears under saves\<world>\data\minecraft when
    # vanilla is given a world without it and made to carry on (it then makes a new world with default settings); only the seed in it is changed here.
    [string]$Template = (Join-Path $env:TEMP 'falcon-vs-vanilla\world_gen_settings.template.dat')
)
# Makes the two worlds of the comparison, with ONE seed (Falcon's): <Out>\world-vanilla (Falcon's level.dat + vanilla's settings file with that seed) and <Out>\world-falcon (level.dat).
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Template)) { throw "no $Template (a world_gen_settings.dat that vanilla made, see the comment above)" }
$seed = (node (Join-Path $PSScriptRoot 'world_seed.mjs') get $FalconLevel)
Remove-Item "$Out\world-vanilla", "$Out\world-falcon" -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force "$Out\world-vanilla\data\minecraft", "$Out\world-falcon" | Out-Null
Copy-Item $FalconLevel "$Out\world-vanilla\level.dat"
Copy-Item $Template "$Out\world-vanilla\data\minecraft\world_gen_settings.dat"
node (Join-Path $PSScriptRoot 'world_seed.mjs') set "$Out\world-vanilla\data\minecraft\world_gen_settings.dat" $seed
Copy-Item $FalconLevel "$Out\world-falcon\level.dat"
Set-Content (Join-Path $Out 'seed.txt') $seed
"seed $seed"
