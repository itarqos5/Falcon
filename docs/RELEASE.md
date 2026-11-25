# Releasing Falcon

Two things are published, and this page is about both: the **game** (`Falcon.exe`, built by `cmake --build --preset release`)
and the **launcher** (`FalconLauncher.exe`, which carries the client inside it). Linux and macOS have their own workflows
(`.github/workflows/release-linux.yml`, `.github/workflows/macos.yml`) and are unchanged by anything here.

## What a release contains

| Asset | What it is |
| --- | --- |
| `FalconLauncher-Windows-x64.zip` | `FalconLauncher.exe` alone: the recommended download |
| `FalconLauncher.exe` | the same binary, unzipped, for anyone who prefers it (the link older releases used) |
| `SHA256SUMS.txt` | SHA-256 of both files above |
| `FalconLauncher-linux-x86_64`, `….sha256` | built by the Linux workflow, as before |
| `FalconLauncher-macos-x86_64.zip`, `….sha256` | `FalconLauncher.app`, built by the macOS workflow on a tag (unsigned: see below) |
| `Falcon-macos-x86_64`, `….sha256` | the client alone, from the same workflow |

The zip holds one file on purpose: nothing else is needed to start Falcon, and a short "what should be in here" list is easy to
check.

## The Windows release, step by step

```
tools\release_windows.ps1                                # unsigned package in release\
tools\release_windows.ps1 -Sign -CertThumbprint <sha1>   # signed package
```

The script runs these steps in this order, which is the only order that keeps every signature valid:

1. `cmake --preset release` and `cmake --build build --target falcon_client_licensed`.
2. Sign `build\licensed\Falcon.exe` (the client that is about to be embedded).
3. Verify that signature.
4. `cmake --build build --target falcon_launcher`. The generated resource script
   (`launcher/CMakeLists.txt`, `FALCON_CLIENT RCDATA`) hands the client to `windres`, which copies its **bytes**: a signed client
   stays signed inside the launcher's resources, and the launcher writes those bytes to disk unchanged.
5. Sign `dist-launcher\FalconLauncher.exe`.
6. Verify it.
7. `SHA256SUMS.txt`.
8. `FalconLauncher-Windows-x64.zip`.

Nothing writes to either executable after step 5: hashing and zipping only read it. Re-linking, `editbin`, re-running `windres`,
or patching a resource after signing invalidates the signature, so the build is finished before it is signed.

`tools\release_windows.ps1` needs `signtool.exe` **only** with `-Sign`. It comes with the Windows SDK ("Windows SDK Signing Tools
for Desktop Apps"); the script finds it under `%ProgramFiles(x86)%\Windows Kits\10\bin\*\x64\`.

## Code signing

Falcon is signed with an ordinary **OV ("standard") code-signing certificate** from a public CA. That is the kind an independent
project can obtain (organisation or individual validation). Notes:

* **EV is not needed.** Extended Validation no longer bypasses SmartScreen: Microsoft's documentation says reputation is now built
  from the file hash and, for signed files, the publisher certificate, for OV and EV alike.
* **Azure Artifact Signing** (formerly *Trusted Signing*, $9.99/month) is an alternative that keeps the key in Azure, but it is
  limited to organisations in the US/Canada/EU/UK and to **individuals in the US and Canada**. Check eligibility before relying on it.
* **Always timestamp** (`/tr` and `/td sha256` are in the script): a timestamped signature stays valid after the certificate expires.
* **Set the publisher name you will actually sign with**, so the launcher can require it:
  `cmake --preset release -DFALCON_LAUNCHER_PUBLISHER="<the name the certificate shows>"` (the script passes `-Publisher` through).
  With it empty, any valid signature is accepted and an unsigned client is reported in `launcher.log` and allowed (that is what a
  build made before the certificate exists does). The same value is what `verifyFileSignature()` reads in
  `launcher/src/setup.cpp`.
* The client and the launcher are signed **separately and both**: the client is the file that is written to
  `%LOCALAPPDATA%\Falcon\client\…` and executed, so it is the one that has to carry a signature.

Once a certificate exists, the recommended flow is: `git tag vX.Y.Z` → `tools\release_windows.ps1 -Sign -CertThumbprint … -Publisher "…"`
→ `gh release upload`.

The macOS bundle is **not signed or notarised** by `macos.yml`, so Gatekeeper stops it: the player has to right-click → Open the
first time (or `xattr -dr com.apple.quarantine FalconLauncher.app`). Signing it means a Developer ID certificate and
`codesign --deep --options runtime` followed by `xcrun notarytool submit` and `xcrun stapler staple`, with the App Sandbox off
(the client writes its own worlds and reads the player's `.minecraft`). There is no equivalent of `verifyFileSignature` on macOS.

## What the launcher verifies at run time

| What | How | Where |
| --- | --- | --- |
| the bundled client | SHA-256 of the embedded bytes against the copy on disk, before it is used | `launcher/src/setup.cpp`, `installBundledClient` |
| the client about to run | Authenticode (whole chain, offline, no UI). Invalid → nothing starts; unsigned → logged and allowed; signed by another publisher → Invalid | `verifyFileSignature`, called from `app.cpp` |
| the WebView2 bootstrapper | Authenticode, and the signer must be Microsoft. It is downloaded under a name of its own into `%TEMP%`, and is deleted if it does not verify | `installWebView2Runtime` |
| Falcon's data tables | SHA-256 carried in the binary (`launcher/src/data_tables.hpp`), not downloaded | `game_files.cpp` |
| Mojang's files | the SHA-1 in Mojang's own manifest, over HTTPS | `game_files.cpp` |
| mods from Modrinth | address restricted to `cdn.modrinth.com`, size + SHA-1 from Modrinth's API, downloaded as `.part` and moved into place | `mods.cpp` |

The launcher's own hash check is about **damage and stray copies**; authenticity comes from the signature. A hash that travels with
the download from the same server proves nothing on its own — that is why `data_tables.hpp` compiles it in, and why the client's
signature is checked as well.

## Why the client is still inside the launcher

One file to download, nothing to unzip, and the launcher and the client can never be out of step: a new launcher always carries the
client it was built with, and the old `client\…` folder is removed on the next start. Shipping the two files separately would move
the same trust decision to the user ("did you keep both together?") without removing any code. If that ever changes, the extraction
step (`installBundledClient`) is the only place that would have to go: everything else takes the client's path as a parameter.

## Verifying a download (for players)

PowerShell, in the folder the files were downloaded to:

```powershell
Get-FileHash .\FalconLauncher-Windows-x64.zip -Algorithm SHA256   # compare with SHA256SUMS.txt
Expand-Archive .\FalconLauncher-Windows-x64.zip -DestinationPath .
Get-AuthenticodeSignature .\FalconLauncher.exe | Format-List Status, SignerCertificate
```

`Status` reads `Valid` once releases are signed; the file's **Properties → Digital Signatures** tab shows the same thing. An
unsigned build (tagged as such on the release page) reports `NotSigned`.

## Antivirus false positives

A heuristic or ML verdict is about the file, not about intent, and an unsigned, freshly-built binary has no reputation to lean on.
What this repository does about it, and what it cannot do:

* builds are **reproducible** (fixed PE timestamp, `FALCON_REPRODUCIBLE`, on by default) so one hash stands for one build and can be
  submitted and replied to;
* the version resource carries real metadata (`CompanyName`, `LegalCopyright`, `FileDescription`, versions), and the PE header now
  says OS/subsystem 6.1 instead of GNU ld's 4.0/5.2 defaults;
* the binaries that run are **signed**, and the launcher checks the ones it starts;
* releases are published as a zip with a checksum file.

Still required, and manual: **submitting the exact file to Microsoft** at
<https://www.microsoft.com/en-us/wdsi/filesubmission> ("software developer → incorrect detection"), with the SHA-256 and a link to
the source. No build change removes a definition; only Microsoft (or the vendor of whichever engine is wrong) can.

Do not treat a handful of engines on a multi-scanner as proof either way, and never "fix" a detection by hiding what the program
does: obfuscation, packing, import stripping or removing a check would make the binary less honest, not more trustworthy.

## Building the same bytes

```
cmake --preset release
cmake --build --preset release -j 3       # dist\Falcon.exe
cmake --build --preset launcher -j 3      # build\licensed\Falcon.exe and dist-launcher\FalconLauncher.exe
```

`-DFALCON_REPRODUCIBLE=OFF` restores a normal build timestamp. `-DFALCON_STRIP=OFF` keeps symbols in both executables (a debugger,
or `tools/resolve_samples.mjs`, can then read them). `-DFALCON_LAUNCHER_PUBLISHER=…` is described above.
