# MH Tri - Rowell 1.4-rowell.14 - gameplay stability pre-release

- Hammer spin finishers retain their attack-input window at higher frame rates;
  the combo list is installed once instead of clearing buffered input repeatedly.
- Lance guard advance can chain into shield bash while forward remains held.
- Animation-event history is keyed by the full actor address. Actors no longer
  collide in a 64-slot history table and replay or lose projectile/item events.
- In-place material changes invalidate the live renderer snapshot, including
  Agnaktor's glowing and cooling lava armour.
- Releasing a monster grab restores camera roll, field of view and the player's
  selected camera-distance row without restoring a stale bound-camera pose.
- Android requests a 1024-frame playback buffer for AAudio/Bluetooth scheduling
  jitter. Existing serialized audio recovery and the Windows buffer are retained.
- Synthetic motion-window regressions cover colliding actor addresses, shared
  same-step consumers, 30/60/90/120 FPS, varying steps and command-window bounds.

Adapted from Kiridor123's source at `a2a131df`; see
[integration details](UPSTREAM_GAMEPLAY.md). Fractional motion/hit-stop timing,
existing UI, saves, solo-speed limits, both networking fixes and app identity are
preserved. Both builds and automated checks passed; physical gameplay testing of
this release remains pending.

The earlier queued archive-selection checks are included: ZIP/RAR/7z selections
explain extraction, incomplete headers/transfers are rejected, and failed copies
retain the existing game image. Optional offline cutscene skipping and bounded
session-only telemetry remain experimental and disabled by default.

# MH Tri - Rowell 1.4-rowell.13-netfix2 - Android City connection fix

- Fixed an incomplete login reply causing City error 11612 on a Samsung Galaxy
  A31 running Android 12. Android TCP send buffers now have a 32 KiB minimum;
  Linux packet overhead could exhaust the game's original 4 KiB setting.
- Clear the pending-connect state after TCP completion. Keep genuine connection
  failures and TLS retry results intact. UDP buffer settings are unchanged.
- Retain the earlier POCO online-stutter fix and existing app features.
- Same package, signing key, save format, controls and MH3_Rowell_Dev folder.
  Save inside Tri and install over the existing app; a reset is not required.
- The functional fix entered Loc Lac on the A31 and passed 15 targeted native
  checks. The regular APK was rebuilt without private diagnostic logging.
  A full online quest and an in-place phone test of this clean APK are pending.
- Android ARM64 only. Windows remains 1.4-rowell.13; Low Spec remains held.

## MH Tri - Rowell 1.4-rowell.13-netfix1 — Android online-stutter fix

- Fixed repeated online freezes caused by Android network-interface enumeration
  blocking the game's main thread. Interface details now refresh in a background
  worker; gameplay reads a cached snapshot.
- Kept the last usable snapshot if a refresh fails. The initial route is detected
  without waiting for the full interface scan.
- Confirmed on a POCO F5 running Android 15: two native stack samples showed
  roughly 1.5-second waits before the fix. A 32-second online-city recording after
  the fix had no frame gaps over 80 ms, and the player confirmed improvement.
- Package, signing key, controls, character-save format and MH3_Rowell_Dev folder
  remain compatible. Save inside Tri, then install over the existing Rowell app.
- Android ARM64 only. Windows remains on 1.4-rowell.13. Performance on other
  devices and extended quests has not been established by this test.

## 1.4-rowell.13 pre-release

- Android creates imports, exports and saves folders after storage access is
  granted, before selecting or preparing the game image.
- Windows creates the same transfer folders at startup. Existing files and
  folders are preserved.
- For legacy-save migration, copy the complete exported MH3-RMHP08 folder into
  MH3_Rowell_Dev/imports, then choose it under Saves > Restore / import > Other saves.
- Includes the .12 controls, settings and stability changes below. Package,
  signing identity and save-data folder are unchanged.

## Version .12 stability changes

- Integrated selected stable Android-upstream resource guards for equipment
  transitions: absent nodes/animations produce an empty result. Bounds and null
  outputs are checked before reads; malformed alignment still reports a failure.
- Added safe missing-node consumers and a deterministic skinning fallback.
  The first rejected lookups are recorded in the bounded log for diagnosis.
- Corrected particle-lighting order using upstream's collected-effect pass.
- Android GPU purge keeps a model registered until actual retirement and checks
  demand again on the graphics thread, preventing reuse/retirement races.
- Audio playback can reopen a stalled stream after foreground return, with a
  short fade-in. Recovery and shutdown are serialized and the worker is joined.
- Android slow frames retain up to 100 ms of simulation time; loading, long
  pauses and FPS changes reset smoothing. Existing solo-speed limits remain.
- Added equipment-boundary and audio-recovery runtime checks, and isolated frame
  pacing tests. POCO F5 Village loading and Gear-menu use are confirmed; exact
  Rathian/Barroth reproduction and wider phone testing remain pending.

These are selected adaptations from upstream v1.5 / `12eb4b75`; see the credits.
Cutscene skipping, Android IME changes, experimental collision/physics changes,
new renderer options and telemetry are not included in this stability batch.

## Development build .11, included in .12

- Android activity/game logs retain a current file and one previous copy, each
  capped at 2 MiB. Crash logs use 256 KiB per file with one previous copy.
- Existing oversized activity/crash logs keep their newest tail on upgrade.
  The emergency Android Downloads fallback discards oversized legacy contents
  to preserve the newest fault without unbounded storage use.
- Windows' app-managed crash log uses the same 256 KiB retention. Logging errors
  do not stop gameplay. This does not cap externally captured console/build logs.
- Log retention only edits diagnostic files; character saves, game data, settings
  and backups are unchanged. No upstream equipment fix is included in this change.

## Development build .10, included in .12

- Fixed a Windows fullscreen/aspect-ratio fatal error by queuing render-target
  changes through the existing graphics-owner task queue. Resolution changes use
  the same queue. Fullscreen keeps the selected picture shape.
- One PC settings menu on F2. The legacy F1 panel and title hint are retired;
  F1 is available for keyboard bindings. Existing character/controller saves stay.
- PC renderer choice moves into Graphics with a restart notice; useful folder
  shortcuts move into System. Startup and the menu read the same graphics profile,
  with compatible fallback from the earlier PC/Rowell file formats.
- Windows starts with settings closed; F2 opens them when needed.
- Faded blue settings, editor and confirmation panels with restrained accents.
- Embedded Inter Regular / Semibold, fitted and vertically centered button labels,
  consistent text spacing and simple line icons beside the settings tabs.
- One prebuilt 512 KiB font texture shared by text and icons; no runtime font
  downloads or new UI framework. Menu text no longer draws duplicated shadows.
- Existing gameplay control shapes, character saves, controller/keyboard mappings,
  Android package and signing identity remain compatible.
- One saved-combo library with actual button labels, Add/Edit/Delete, and a
  shared layout-editor flow. Duplicate Controls switches are removed.
- Custom-combo deletion has Yes/No confirmation, assignment warnings, Undo,
  Cancel and persistent slot reuse. Original Wii controls are protected.
- Hidden saved combos are kept; changes require Save & play. Physical controller
  remapping remains separate and labels Wii actions and controller inputs clearly.

Windows and Android builds, native startup checks, input/save tests and 18
landscape layout captures passed locally. The .10 update also passed phone
startup and the user's available clothing-change test. The reported Rathian /
Barroth helmet case and physical DS4 testing remain unverified.

The .12 release includes the .10 and .11 development changes above.

## 1.4-rowell.9 pre-release

- Physical controller remapping on Android and Windows, separate from touch and
  PC keyboard controls, with saved bindings, conflict confirmation and reset.
- A movable, collapsible touch editor with - / + sizing, combo creation/editing,
  Undo and explicit Save & play / Cancel.
- Plain dark menus, thin-outline buttons, a simple joystick and fitted combo labels.
- A visible Resume button and PC pointer recovery for settings and touch controls.
- Existing character saves, data folders and save formats remain unchanged.

Windows build, runtime self-checks and local input/save/editor tests passed.
Android/DS4 gameplay testing is pending. Android retains its package, signing
key and data folder for installation over the existing Rowell app.

## 1.4-rowell.8

- Fixed the solo speed button being disabled in Moga Woods/free hunting and
  offline counter quests. Both now support 1x, 1.25x and 1.5x.
- City, online quests and split-screen Arena still run at normal speed.

## 1.4-rowell.7

Modified source snapshot: **25 October 2026**.

- English application settings and an English default for new game-language settings.
- Independent multi-touch, customizable combos and original Wii + / - labels.
- Backup retention capped at three after a successful verified copy.
- Readable backup creation times and explicit selection before restore/import.
- Save-tool confirmations, automatic safety backup and restart before restore.
- Windows-only keyboard remapping, automatic saving, conflict checks and reset.
- Experimental solo speed controls; City/online remains at normal speed.
- Android app-switching/restart fixes and matched SDL Java/native packaging.
- Separate app identity and storage for **MH Tri - Rowell**.

These changes build on the projects credited in [CREDITS.md](../CREDITS.md).
Device performance and experimental speed behavior still need player testing.
