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


WINDOW = (100, 100, 700, 500)  # the window's bounds on screen, 600 x 400 points
APP_POSITION = (150, 200)
APPLICATIONS_POSITION = (450, 200)


def make_background(path: Path) -> None:
    """The window's picture: a title, the instruction and an arrow between the
    app and the Applications folder. Drawn here, with no game artwork."""
    from PIL import Image, ImageDraw, ImageFont

    width, height = WINDOW[2] - WINDOW[0], WINDOW[3] - WINDOW[1]
    image = Image.new("RGB", (width, height))
    pixels = image.load()
    top, bottom = (14, 22, 44), (10, 58, 74)
    for y in range(height):
        t = y / (height - 1)
        colour = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(width):
            pixels[x, y] = colour
    draw = ImageDraw.Draw(image)
    bold = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
    regular = "/System/Library/Fonts/Supplemental/Arial.ttf"
    title_font = ImageFont.truetype(bold, 34)
    text_font = ImageFont.truetype(regular, 18)
    small_font = ImageFont.truetype(regular, 13)
    draw.text((width / 2, 52), "Instala Halo CE", font=title_font, fill=(235, 242, 248), anchor="mm")
    draw.text((width / 2, 92), "Arrastra Halo CE a la carpeta Aplicaciones", font=text_font,
              fill=(150, 205, 215), anchor="mm")
    # the arrow, from the app's icon to the Applications folder
    y = APP_POSITION[1]
    draw.line([(235, y), (352, y)], fill=(120, 230, 210), width=9)
    draw.polygon([(352, y - 22), (384, y), (352, y + 22)], fill=(120, 230, 210))
    draw.text((width / 2, height - 36), "Port no oficial de la comunidad. Necesitas tu propia imagen de disco de Xbox.",
              font=small_font, fill=(120, 140, 160), anchor="mm")
    image.save(path)


def mark_custom_icon(mount: str) -> None:
    """Tells Finder that the volume has its own icon (.VolumeIcon.icns)."""
    run("SetFile", "-a", "C", mount, check=False)


def layout_window(staged: Path, app_name: str, writable: Path, icon: Path) -> bool:
    """Writes the window's layout (icon positions, size, picture and the disk's
    icon) by letting Finder do it on a writable image made at `writable`. Returns
    False if Finder does not allow it; the caller then keeps the plain layout."""
    run("hdiutil", "create", "-volname", VOLUME_NAME, "-srcfolder", str(staged), "-fs", "HFS+",
        "-format", "UDRW", "-ov", str(writable))
    attached = run("hdiutil", "attach", str(writable), "-readwrite", "-noverify", "-noautoopen", "-nobrowse")
    mount = next((line.split("\t")[-1].strip() for line in attached.stdout.splitlines() if "/Volumes/" in line), None)
    if not mount:
        return False
    script = f"""
tell application "Finder"
    tell disk "{VOLUME_NAME}"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {{{WINDOW[0]}, {WINDOW[1]}, {WINDOW[2]}, {WINDOW[3]}}}
        set viewOptions to the icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 128
        set text size of viewOptions to 14
        set background picture of viewOptions to file ".background:background.png"
        set position of item "{app_name}" of container window to {{{APP_POSITION[0]}, {APP_POSITION[1]}}}
        set position of item "Applications" of container window to {{{APPLICATIONS_POSITION[0]}, {APPLICATIONS_POSITION[1]}}}
        update without registering applications
        delay 2
        close
    end tell
end tell
"""
    laid_out = run("osascript", "-e", script, check=False).returncode == 0
    # after Finder's pass: the disk's own icon, and the flag that makes Finder show it
    shutil.copy2(icon, Path(mount) / ".VolumeIcon.icns")
    mark_custom_icon(mount)
    run("sync", check=False)
    for attempt in range(5):
        if run("hdiutil", "detach", mount, check=False).returncode == 0:
            break
        subprocess.run(["sleep", "2"], check=False)
    return laid_out

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
        (staged / ".background").mkdir()
        make_background(staged / ".background" / "background.png")
        with tempfile.TemporaryDirectory(prefix="halo-dmg-rw-") as work:
            writable = Path(work) / "layout.dmg"
            laid_out = layout_window(staged, app.name, writable, app / "Contents/Resources/AppIcon.icns")
            if not laid_out:
                print("warning: the window could not be laid out (Finder automation refused); the image has the plain layout")
                run("hdiutil", "create", "-volname", VOLUME_NAME, "-srcfolder", str(staged), "-fs", "HFS+",
                    "-format", "UDZO", "-ov", str(output))
            else:
                # the writable copy Finder laid out is the one compressed, so its layout is kept
                run("hdiutil", "convert", str(writable), "-format", "UDZO", "-imagekey", "zlib-level=9", "-ov",
                    "-o", str(output))
    run("hdiutil", "verify", str(output))
    check_image(output, app.name)
    size = output.stat().st_size / (1024 * 1024)
    print(f"made {output} ({size:.1f} MB, volume \"{VOLUME_NAME}\": {app.name} and Applications)")
    print("the app is signed ad hoc only: not signed with a Developer ID and not notarized")


if __name__ == "__main__":
    main()
