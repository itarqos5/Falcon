# Builds the test mods from these sources with the JDK (javac --release 21): tests/data/jvm/vmtest.jar (the VM's arithmetic and registrations) and vmcontent.jar (items, a block, a key, a HUD,
# a button, with assets). The stand-in API classes are only compiled against, never put in a jar.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$jdk = 'C:\Program Files\Java\jdk-25\\bin'
$target = Join-Path $here '..\data\jvm'
New-Item -ItemType Directory -Force -Path $target | Out-Null
foreach ($mod in @(@{ name = 'vmtest'; dir = 'mod' }, @{ name = 'vmcontent'; dir = 'content' })) {
    $out = Join-Path $env:TEMP ('falcon-' + $mod.name + '-classes')
    if (Test-Path $out) { Remove-Item $out -Recurse -Force -Confirm:$false }
    New-Item -ItemType Directory -Force -Path $out | Out-Null
    $sources = Get-ChildItem (Join-Path $here 'api'), (Join-Path $here $mod.dir) -Recurse -Filter *.java | ForEach-Object { $_.FullName }
    & "$jdk\javac.exe" --release 21 -d $out $sources
    if ($LASTEXITCODE -ne 0) { throw 'javac failed' }
    $stage = Join-Path $env:TEMP ('falcon-' + $mod.name + '-jar')
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force -Confirm:$false }
    New-Item -ItemType Directory -Force -Path $stage | Out-Null
    Copy-Item (Join-Path $out 'com') $stage -Recurse
    # what the mod ships besides code: its json files, assets
    Get-ChildItem (Join-Path $here $mod.dir) -Exclude com | Copy-Item -Destination $stage -Recurse
    $jar = Join-Path $target ($mod.name + '.jar')
    if (Test-Path $jar) { Remove-Item $jar -Force -Confirm:$false }
    & "$jdk\jar.exe" --create --file $jar --no-manifest -C $stage .
    "made $jar"
}
