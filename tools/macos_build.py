"""Ninja rules for the macOS port (docs/macos-build.md).

The game runs as the Android port's ILP32 guest inside a 4 GB arena of a
native arm64 process (docs/macos-port-audit.md):

- macos_guest: the guest image (tools/macos_guest_build.py);
- macos_host: the native host with the image embedded (tools/macos_host_build.py);
- macos_app: build/macos/Halo CE.app (tools/macos_bundle.py);
- macos_dmg: the disk image (tools/macos_dmg.py);
- macos_spike, macos_lowmem_probe, macos_arena_probe, macos_hvf_probe: the
  feasibility spike and probes the design was decided with.

Nothing is downloaded or installed here. SDL3 must be visible to pkg-config
as sdl3, ld.lld must be installed, and tools/macos_fetch.py must have been
run; a missing dependency gives a target that says so.
"""

import shutil
import subprocess
import sys
import re
from pathlib import Path
from typing import Any, List, Optional

from .macos_guest_build import generate_macos_guest_build, guest_rules, missing_dependencies
from .macos_host_build import generate_macos_host_build
from .ninja_syntax import Writer

PORT_DIR = Path("port/macos")
BUILD_DIR = Path("build/macos")
APP_DIR = BUILD_DIR / "Halo CE.app"
BUNDLE_TOOL = Path("tools/macos_bundle.py")
DMG_TOOL = Path("tools/macos_dmg.py")
ICON = PORT_DIR / "AppIcon.icns"
INFO_PLIST = PORT_DIR / "Info.plist"
SOURCE = PORT_DIR / "src/macos_spike.c"
SPIKE_DIR = PORT_DIR / "spike"
LOWMEM_PROBE_SOURCE = SPIKE_DIR / "lowmem_probe.c"
HVF_PROBE_SOURCE = SPIKE_DIR / "hvf_probe.c"
HVF_GUEST_SOURCE = SPIKE_DIR / "hvf_guest.c"
HYPERVISOR_ENTITLEMENTS = PORT_DIR / "hypervisor.entitlements"
GUEST_BLOB_TOOL = Path("tools/macos_guest_blob.py")
# the ILP32 target of the Android guest (tools/android_build.py)
GUEST_TARGET = "arm64_32-apple-watchos"


ARENA_DIR = BUILD_DIR / "arena"


def macos_configure_inputs() -> List[Path]:
    return [Path(__file__), Path("tools/macos_guest_build.py"), Path("tools/macos_host_build.py"),
            Path("tools/macos_fetch.py"), SOURCE, INFO_PLIST,
            PORT_DIR / "guest"]


def _generate_arena_probe(n: Writer) -> None:
    """The arena probe (port/macos/spike/arena_probe.c): a small guest built
    with the game guest's rules, run in-process by a minimal host."""
    guest_object = ARENA_DIR / "arena_guest.o"
    imports_s = ARENA_DIR / "imports.s"
    imports_object = ARENA_DIR / "imports.o"
    image = ARENA_DIR / "arena_guest.elf"
    embed_dir = ARENA_DIR / "embed"
    probe = BUILD_DIR / "arena_probe"
    n.build(
        outputs=guest_object,
        rule="macos_guest_cc",
        inputs=SPIKE_DIR / "arena_guest.c",
        implicit=[Path("tools/macos_asm_lift.py"), Path("tools/android_asm_convert.py")],
        variables={"cflags": (f"--target={GUEST_TARGET} -Wno-incompatible-sysroot -mcpu=cortex-a53 "
                              "-ffixed-x15 -ffixed-x27 -mllvm -aarch64-enable-compress-jump-tables=false "
                              "-fno-stack-protector -ffreestanding -O2 -std=c11 -Wall -Wextra -Werror")},
    )
    n.build(outputs=imports_s, rule="macos_guest_imports", inputs=SPIKE_DIR / "arena_imports.list",
            implicit=[Path("tools/android_imports.py")])
    n.build(outputs=imports_object, rule="macos_guest_as", inputs=imports_s)
    n.build(outputs=image, rule="macos_guest_link", inputs=[guest_object, imports_object],
            implicit=[PORT_DIR / "guest.ld"], variables={"libraries": ""})
    n.build(outputs=[embed_dir / "guest_image.h", embed_dir / "guest_image.S"], rule="macos_guest_embed",
            inputs=image, implicit=[Path("tools/macos_embed_guest.py")], variables={"embed_dir": str(embed_dir)})
    n.rule(
        name="macos_arena_link",
        command="$macos_cc -arch arm64 -O2 -Wall -Wextra -Werror -I$embed_dir -o $out $in",
        description="MACOS ARENA PROBE $out",
    )
    n.build(outputs=probe, rule="macos_arena_link",
            inputs=[SPIKE_DIR / "arena_probe.c", PORT_DIR / "host" / "guest_call.S", embed_dir / "guest_image.S"],
            implicit=[embed_dir / "guest_image.h"], variables={"embed_dir": str(embed_dir)})
    n.build(outputs="macos_arena_probe", rule="phony", inputs=probe)


def _generate_probes(n: Writer) -> None:
    """The feasibility probes of docs/macos-port-audit.md; they need only clang."""
    n.rule(
        name="macos_probe_link",
        command="$macos_cc -arch arm64 -std=c11 -O2 -Wall -Wextra -Werror -o $out $in",
        description="MACOS PROBE $out",
    )
    n.build(outputs=BUILD_DIR / "lowmem_probe", rule="macos_probe_link", inputs=LOWMEM_PROBE_SOURCE)
    n.build(outputs="macos_lowmem_probe", rule="phony", inputs=BUILD_DIR / "lowmem_probe")

    guest_object = BUILD_DIR / "obj/hvf_guest.o"
    guest_header = BUILD_DIR / "gen/hvf_guest_blob.h"
    probe = BUILD_DIR / "hvf_probe"
    n.rule(
        name="macos_hvf_guest_cc",
        command=(f"$macos_cc --target={GUEST_TARGET} -Wno-incompatible-sysroot -std=c11 -O2 -Wall -Wextra -Werror "
                 "-ffreestanding -fno-stack-protector -mno-outline-atomics -c $in -o $out"),
        description="MACOS HVF GUEST CC $out",
    )
    n.rule(
        name="macos_hvf_guest_blob",
        command=f"{sys.executable} {GUEST_BLOB_TOOL} $in $out hvf_guest",
        description="MACOS GUEST BLOB $out",
    )
    n.rule(
        name="macos_hvf_link",
        command=("$macos_cc -arch arm64 -std=c11 -O2 -Wall -Wextra -Werror -I$gen_dir -o $out $in "
                 "-framework Hypervisor && codesign --force --sign - --entitlements $entitlements $out"),
        description="MACOS HVF PROBE $out",
    )
    n.build(outputs=guest_object, rule="macos_hvf_guest_cc", inputs=HVF_GUEST_SOURCE)
    n.build(outputs=guest_header, rule="macos_hvf_guest_blob", inputs=guest_object, implicit=[GUEST_BLOB_TOOL])
    n.build(
        outputs=probe,
        rule="macos_hvf_link",
        inputs=HVF_PROBE_SOURCE,
        implicit=[guest_header, HYPERVISOR_ENTITLEMENTS],
        variables={"gen_dir": str(guest_header.parent), "entitlements": str(HYPERVISOR_ENTITLEMENTS)},
    )
    n.build(outputs="macos_hvf_probe", rule="phony", inputs=probe)


def _command_output(command: List[str]) -> Optional[str]:
    try:
        result = subprocess.run(command, capture_output=True, check=False, text=True)
    except OSError:
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def _sdl_flags() -> Optional[tuple[str, str]]:
    pkg_config = shutil.which("pkg-config")
    if not pkg_config:
        return None
    cflags = _command_output([pkg_config, "--cflags", "sdl3"])
    libs = _command_output([pkg_config, "--libs", "sdl3"])
    return (cflags, libs) if cflags is not None and libs is not None else None


def generate_macos_build(n: Writer, sln: Any) -> None:
    """Emit the macOS host-spike target, or a target with a clear dependency error."""
    if sys.platform != "darwin":
        return

    cc = getattr(sln, "macos_cc", None) or "clang"
    bundle_id = getattr(sln, "macos_bundle_id", None) or "org.opencommunityedition.haloce.macos"
    if not re.fullmatch(r"[A-Za-z0-9.-]+", bundle_id):
        raise ValueError("--macos-bundle-id may contain only letters, digits, dots and hyphens")
    sdl = _sdl_flags()
    n.comment("macOS arm64 host-platform spike (not the Halo engine)")
    n.variable("macos_cc", cc)
    _generate_probes(n)
    guest_rules(n, sln)
    if [p for p in missing_dependencies(sln) if "ld.lld" in p]:
        n.rule(name="macos_arena_missing", command="echo 'the arena probe needs ld.lld (brew install lld)' >&2; exit 1",
               description="MACOS LLD REQUIRED")
        n.build(outputs="macos_arena_probe", rule="macos_arena_missing")
    else:
        _generate_arena_probe(n)
    guest = generate_macos_guest_build(n, sln)

    if not sdl:
        n.rule(
            name="macos_missing_sdl3",
            command=("echo 'macOS spike needs SDL3 discoverable as pkg-config package sdl3; "
                     "install/provide it, then rerun python configure.py.' >&2; exit 1"),
            description="MACOS SDL3 REQUIRED",
        )
        n.build(outputs="macos_spike", rule="macos_missing_sdl3")
        return

    sdl_cflags, libs = sdl
    n.variable("macos_sdl_cflags", sdl_cflags)
    # the window spike of the first milestone (port/macos/src/macos_spike.c):
    # SDL3's initialisation, a window and shutdown, without the game
    object_file = BUILD_DIR / "obj/macos_spike.o"
    spike = BUILD_DIR / "macos_spike"
    n.rule(
        name="macos_cc",
        command="$macos_cc -arch arm64 $macos_sdl_cflags $cflags -MMD -MF $out.d -c $in -o $out",
        description="MACOS CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    n.rule(
        name="macos_spike_link",
        command="$macos_cc -arch arm64 -o $out $in $libs",
        description="MACOS LINK $out",
    )
    n.build(outputs=object_file, rule="macos_cc", inputs=SOURCE, variables={"cflags": "-std=c11 -Wall -Wextra -Werror"})
    n.build(outputs=spike, rule="macos_spike_link", inputs=object_file, variables={"libs": libs})
    n.build(outputs="macos_spike", rule="phony", inputs=spike)

    if not guest:
        return
    # the app: build/macos/Halo CE.app (tools/macos_bundle.py)
    n.rule(
        name="macos_bundle",
        command=f"$python {BUNDLE_TOOL} $in {INFO_PLIST} {ICON} {bundle_id} '{APP_DIR}'",
        description="MACOS APP $out",
    )
    host = generate_macos_host_build(n, guest, sdl_cflags, libs)
    n.build(outputs=APP_DIR / "Contents" / "MacOS" / host.name, rule="macos_bundle", inputs=host,
            implicit=[BUNDLE_TOOL, INFO_PLIST, ICON])
    n.build(outputs="macos_app", rule="phony", inputs=APP_DIR / "Contents" / "MacOS" / host.name)

    # the disk image (tools/macos_dmg.py), from the app
    import plistlib

    with open(INFO_PLIST, "rb") as source:
        version = plistlib.load(source)["CFBundleShortVersionString"]
    dmg = BUILD_DIR / f"Halo-CE-macOS-{version}-arm64.dmg"
    n.rule(
        name="macos_dmg",
        command=f"$python {DMG_TOOL} '{APP_DIR}' $out",
        description="MACOS DMG $out",
    )
    n.build(outputs=dmg, rule="macos_dmg", inputs=APP_DIR / "Contents" / "MacOS" / host.name, implicit=[DMG_TOOL])
    n.build(outputs="macos_dmg", rule="phony", inputs=dmg)
