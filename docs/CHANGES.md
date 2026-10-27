# MH Tri - Rowell 1.4-rowell.9 pre-release

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
