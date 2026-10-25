# Build from source

This repository contains Windows source and a pinned Android patch bundle.
Use your own PAL RMHP08 game image. Generated game code, ROMs, saves, SDKs,
build logs and signing keys are excluded from Git.

## Setup

Install Git and Docker Desktop with a working Linux/WSL2 engine. Use a checkout
path without commas. Windows builds also need the host C++/Windows SDK tools;
the preinstall command below prepares them and the cached Docker tools.

```powershell
git clone https://github.com/Dling11/monster_hunter_tri_emulator.git
Set-Location monster_hunter_tri_emulator
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\android-source.ps1 -Mode Restore
powershell -NoProfile -ExecutionPolicy Bypass -File .\dev.ps1 -Mode PreinstallEverything
powershell -NoProfile -ExecutionPolicy Bypass -File .\dev.ps1 -Mode Prepare -RomPath 'C:\Games\Monster Hunter Tri PAL.rvz'
```

The Android restore is needed for Windows too: both builds share the touch UI
in `android-source/runtime/src/touch_controls.cpp`. Preinstallation may require
Administrator rights for Windows build tools. Later builds reuse the caches.

For Android-only work, `dev.ps1 -Mode Preinstall` prepares Docker translation
tools without installing Windows C++ tools. Then run Prepare as above.

## Windows

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\rowell.ps1 -Mode Build
powershell -NoProfile -ExecutionPolicy Bypass -File .\rowell.ps1 -Mode Test
powershell -NoProfile -ExecutionPolicy Bypass -File .\rowell.ps1 -Mode Run -RomPath 'C:\Games\Monster Hunter Tri PAL.rvz'
```

Output: `playtest/PC/MH3_Rowell_Dev.exe` with its DLLs. The launcher reads the
existing image through prepared metadata, avoiding another RVZ copy. F2 opens
shared settings; F1 opens the original PC settings.

## Android

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\dev.ps1 -Mode Translate -Jobs 3 -OverlayJobs 2
powershell -NoProfile -ExecutionPolicy Bypass -File .\android-source\rowell-android.ps1 -Mode Preinstall
powershell -NoProfile -ExecutionPolicy Bypass -File .\android-source\rowell-android.ps1 -Mode Build
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package-phone.ps1
```

The build produces `android-source/playtest/MH3-Rowell-Dev-1.4.1-arm64.apk`.
That legacy filename is separate from the actual Android version in
`packaging/android/AndroidManifest.xml` and the packager's metadata.
The phone ZIP/APK, VERSION.json and ZIP checksum go in **dist/Android**.

Keep `android-source/.dev/rowell-dev.keystore` privately for compatible updates.
A new checkout makes a new key unless you restore your existing key locally.
Never commit the private key or add it to a release archive.

## Keep source changes reproducible

After editing the ignored Android checkout, export its changes before committing:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\android-source.ps1 -Mode Export
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\android-source.ps1 -Mode Verify
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-android-bundle.ps1
```

The source workflow checks bundle contents and clean/repeat restoration without
a game image. Tests remain source code; their local output stays ignored.
See [Android bundle details](../android/README.md).

## Publish a build

Review source, commit and push to this repository, then create a GitHub Release
for that exact source commit. Attach the platform ZIP and its checksums; Android
can additionally provide the direct APK and VERSION.json. Keep Windows and
Android downloads separate and include [credits](../CREDITS.md), the retained
licenses and access to the matching source. Do not distribute the entire local
workspace, game data, personal saves or signing keys.

The scripts do not commit, push or publish automatically. Local reports belong
in **.local/** or **logs/**. The source snapshot starts a fresh Git history;
upstream attribution and license notices remain in place.
