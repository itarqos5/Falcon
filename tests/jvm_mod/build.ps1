# Builds tests/data/jvm/vmtest.jar from these sources with the JDK (javac --release 21): the stand-in API classes are only compiled against, never put in the jar.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$jdk = 'C:\Program Files\Java\jdk-25\bin'
$out = Join-Path $env:TEMP 'falcon-vmtest-classes'
if (Test-Path $out) { Remove-Item $out -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $out | Out-Null
$sources = Get-ChildItem (Join-Path $here 'api'), (Join-Path $here 'mod') -Recurse -Filter *.java | ForEach-Object { $_.FullName }
& "$jdk\javac.exe" --release 21 -d $out $sources
if ($LASTEXITCODE -ne 0) { throw 'javac failed' }
$stage = Join-Path $env:TEMP 'falcon-vmtest-jar'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item (Join-Path $out 'com') $stage -Recurse
Copy-Item (Join-Path $here 'mod\fabric.mod.json') $stage
Copy-Item (Join-Path $here 'mod\vmtest.mixins.json') $stage
$target = Join-Path $here '..\data\jvm\vmtest.jar'
New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
& "$jdk\jar.exe" --create --file $target --no-manifest -C $stage .
"made $target"
