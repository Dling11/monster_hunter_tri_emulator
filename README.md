# MH3 - Low Spec

An independent, experimental Android compatibility workspace based on the Rowell fork of OceanTriRecomp.

**Status: experimental ARM32 APK built, signed and installed in a PC Android 11 test environment.** Intro, Village loading/movement, English settings, live resolution changes, saved resolution after restart, and Home/resume were checked there. Installation and sustained gameplay on the actual Realme/Infinix phones remain untested.

**APK downloads are temporarily unavailable.** This branch shares the experimental source code only. The standard [Android ARM64 and Windows downloads](https://github.com/Dling11/monster_hunter_tri_emulator#downloads) remain available on the main branch.

This is a 32-bit ARM APK. A 64-bit phone can use it if Android also supports 32-bit apps; 64-bit-only Android cannot install it. See [Android's architecture guidance](https://developer.android.com/google/play/requirements/64-bit).

Known PC-test visual issue: the character-selection preview omits the head, while the player and NPCs render correctly in the Village. Its cause and whether it occurs on physical phones remain unresolved. Full quests, online play and sustained phone performance have not been validated.

## Lighter defaults

- 30 FPS, 75% internal resolution; **Low (50%)** is available in Graphics.
- Original game textures, single-sample rendering and a 60 Hz display preference.
- One background asset worker/model workspace, an 8 MiB input queue budget, and a smaller optional replacement-texture cache.
- A compact ARM32 memory layout, 64-bit disc-file offsets, and an FPS-only HUD without repeated temperature-file probes.

The game, physics, original Wii RAM sizes, touch editor, combos, save tools and controller remapping remain available. Lower resolution reduces GPU work; actual speed still depends on the phone. Online keeps its 30 FPS sync limit.

## Separate application

| Item | Lite workspace |
| --- | --- |
| Android package | `com.rowell.mh3lite` |
| App label | MH3 - Low Spec |
| User-data directory | `MH3_Low_Spec` |
| First compatibility target | `armeabi-v7a` (32-bit ARM) |

The main MH Tri - Rowell application keeps its own package and data directory. This workspace does not update or migrate its saves automatically. Any future save transfer must be explicit.

## Development status

The copied source includes the existing touch controls, English menus, save tools, archive-selection validation and pending changes from the main development workspace. This snapshot is an experimental testing build. Physical-device validation is still pending.

The primary test devices are Realme C11 RMX3231 (2 GB RAM) and Infinix X6532 (3 GB RAM). Installation compatibility must be established before optimizing frame rate or memory usage. Android version and a 64-bit-capable processor alone do not establish support for 64-bit apps.

The build targets `armeabi-v7a`. Packaging checks each native ELF library, SDL Java/native signatures, APK signing and alignment. PC checks cover save-tool safety, backup rotation, capped logs, controller remapping, file validation, large-file access and the ARM32 memory mapping. PC tests do not establish phone FPS or thermal behavior.

## Build inputs

No game image, extracted assets, generated game translation, device saves, signing keys or build caches are included. Building requires your own European/PAL RMHP08 game extraction, the matching generated translation, the toolchain and an ABI-matched OpenSSL build.

See [Building and performance choices](docs/BUILDING.md). The current build uses portable Windows tools through `build-local.ps1`; Docker is not required.

## Credits

Custom Rowell features build on the work of:

- Original native port: [krisztsa/OceanTriRecomp](https://github.com/krisztsa/OceanTriRecomp)
- Android base: [Kiridor123/OceanTriRecomp-Android](https://github.com/Kiridor123/OceanTriRecomp-Android)
- Windows baseline: [masterd2003/OceanTriRecomp](https://github.com/masterd2003/OceanTriRecomp)

See [CREDITS.md](CREDITS.md) and [LICENSE](LICENSE). Monster Hunter Tri belongs to CAPCOM. This is an unofficial fan project, not affiliated with or endorsed by CAPCOM or Nintendo.
