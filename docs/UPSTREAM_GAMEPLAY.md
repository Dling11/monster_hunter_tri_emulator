# Upstream gameplay fixes

This fork selectively adapts the Android upstream's v1.6 work, reviewed at
[`a2a131df1d9209393d032ed91f34d9bb3edc397f`](https://github.com/Kiridor123/OceanTriRecomp-Android/commit/a2a131df1d9209393d032ed91f34d9bb3edc397f).
The original author and contributors retain their credits and licences.

| Area | Adaptation |
| --- | --- |
| Hammer | Preserve the spin input flag, install its finisher command list once, and accept crossed single-frame command windows. |
| Lance | Propagate the classic-controller guard-advance command bit to the shield-bash path. |
| Throwables and animation events | Keep independent ahead-window history for each full actor address. Retain this fork's existing step tiling and fractional motion/hit-stop counters. |
| Agnaktor materials | Increment the live-material generation after a validated in-place setter so cached GPU material snapshots are refreshed. |
| Grab camera | Restore upright camera presentation and the player's camera distance after a bind ends. Keep each platform's existing pitch behaviour. |
| Android audio | Request 1024 sample frames while keeping the existing mixer chunk size, null-sink clock and serialized recovery/shutdown. |

These patches do not replace the custom controls, save formats, package identity
or networking implementation. The POCO interface-cache fix and Galaxy A31 TCP
buffer fix must remain when taking later upstream changes.

The upstream release notes and tag do not establish the exact contents of its
later-uploaded APK. This integration uses the pinned source above, not an APK
repack. The source's Swappy glue is disabled, so this update does not enable
Swappy or claim a performance improvement from it.

Optional floating damage numbers, broader collision/physics changes, alternate
ROM-selection behaviour and upstream menu replacements are outside this gameplay
stability batch. They need separate compatibility and presentation review.

Run the runtime's explicit `--self-test` for the animation-window regressions,
then the existing controls, networking and audio-recovery checks. Synthetic
checks do not replace physical gameplay tests of hammer/lance combos, throwing,
Agnaktor's materials, grab release, Bluetooth audio or online quests.
