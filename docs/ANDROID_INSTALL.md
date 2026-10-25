# Install on Android

App: **MH Tri - Rowell**. Version **1.4-rowell.7 (10007)**.
Requires **ARM64 Android 10+**, OpenGL ES 3.0 and your own European/PAL
Monster Hunter Tri image, game ID **RMHP08**. Performance depends on the device.

## Install

1. Open the repository's [Releases](https://github.com/Dling11/monster_hunter_tri_emulator/releases).
   If no release is published, transfer a locally built APK or Android ZIP.
2. Extract **MH3-Rowell-Dev-Android.zip** and tap **MH3_Rowell_Dev-arm64.apk**.
   You can also install the APK directly. Allow installation from your Files
   app or browser if Android requests it.
3. Open **MH Tri - Rowell**, follow the storage-access prompt and return to it.
4. Choose your PAL **RVZ, ISO or WIA** image. Extract ZIP/7z game archives first.
   The app copies a selected image into its own folder and prepares it.
5. Let preparation finish, then create a character or continue your Rowell save.

The app creates **Internal storage/MH3_Rowell_Dev**. Your original app and its
**MH3** folder stay separate; their saves are not automatically imported.
The download contains no game image or saved progress. Docker, Android Studio
and BlueStacks are not needed on the phone.

## Updates and progress

Save inside Tri before updating. Install the new APK over the Rowell app using
the same signing key; avoid uninstalling an app whose saves you need.
The package remains **com.rowell.mh3dev** and the data folder remains
**MH3_Rowell_Dev**. A different signing key cannot update this package in place.

- **Create backup:** retain three local copies with readable creation times.
  A fourth removes the oldest after the new copy is verified. Automatic safety
  copies before restore count toward the same limit.
- **Export save:** copy the last in-game save to a transferable folder.
  Exports are independent of the three-backup limit.
- **Restore / import:** choose My backups or Other saves, select a copy,
  review it and confirm Yes. The app stages it, restarts and backs up the
  current saved progress before replacing it. Use Tri's Continue afterward.
  No/Cancel leaves saved files unchanged; unsaved play is lost on restore.

For transfers, put the entire exported folder in **MH3_Rowell_Dev/imports**
(create imports if needed), then select it under Other saves. Save tools copy
already-saved progress; they are not save states.

## Controls

**Gear > Combos** configures five combo buttons, including the default
**R + X + A**. **Controls > Edit touch layout** adjusts placement and size.
The PC keyboard editor is absent from Android.

Solo speed cycles **1x / 1.25x / 1.5x**; City/online stays at 1x. Village/solo
quests permit a 60 FPS limit, which does not guarantee the rendered frame rate.
See [controls](CONTROLS.md) for all options and speed limits.

Report the app version, phone/Android version, scene and steps when reporting
a problem. Relevant logs can help; keep personal data out of shared excerpts.

## Package verification

The ZIP includes installation/controls guides, changes, credits, license texts,
version metadata and SHA256 checksums. The `.sha256` sidecar verifies the ZIP;
`VERSION.json` records the APK hash. Windows may show a BlueStacks icon because
of its APK file association; that does not affect Android installation.
