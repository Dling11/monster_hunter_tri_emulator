# MH Tri - Rowell

Maintained by **Rowell ([Dling11](https://github.com/Dling11))**. A modified
Monster Hunter Tri port for **Android and Windows**, based on OceanTriRecomp.
It uses static recompilation; despite this repository's name, it is not a
general-purpose Wii emulator. Supply your own European/PAL **RMHP08** game image.

## Features

- English app settings and customizable touch controls.
- Independent multi-touch and five combo buttons, including **R + X + A**.
- Original Wii **+ / -** labels, adjustable layout, size and opacity.
- Three retained save backups, readable timestamps and a selectable restore list.
- Yes/No save-tool prompts; a confirmed restore backs up progress and restarts.
- Experimental **1x / 1.25x / 1.5x** speed in solo play; City/online stays at 1x.
- Windows keyboard remapping with automatic saving and reset defaults.
  The keyboard editor is excluded from Android.

## Get started

| Platform | Requirements | Install |
| --- | --- | --- |
| Android | ARM64, Android 10+, OpenGL ES 3.0 | Extract the Android ZIP and install its APK. |
| Windows | Windows 10/11 x64, compatible graphics driver | Extract the Windows package; keep its EXE and DLLs together. |

Open **MH Tri - Rowell**, grant the requested storage access on Android and
choose your PAL RVZ/ISO/WIA image. Extract compressed game archives first.
No game image, extracted game assets or character saves are supplied.

App data stays in **MH3_Rowell_Dev**, separately from the original Android
app's **MH3** folder. Updates retain your Rowell saves when signed with the
same key. Save inside Tri before using backup/export; these are save-file
tools, not save states.

- [Android installation](docs/ANDROID_INSTALL.md)
- [Controls and save tools](docs/CONTROLS.md)
- [Build from source](docs/BUILDING.md)
- [Changes in this version](docs/CHANGES.md)

## Credits and license

Rowell maintains this fork and its control/UI, save-management and build changes.
The underlying port comes from **krisztsa**, **masterd2003**, **Kiridor123** and
the other contributors listed in [CREDITS.md](CREDITS.md).

The inherited [GNU GPL v3 license](LICENSE), the native server component's
[AGPL notice](LICENSES/AGPL-3.0-or-later.txt) and third-party notices are preserved.
Monster Hunter Tri and its game content belong to Capcom and the respective
rights holders. This is an unofficial fan project.

## Downloads

[**Open Downloads / Releases**](https://github.com/Dling11/monster_hunter_tri_emulator/releases)

This new repository has no published downloads yet. When a release is available,
choose **MH3-Rowell-Dev-Android.zip** for Android and install the APK inside it.
Windows packages are separate. GitHub's **Code > Download ZIP** downloads source
code, not an installable application. Legacy artifact filenames retain `Dev`;
the installed app is named **MH Tri - Rowell**.
