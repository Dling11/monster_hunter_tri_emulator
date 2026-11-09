# Building the experimental Lite app

Use an ARM32-capable Android 10 or later device with OpenGL ES 3.0. The matching APK was installed on a Realme C11 RMX3231 and reached character creation. Infinix installation and sustained gameplay performance still need physical testing.

The APK uses package `com.rowell.mh3lite` and folder `MH3_Low_Spec`. It does not update the normal Rowell app or automatically transfer its progress. Keep the Lite signing key for future updates.

Updating the earlier Lite build renames its `MH3_Rowell_Lite` folder to `MH3_Low_Spec` before native startup, carrying its saves, settings and backups together. If both folders already exist or the rename fails, startup stops with an explanation; no folders are merged or overwritten. The internal package/signing key stay unchanged so this remains an update to Lite.

## Windows build

Required: JDK 17, Python 3.11+, .NET SDK 8, CMake 3.25+, Ninja, Android platform 35/build-tools 35.0.0, NDK r29, and static OpenSSL built for Android ARM32/API 29. These can be portable tools; Android Studio is unnecessary.

Set `ANDROID_HOME` and `JAVA_HOME`, and add CMake/Ninja/.NET/Python to PATH. Alternatively create an ignored `.local/build-tools.json` containing `sdk`, `java`, `cmake`, `ninja`, `python`, and optional `ndk`/`openssl` paths. The CMake/Ninja values are directories containing their executables; SDK/JDK values are their roots. The default NDK and OpenSSL paths are `.tools/ndk29/android-ndk-r29` and `.tools/openssl-arm32`.

Provide your own matching European RMHP08 extraction at `Assets/DATA/sys/main.dol`, analysis map at `build/MAP.txt`, and matching `generated/` game translation. The build checks the DOL hash and refreshes native dispatch with the translator without replacing the authored game-function patches. Game-derived files are ignored and must not be committed.

Run in PowerShell:

```powershell
.\build-local.ps1 -Jobs 2
```

Output: `playtest/MH3-Low-Spec-0.1-armeabi-v7a.apk` with a matching JSON receipt. The packager validates native ELF architecture, matching SDL Java/native signatures, APK signature and ZIP alignment. Keep `.dev/rowell-lite.keystore` privately backed up; a different key cannot update an existing Lite installation.

The optional container helper remains available, but the local Windows build does not start Docker. ARM64 requires a separate build directory and ABI-matched OpenSSL; do not mix objects from different ABIs.

## Performance choices

Lite starts at 30 FPS, 75% internal resolution, VSync and single-sample rendering. Choose **Low (50%)** under Graphics for less GPU work, or **Wii original** for a sharper picture. Touch/menu text still uses the display resolution. Solo speed controls remain available, but extra speed requires spare CPU/GPU capacity. Online keeps its 30 FPS synchronization cap.

Asset conversion uses one worker/model workspace and an 8 MiB pending input budget. This trades some first-load speed for lower peak memory. HD replacements start disabled; their optional cache has a 32 MiB ceiling. The FPS HUD skips temperature-file probes. Original game assets, physics and guest RAM sizes are preserved; disk caches are not disabled because rebuilding assets repeatedly can cause stutter.

The Lite GLES backend permits render targets below the Wii EFB size; its existing logical viewport, scissor and feedback mappings continue to apply. This makes the 50%/75% settings reduce the actual target, rather than only changing a saved preference. The menu and touch controls render at the window's full resolution.

On ARM32 Android the runtime reserves a compact virtual memory window and folds Wii RAM aliases, avoiding the Linux fixed-window collision with Android runtime mappings. The window reserves address space, not 320 MiB of physical RAM.

PC emulation can establish ARM32 installation, native loading, large-file I/O and functional behavior. It cannot establish real-phone FPS, GPU-driver compatibility, heat, battery use or survival under Android's memory pressure.
