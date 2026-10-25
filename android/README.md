# Android source bundle

The working Android checkout is intentionally outside the parent repository's
tracked files. This directory records all Rowell changes needed to restore it:

- `source.json`: upstream URL, pinned commit and content checksums.
- `rowell.patch`: tracked source changes relative to the pinned Android commit.
- `extra/`: new build scripts, Docker recipe and touch-contact helper.

From the repository root, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\android-source.ps1 -Mode Restore
```

This clones the pinned upstream snapshot into `android-source/`, then applies
the saved changes. An existing checkout is checked before anything is replaced.
Repeated restoration accepts an already-applied matching bundle. It does not
switch an existing checkout to another commit or overwrite different additional
files.

After editing Android code, update the tracked bundle before committing:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\android-source.ps1 -Mode Export
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\android-source.ps1 -Mode Verify
```

The Android author's generated game-function patches are obtained from the
pinned upstream commit and preserved by the build. Derived shard changes are
regenerated during Android builds; they are excluded from this patch bundle.
Game images, extracted assets, saves, caches and signing keys are excluded.
The exporter rejects unexpected additional files or edits to generated game
functions rather than silently omitting them. Register intentional new source
files in its additional-file list before exporting again. Git attributes keep
the saved patch/payload bytes unchanged across Windows and Linux checkouts.

Upstream: [Kiridor123/OceanTriRecomp-Android](https://github.com/Kiridor123/OceanTriRecomp-Android),
commit `f7b47b7208bfe1febe8d50716359dc682331c9d9`.
