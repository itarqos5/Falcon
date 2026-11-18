# Builds modapi/falcon-modapi.jar: Falcon's own Java classes for the game, the mod loader's API and the parts of the JDK that the VM has no native class for (src/jvm/vm.hpp, addPlatformJar).
#   src/ : the game's, Fabric's and the libraries' classes (net.minecraft.*, net.fabricmc.*, com.mojang.*, org.joml.*, net.falcon.*), compiled against the JDK
#   jdk/ : JDK classes under their own names (java.io, java.nio.file, java.util.stream ...), compiled with --patch-module java.base so that they keep them
# The jar is committed (like tests/data/jvm/*.jar); tools/embed_modapi.mjs makes src/jvm/modapi_data.cpp from it.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$jdk = 'C:\Program Files\Java\jdk-25\bin'
$out = Join-Path $env:TEMP 'falcon-modapi-classes'
if (Test-Path $out) { Remove-Item $out -Recurse -Force -Confirm:$false }
New-Item -ItemType Directory -Force -Path $out | Out-Null
$sources = @(Get-ChildItem (Join-Path $here 'src') -Recurse -Filter *.java | ForEach-Object { $_.FullName })
$jdkDir = Join-Path $here 'jdk'
$jdkSources = @()
if (Test-Path $jdkDir) { $jdkSources = @(Get-ChildItem $jdkDir -Recurse -Filter *.java | ForEach-Object { $_.FullName }) }
$all = $sources + $jdkSources
$args = @('-nowarn', '-encoding', 'UTF-8', '-source', '21', '-target', '21', '-Xlint:-options', '-proc:none', '-d', $out)
if ($jdkSources.Count -gt 0) { $args += @('--patch-module', "java.base=$jdkDir") }
$list = Join-Path $env:TEMP 'falcon-modapi-sources.txt'
$all | ForEach-Object { '"' + ($_ -replace '\\', '/') + '"' } | Set-Content $list -Encoding ASCII
& "$jdk\javac.exe" @args "@$list"
if ($LASTEXITCODE -ne 0) { throw 'javac failed' }
$jar = Join-Path $here 'falcon-modapi.jar'
if (Test-Path $jar) { Remove-Item $jar -Force -Confirm:$false }
& "$jdk\jar.exe" --create --file $jar --no-manifest -C $out .
"made $jar ($((Get-Item $jar).Length) bytes, $($all.Count) sources)"
