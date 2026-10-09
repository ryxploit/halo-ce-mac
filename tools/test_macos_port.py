"""Tests of the macOS port (docs/macos-build.md, "Tests").

    python3 -m pytest tools/test_macos_port.py

The arena pass is tested on its own; the rest runs what ninja built
(macos_lowmem_probe, macos_arena_probe, macos_hvf_probe, macos_app,
macos_dmg) and is skipped when it is not there. Nothing here needs game
data, except the smoke test of the game, which runs only with
HALO_MACOS_TEST_DATA set to a folder holding the maps folder extracted from
the player's own disc image (it is used read-only, through a link).
"""

import os
import plistlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/macos"
APP = BUILD / "Halo CE.app"
EXECUTABLE = APP / "Contents/MacOS/halo-ce"

sys.path.insert(0, str(ROOT / "tools"))

pytestmark = pytest.mark.skipif(sys.platform != "darwin", reason="the macOS port's tests run on macOS")


def built(path: Path) -> Path:
    if not path.exists():
        pytest.skip(f"{path.relative_to(ROOT)} is not built")
    return path


def run_game(user_root: Path, *arguments: str, environment=None, timeout=120):
    variables = dict(os.environ, HALO_MACOS_USER_ROOT=str(user_root), HALO_MACOS_NO_DIALOGS="1")
    variables.update(environment or {})
    return subprocess.run([str(built(EXECUTABLE)), *arguments], capture_output=True, text=True, env=variables,
                          timeout=timeout)


# ---------- the arena pass (tools/macos_asm_lift.py)


def lift(text: str) -> str:
    from macos_asm_lift import lift as lift_assembly

    return lift_assembly(text)


def test_lift_memory_access_goes_through_the_arena():
    assert lift("\tldr w0, [x1, #8]\n").splitlines() == [
        "\tmov w15, w1", "\torr x15, x15, x27", "\tldr w0, [x15, #8]"]


def test_lift_register_offset_keeps_its_index():
    assert lift("\tldr w0, [x1, x2, lsl #2]\n").splitlines()[-1] == "\tldr w0, [x15, x2, lsl #2]"


def test_lift_post_index_updates_the_original_base():
    lines = lift("\tldr x0, [x1], #16\n").splitlines()
    assert lines[-2:] == ["\tldr x0, [x15], #16", "\tadd x1, x1, #16"]


def test_lift_pre_index_updates_the_original_base():
    lines = lift("\tstr w0, [x3, #-4]!\n").splitlines()
    assert lines[-2:] == ["\tstr w0, [x15, #-4]!", "\tsub x3, x3, #4"]


def test_lift_leaves_stack_accesses_alone():
    assert lift("\tstp x29, x30, [sp, #-16]!\n") == "\tstp x29, x30, [sp, #-16]!\n"


def test_lift_truncates_pc_relative_addresses():
    assert lift("\tadrp x8, table\n").splitlines() == ["\tadrp x8, table", "\tmov w8, w8"]


def test_lift_indirect_branches_go_through_the_arena():
    assert lift("\tblr x8\n").splitlines() == ["\tmov w15, w8", "\torr x15, x15, x27", "\tblr x15"]


def test_lift_allows_saving_x27_with_x28():
    assert lift("\tstp x28, x27, [sp, #-80]!\n") == "\tstp x28, x27, [sp, #-80]!\n"


def test_lift_rejects_the_reserved_registers():
    from android_asm_convert import ConvertError

    with pytest.raises(ConvertError):
        lift("\tadd x15, x15, #1\n")
    with pytest.raises(ConvertError):
        lift("\tmov x27, x0\n")


def test_lift_rejects_a_register_post_index():
    from android_asm_convert import ConvertError

    with pytest.raises(ConvertError):
        lift("\tld1 {v0.4s}, [x1], x2\n")


# ---------- the feasibility probes


def test_lowmem_probe_finds_no_low_memory():
    result = subprocess.run([str(built(BUILD / "lowmem_probe"))], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0
    assert "sizeof(void *) = 8" in result.stdout
    assert "MAP_FIXED 0x80000000: Cannot allocate memory" in result.stdout


def test_arena_probe_runs_lifted_ilp32_code():
    result = subprocess.run([str(built(BUILD / "arena_probe"))], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "ARENA PROBE OK" in result.stdout


def test_hvf_probe_runs_ilp32_code_in_a_vm():
    result = subprocess.run([str(built(BUILD / "hvf_probe"))], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "HVF PROBE OK" in result.stdout


# ---------- the host and the app


def test_host_loads_the_guest_image(tmp_path):
    result = run_game(tmp_path / "user", "--check")
    assert result.returncode == 0, result.stderr
    assert re.search(r"loaded: \d+ imports, 0 unavailable", result.stderr)
    assert (tmp_path / "user" / "macos-host.log").is_file()


def test_host_rejects_unknown_arguments(tmp_path):
    result = run_game(tmp_path / "user", "--bogus")
    assert result.returncode == 2
    assert "usage" in result.stderr


def test_missing_game_data_is_reported(tmp_path):
    # a run nobody watches (exit_after) does not offer the disc image dialog
    result = run_game(tmp_path / "user", environment={"HALO_EXIT_AFTER": "3", "HALO_FULLSCREEN": "0",
                                                      "HALO_VOLUME": "0", "HALO_NET_ONLINE": "0"})
    assert "no maps/ folder found" in result.stderr
    # the settings go to the user's folder, never into the bundle
    assert (tmp_path / "user" / "config.toml").is_file()
    assert not list(APP.rglob("config.toml"))


def test_app_bundle_layout():
    with open(built(APP) / "Contents/Info.plist", "rb") as source:
        information = plistlib.load(source)
    assert information["CFBundleExecutable"] == "halo-ce"
    assert information["CFBundlePackageType"] == "APPL"
    assert not information["CFBundleIdentifier"].startswith(("com.microsoft", "com.bungie", "com.halo"))
    assert "Not affiliated" in information["NSHumanReadableCopyright"]
    assert (APP / "Contents/Resources/AppIcon.icns").is_file()
    licenses = {path.name for path in (APP / "Contents/Resources/Licenses").iterdir()}
    assert {"Halo-CE-port-CC0.md", "SDL3-LICENSE.txt", "musl-COPYRIGHT.txt"} <= licenses
    minos = re.search(r"minos (\S+)", subprocess.run(["otool", "-l", str(EXECUTABLE)], capture_output=True,
                                                    text=True).stdout).group(1)
    assert information["LSMinimumSystemVersion"] == minos


def test_app_architecture_and_libraries():
    for binary in (built(EXECUTABLE), APP / "Contents/Frameworks/libSDL3.0.dylib"):
        assert subprocess.run(["lipo", "-archs", str(binary)], capture_output=True, text=True).stdout.split() == ["arm64"]
        libraries = subprocess.run(["otool", "-L", str(binary)], capture_output=True, text=True).stdout
        for line in libraries.splitlines()[1:]:
            path = line.strip().split(" (")[0]
            assert path.startswith(("@rpath/", "/System/", "/usr/lib/")), f"{binary.name} links {path}"
    load_commands = subprocess.run(["otool", "-l", str(EXECUTABLE)], capture_output=True, text=True).stdout
    rpaths = re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (\S+)", load_commands)
    assert rpaths == ["@executable_path/../Frameworks"]


def test_app_signature_is_valid_and_ad_hoc():
    result = subprocess.run(["codesign", "--verify", "--strict", "--deep", str(built(APP))], capture_output=True,
                            text=True)
    assert result.returncode == 0, result.stderr
    details = subprocess.run(["codesign", "-dv", str(APP)], capture_output=True, text=True).stderr
    assert "Signature=adhoc" in details


def test_disk_image_holds_the_app_and_applications():
    images = sorted(BUILD.glob("Halo-CE-macOS-*-arm64.dmg"))
    if not images:
        pytest.skip("no disk image built (ninja macos_dmg)")
    from macos_dmg import check_image

    assert subprocess.run(["hdiutil", "verify", str(images[-1])], capture_output=True).returncode == 0
    check_image(images[-1], "Halo CE.app")


# ---------- the game, with the player's data


def game_data() -> Path:
    data = os.environ.get("HALO_MACOS_TEST_DATA")
    if not data or not (Path(data) / "maps" / "ui.map").is_file():
        pytest.skip("HALO_MACOS_TEST_DATA is not a folder holding maps/ui.map")
    return Path(data)


def test_game_reaches_the_main_menu_and_quits(tmp_path):
    user = tmp_path / "user"
    user.mkdir()
    (user / "maps").symlink_to(game_data() / "maps")
    result = run_game(user, environment={"HALO_EXIT_AFTER": "8", "HALO_DISPLAY_MODE": "windowed", "HALO_VOLUME": "0",
                                         "HALO_NET_ONLINE": "0"})
    assert result.returncode == 0, result.stderr[-2000:]
    assert "presented to the game as OpenGL ES 3.0" in result.stderr
    assert "menus:" in result.stderr
    assert "exiting after debug.exit_after" in result.stderr


def test_game_draws_a_campaign_level(tmp_path):
    user = tmp_path / "user"
    shots = tmp_path / "shots"
    user.mkdir()
    shots.mkdir()
    (user / "maps").symlink_to(game_data() / "maps")
    (user / "init.txt").write_text("map_name levels\\a30\\a30\n", encoding="ascii")
    result = run_game(user, environment={"HALO_EXIT_AFTER": "25", "HALO_DISPLAY_MODE": "windowed",
                                         "HALO_VOLUME": "0", "HALO_NET_ONLINE": "0", "HALO_NO_VSYNC": "1",
                                         "HALO_SCREENSHOT_DIR": str(shots), "HALO_SCREENSHOT_EVERY": "300"},
                      timeout=180)
    assert result.returncode == 0, result.stderr[-2000:]
    frames = sorted(shots.glob("frame*.bmp"))
    assert len(frames) >= 3
    # a drawn frame is not one colour: its bytes vary (the level's opening
    # cutscene has fades to black, so not every frame is)
    drawn = [frame for frame in frames if len(set(frame.read_bytes()[54::97])) > 16]
    assert len(drawn) >= len(frames) // 2, f"{len(drawn)} of {len(frames)} frames drawn"
