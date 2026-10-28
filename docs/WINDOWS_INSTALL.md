# Install on Windows

Requires **Windows 10/11 x64**, a compatible graphics driver and your own
European/PAL Monster Hunter Tri **RMHP08** image (RVZ, ISO or WIA).
Windows uses a native **EXE**, not an Android APK or BlueStacks.

## Install and start

1. Download **MH3-Rowell-Windows-x64.zip** from
   [Releases](https://github.com/Dling11/monster_hunter_tri_emulator/releases).
2. Extract the entire ZIP into a writable folder, for example
   **D:\Games\MH-Tri-Rowell**. Keep the EXE and DLLs together.
3. Put one PAL RVZ/ISO/WIA image beside **MH3_Rowell_Dev.exe** for automatic
   detection, or select the image in the first-launch window. Selecting an
   image elsewhere copies it beside the EXE; allow space for preparation too.
4. Run **START_ROWELL.cmd**, or open **MH3_Rowell_Dev.exe** directly.
5. Let preparation finish, then create a character or continue your Rowell save.

Settings stay closed at startup. Press **F2** whenever you need them.

If Windows reports a missing **MSVCP140** or **VCRUNTIME140** DLL, install the
[Microsoft Visual C++ x64 runtime](https://aka.ms/vc14/vc_redist.x64.exe)
from Microsoft. Docker and developer tools are not needed to play this package.

The ZIP contains no game image, extracted game assets or saved progress.
GitHub's **Code > Download ZIP** is the source download, not this playable ZIP.

## Controls

- **W/A/S/D:** move. **I/J/K/L:** camera.
- **F / X / Space / E:** A / X / B / Y. **Shift:** R.
- **Enter / Tab:** Wii + / -.
- **F2:** app settings. **Escape:** return to the game.
- **Graphics > PC graphics renderer:** Vulkan (ANGLE) or OpenGL ES; restart to apply.
- **System > Open data folder / Open app folder:** open the matching PC folders.
- **Controls > Keyboard bindings:** remap gameplay inputs and combo shortcuts.
  Changes save automatically; Reset restores defaults.

See [all controls and save tools](CONTROLS.md).

## Updates and saves

Save inside Tri and close the game before updating. Keep your existing
**MH3_Rowell_Dev** folder and **UserData** folder when replacing the program
files. They hold character saves and settings; the download supplies neither.
The launcher uses MH3_Rowell_Dev beside its EXE and does not import another
installation's saves automatically. To migrate progress, use the save tools
described in the controls guide.

Captured crashes are recorded in `MH3_Rowell_Dev/crash.txt`, up to 256 KiB
with one `.previous` copy. Send both soon after a crash if requested. This
retention applies only to app-managed crash logs, not console output captured
by external tools; it does not change saves, settings or game data.
