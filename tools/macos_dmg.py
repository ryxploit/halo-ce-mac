#!/usr/bin/env python3
"""Make the macOS disk image from a built app (``ninja macos_dmg``).

    python3 tools/macos_dmg.py ["build/macos/Halo CE.app"] [output.dmg]

The image's volume is "Halo CE" and holds the app and a link to
/Applications, to drag it to. The script checks the app first (bundle layout,
arm64 executable, the embedded SDL3, a valid signature) and fails with the
reason if it is missing or incomplete; it then mounts the image it made,
read-only and out of sight, and checks what it holds.

It needs no administrator rights and changes nothing outside build/. It
signs and notarizes nothing: the app inside is signed ad hoc, as
tools/macos_bundle.py leaves it, which macOS only runs after the player
allows it. Signing with a Developer ID and notarizing are separate steps,
with the owner's own credentials (docs/macos-release.md).
"""

import plistlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_APP = ROOT / "build/macos/Halo CE.app"
VOLUME_NAME = "Halo CE"


def run(*arguments: str, check: bool = True) -> subprocess.CompletedProcess:
    result = subprocess.run(list(arguments), capture_output=True, text=True)
    if check and result.returncode:
        raise SystemExit(f"{arguments[0]} failed ({' '.join(arguments[1:])}):\n{result.stderr.strip()}")
    return result


def check_app(app: Path) -> dict:
    """the app's Info.plist, or exit with what is wrong"""
    if not app.is_dir() or app.suffix != ".app":
        raise SystemExit(f"no app at {app}: build it first (ninja macos_app)")
    plist_path = app / "Contents/Info.plist"
    if not plist_path.is_file():
        raise SystemExit(f"{app} is incomplete: no Contents/Info.plist")
    with open(plist_path, "rb") as source:
        information = plistlib.load(source)
    executable = app / "Contents/MacOS" / information.get("CFBundleExecutable", "")
    if not information.get("CFBundleExecutable") or not executable.is_file():
        raise SystemExit(f"{app} is incomplete: its executable {executable.name or '(none)'} is missing")
    architectures = run("lipo", "-archs", str(executable)).stdout.split()
    if "arm64" not in architectures:
        raise SystemExit(f"{executable} is not an arm64 executable ({' '.join(architectures)})")
    libraries = run("otool", "-L", str(executable)).stdout
    for line in libraries.splitlines()[1:]:
        path = line.strip().split(" (")[0]
        if path.startswith("@rpath/"):
            if not (app / "Contents/Frameworks" / path[len("@rpath/"):]).is_file():
                raise SystemExit(f"{app} is incomplete: {path} is not in Contents/Frameworks")
        elif not path.startswith(("/System/", "/usr/lib/")):
            raise SystemExit(f"{executable} links {path}, outside the app and the system")
    if not (app / "Contents/Resources/AppIcon.icns").is_file():
        raise SystemExit(f"{app} is incomplete: no Contents/Resources/AppIcon.icns")
    verify = run("codesign", "--verify", "--strict", "--deep", str(app), check=False)
    if verify.returncode:
        raise SystemExit(f"{app}'s signature is not valid:\n{verify.stderr.strip()}")
    return information


def check_image(image: Path, app_name: str) -> None:
    """mounts the image read-only where no Finder window opens, and checks it"""
    with tempfile.TemporaryDirectory(prefix="halo-dmg-") as mount_point:
        run("hdiutil", "attach", str(image), "-readonly", "-nobrowse", "-noverify", "-mountpoint", mount_point)
        try:
            mounted = Path(mount_point)
            applications = mounted / "Applications"
            if not (mounted / app_name).is_dir():
                raise SystemExit(f"the image has no {app_name}")
            if not applications.is_symlink() or str(applications.readlink()) != "/Applications":
                raise SystemExit("the image's Applications is not a link to /Applications")
            verify = run("codesign", "--verify", "--strict", "--deep", str(mounted / app_name), check=False)
            if verify.returncode:
                raise SystemExit(f"the app in the image fails its signature check:\n{verify.stderr.strip()}")
        finally:
            run("hdiutil", "detach", mount_point, check=False)


def main() -> None:
    if len(sys.argv) > 3:
        raise SystemExit(__doc__)
    app = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_APP
    information = check_app(app)
    version = information.get("CFBundleShortVersionString", "0")
    output = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / f"build/macos/Halo-CE-macOS-{version}-arm64.dmg"
    if not shutil.which("hdiutil"):
        raise SystemExit("hdiutil (macOS) is needed to make the image")

    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()
    with tempfile.TemporaryDirectory(prefix="halo-dmg-stage-") as stage:
        staged = Path(stage)
        # ditto keeps the bundle's signature, links and extended attributes
        run("ditto", str(app), str(staged / app.name))
        (staged / "Applications").symlink_to("/Applications")
        run("hdiutil", "create", "-volname", VOLUME_NAME, "-srcfolder", str(staged), "-fs", "HFS+",
            "-format", "UDZO", "-ov", str(output))
    run("hdiutil", "verify", str(output))
    check_image(output, app.name)
    size = output.stat().st_size / (1024 * 1024)
    print(f"made {output} ({size:.1f} MB, volume \"{VOLUME_NAME}\": {app.name} and Applications)")
    print("the app is signed ad hoc only: not signed with a Developer ID and not notarized")


if __name__ == "__main__":
    main()
