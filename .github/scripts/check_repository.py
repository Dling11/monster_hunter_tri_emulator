#!/usr/bin/env python3
"""Check the source-only Android bundle without game data or build tools."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import re
import subprocess

root = Path(__file__).resolve().parents[2]
bundle = root / "android"
manifest = json.loads((bundle / "source.json").read_text(encoding="utf-8-sig"))


def require(ok, message):
    if not ok:
        raise SystemExit(message)


def safe_path(value):
    path = PurePosixPath(value)
    require(not path.is_absolute() and ".." not in path.parts and "\\" not in value and ":" not in value,
            f"Unsafe bundle path: {value}")
    require(not set(path.parts) & {".git", ".dev", "Assets", "generated", "playtest", "dist", "UserData"},
            f"Local or generated data in bundle: {value}")
    require(path.suffix.lower() not in {".apk", ".zip", ".rvz", ".iso", ".wia", ".keystore", ".jks"},
            f"Binary/private artifact in source bundle: {value}")
    return path


require(manifest["format"] == 1, "Unsupported bundle format")
require(manifest["baseCommit"] == "f7b47b7208bfe1febe8d50716359dc682331c9d9", "Unexpected Android baseline")
require(manifest["upstream"] == "https://github.com/Kiridor123/OceanTriRecomp-Android.git", "Unexpected Android upstream")
patch = bundle / "rowell.patch"
require(hashlib.sha256(patch.read_bytes()).hexdigest() == manifest["patchSha256"], "Patch digest mismatch")
tracked = {entry["path"] for entry in manifest["trackedFiles"]}
for entry in manifest["trackedFiles"]:
    safe_path(entry["path"])
    require(re.fullmatch(r"[0-9a-f]{64}", entry["sha256Lf"]) is not None, "Invalid source digest")
stats = subprocess.check_output(["git", "apply", "--numstat", str(patch)], cwd=root, text=True)
patched = {line.split("\t", 2)[2] for line in stats.splitlines() if line}
require(tracked == patched, "Patch files differ from the bundle manifest")
extra_paths = set()
for entry in manifest["extraFiles"]:
    relative = safe_path(entry["path"])
    payload = bundle / "extra" / relative
    require(payload.is_file(), f"Missing Android payload: {relative}")
    require(hashlib.sha256(payload.read_bytes()).hexdigest() == entry["sha256"], f"Payload digest mismatch: {relative}")
    extra_paths.add(str(relative))
require({"rowell-android.ps1", "runtime/include/touch_contacts.h", "scripts/rowell_android_package.py"} <= extra_paths,
        "Missing essential Android additions")
attributes = subprocess.check_output(["git", "check-attr", "text", "--", "android/rowell.patch",
                                     *["android/extra/" + item for item in sorted(extra_paths)]], cwd=root, text=True)
require(all(line.endswith(": unset") for line in attributes.splitlines()), "Bundle bytes can be altered by Git line-ending conversion")

docs = ["README.md", "CREDITS.md", "android/README.md", "docs/CHANGES.md",
        "docs/ANDROID_INSTALL.md", "docs/CONTROLS.md", "docs/BUILDING.md"]
for relative in docs:
    document = root / relative
    require(document.is_file(), f"Missing document: {relative}")
    for link in re.findall(r"\]\(([^)]+)\)", document.read_text(encoding="utf-8-sig")):
        target = link.split("#", 1)[0].strip("<>")
        if not target or re.match(r"[A-Za-z][A-Za-z0-9+.-]*:", target):
            continue
        require((document.parent / target).exists(), f"Broken local link in {relative}: {link}")

require((root / "LICENSE").is_file(), "Missing main license")
require((root / "LICENSES/AGPL-3.0-or-later.txt").is_file(), "Missing native-server license")
require((root / "third_party/angle/LICENSE").is_file(), "Missing ANGLE license")
ignored = subprocess.check_output(["git", "check-ignore", "--", ".local/notes/build.md",
                                  "BUILD_STATUS.md", "PHONE_TESTING.md", "logs/startup.log",
                                  "toolchain/translator/src/Translator.Core/obj/cache.dll",
                                  "android-source/.dev/rowell-dev.keystore"], cwd=root, text=True)
require(len(ignored.splitlines()) == 6, "A local report, cache or signing key could enter Git")
print(f"Source bundle and documentation passed: {len(tracked)} patched files, {len(extra_paths)} additions.")
