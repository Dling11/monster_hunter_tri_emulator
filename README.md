# MH Tri - Rowell

Maintained by **Rowell ([Dling11](https://github.com/Dling11))**. A modified
Monster Hunter Tri port for **Android and Windows**, based on OceanTriRecomp.
It uses static recompilation; despite this repository's name, it is not a
general-purpose Wii emulator. Supply your own European/PAL **RMHP08** game image.

## Downloads

**Downloads temporarily paused. Source code and installation guides remain available.**

| Platform | Version | Download |
| --- | --- | --- |
| Android phone | 1.4-rowell.13 | Temporarily unavailable |
| Windows PC | 1.4-rowell.13 | Temporarily unavailable |

Version 1.4-rowell.13 creates imports, exports and saves folders automatically.
It includes faded-blue settings, a unified combo editor, capped logs
and selected upstream stability fixes for equipment resources, particle lighting,
audio recovery and Android frame pacing. Windows uses one settings menu on **F2**.
Controller remapping and the offline-quest speed options are retained.

Windows startup and local regression checks passed. The .12 Android baseline loaded
the Village and Gear menu normally on a POCO F5.
The .13 rebuild has not yet had a new phone gameplay test. Exact Rathian/Barroth
equipment cases, physical DS4 testing and wider device testing remain pending.

**Updating Android:** save inside Tri, then install the APK over your existing Rowell app.
**Keep the app installed and keep its MH3_Rowell_Dev folder** to retain progress and game data.
If you download the ZIP, extract it first.

**Windows:** extract the whole ZIP, then run **START_ROWELL.cmd**.

[Android installation](docs/ANDROID_INSTALL.md) ·
[Windows installation](docs/WINDOWS_INSTALL.md)

Supply your own PAL game image. GitHub's **Code > Download ZIP** downloads source
code; it does not include an Android APK or Windows application.

## Features

- English app settings and customizable touch controls.
- Independent multi-touch and five combo buttons, including **R + X + A**.
- Original Wii **+ / -** labels, adjustable layout, size and opacity.
- Thin-outline touch buttons, fitted combo labels and a simple joystick.
- Faded-blue settings with embedded Inter text and compact icons.
- A movable, collapsible editor with - / + sizing, Add combo, Undo and Save & play.
- Physical controller remapping on Android and PC; touch and keyboard bindings stay separate.
- Three retained save backups, readable timestamps and a selectable restore list.
- Yes/No save-tool prompts; a confirmed restore backs up progress and restarts.
- Experimental **1x / 1.25x / 1.5x** speed in solo play; City/online stays at 1x.
- Windows keyboard remapping with automatic saving and reset defaults.
  The keyboard editor is excluded from Android.
- Automatic retention for Android activity logs and captured crash reports.

## Get started

Binary downloads are currently paused. These instructions remain available for existing downloads and source builds.

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
- [Windows installation](docs/WINDOWS_INSTALL.md)
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
