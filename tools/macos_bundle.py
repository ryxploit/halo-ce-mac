#!/usr/bin/env python3
"""Assemble the macOS app bundle, build/macos/Halo CE.app (``ninja macos_app``).

    Halo CE.app/Contents/
        Info.plist          port/macos/Info.plist, with the bundle identifier
                            and the minimum macOS version of the build
        MacOS/halo-ce       the host (tools/macos_host_build.py), with the guest
                            image inside it
        Frameworks/         SDL3, found through @rpath
        Resources/AppIcon.icns
        Resources/Licenses/ the notices of the code built into the app

The SDL3 the host was linked with is copied in and both are relinked to find
it relative to the executable, so the bundle depends on nothing outside the
system. The bundle is then signed ad hoc (no identity): enough to run on this
Mac, not a Developer ID signature (docs/macos-release.md). No game data goes
into the bundle.

Usage: macos_bundle.py executable Info.plist icon bundle_id output.app
"""

import plistlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LICENSES = {
    "Halo-CE-port-CC0.md": ROOT / "LICENSE.md",
    "musl-COPYRIGHT.txt": ROOT / "build/third_party/musl-1.2.5/COPYRIGHT",
    "musl-math-COPYRIGHT.txt": ROOT / "port/third_party/musl-math/COPYRIGHT",
    "zlib-LICENSE.txt": ROOT / "port/third_party/zlib/LICENSE",
    "expat-COPYING.txt": ROOT / "port/third_party/expat/COPYING",
    "kcp-LICENSE.txt": ROOT / "port/third_party/kcp/LICENSE",
    "monocypher-LICENCE.md": ROOT / "port/third_party/monocypher/LICENCE.md",
    "tomlc17-LICENSE.txt": ROOT / "port/third_party/tomlc17/LICENSE",
    "miniupnpc-LICENSE.txt": ROOT / "port/third_party/miniupnpc/LICENSE",
    "extract-xiso-LICENSE.txt": ROOT / "port/third_party/extract-xiso/LICENSE.TXT",
    "smaa-LICENSE.txt": ROOT / "port/third_party/smaa/LICENSE",
    "stb-LICENSE.txt": ROOT / "port/third_party/stb/LICENSE",
    "Overpass-OFL.txt": ROOT / "port/assets/fonts/Overpass-OFL.txt",
    "Newtown-LICENSE.txt": ROOT / "port/assets/fonts/Newtown-LICENSE.txt",
}


def run(*arguments: str) -> str:
    result = subprocess.run(list(arguments), capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f"{' '.join(arguments)} failed:\n{result.stderr}")
    return result.stdout


def linked_sdl(executable: Path) -> Path:
    for line in run("otool", "-L", str(executable)).splitlines()[1:]:
        path = line.strip().split(" (")[0]
        if "libSDL3" in path and not path.startswith("@"):
            return Path(path)
    raise SystemExit(f"{executable} is not linked with an SDL3 library outside the bundle")


def minimum_system_version(executable: Path) -> str:
    match = re.search(r"cmd LC_BUILD_VERSION.*?minos (\S+)", run("otool", "-l", str(executable)), re.S)
    if not match:
        raise SystemExit(f"{executable}: no LC_BUILD_VERSION")
    return match.group(1)


def main() -> None:
    if len(sys.argv) != 6:
        raise SystemExit(__doc__)
    executable, plist, icon, bundle_id, output = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]), \
        sys.argv[4], Path(sys.argv[5])
    if not re.fullmatch(r"[A-Za-z0-9.-]+", bundle_id):
        raise SystemExit("the bundle identifier may contain only letters, digits, dots and hyphens")
    for required in (executable, plist, icon):
        if not required.is_file():
            raise SystemExit(f"missing {required}")
    if output.suffix != ".app" or output.resolve().parent != (ROOT / "build/macos").resolve():
        raise SystemExit("the bundle must be build/macos/<name>.app")

    if output.exists():
        shutil.rmtree(output)
    contents = output / "Contents"
    (contents / "MacOS").mkdir(parents=True)
    (contents / "Frameworks").mkdir()
    (contents / "Resources" / "Licenses").mkdir(parents=True)

    target = contents / "MacOS" / executable.name
    shutil.copy2(executable, target)
    sdl = linked_sdl(target)
    framework_sdl = contents / "Frameworks" / sdl.name
    shutil.copy2(sdl.resolve(), framework_sdl)
    framework_sdl.chmod(0o755)
    run("install_name_tool", "-id", f"@rpath/{sdl.name}", str(framework_sdl))
    run("install_name_tool", "-change", str(sdl), f"@rpath/{sdl.name}", str(target))
    # (the developer's library folders, which pkg-config's flags add, are
    # not the bundle's business)
    for rpath in re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (\S+)", run("otool", "-l", str(target))):
        if not rpath.startswith("@"):
            run("install_name_tool", "-delete_rpath", rpath, str(target))
    run("install_name_tool", "-add_rpath", "@executable_path/../Frameworks", str(target))

    with open(plist, "rb") as source:
        information = plistlib.load(source)
    information["CFBundleIdentifier"] = bundle_id
    information["CFBundleExecutable"] = executable.name
    information["LSMinimumSystemVersion"] = minimum_system_version(target)
    with open(contents / "Info.plist", "wb") as destination:
        plistlib.dump(information, destination)
    shutil.copy2(icon, contents / "Resources" / "AppIcon.icns")
    # internet play's public MQTT brokers: the host copies this beside
    # config.toml at each start, as the Android app does (host_main.c)
    brokers = ROOT / "port/assets/network/brokers.txt"
    if not brokers.is_file():
        raise SystemExit(f"missing {brokers}: internet play needs the brokers' list")
    shutil.copy2(brokers, contents / "Resources" / "brokers.txt")

    sdl_license = sdl.resolve().parent.parent / "LICENSE.txt"
    licenses = dict(LICENSES, **({"SDL3-LICENSE.txt": sdl_license} if sdl_license.is_file() else {}))
    for name, source in licenses.items():
        if not source.is_file():
            raise SystemExit(f"missing the notice {source}")
        shutil.copy2(source, contents / "Resources" / "Licenses" / name)

    # inside out: the library, then the bundle (whose seal covers it)
    run("codesign", "--force", "--sign", "-", str(framework_sdl))
    run("codesign", "--force", "--sign", "-", str(output))
    run("codesign", "--verify", "--strict", "--deep", str(output))
    print(f"assembled {output} ({bundle_id}, macOS {information['LSMinimumSystemVersion']}+, "
          "signed ad hoc)")


if __name__ == "__main__":
    main()
