"""Ninja rules for the macOS guest image (``ninja macos_guest``).

The macOS port runs the game as the Android port's ILP32 guest
(tools/android_build.py): the game sources, the platform layer shared with
the Linux port (port/linux/src) and the guest runtime and musl subset of
port/android/guest, compiled by clang for arm64_32-apple-watchos and
converted to ELF assembly. A macOS process cannot map the low 4 GB those
32-bit pointers address, so one more pass (tools/macos_asm_lift.py) moves
every memory access into a 4 GB arena whose base the host keeps in x27
(docs/macos-port-audit.md). ld.lld links the image with port/macos/guest.ld
and tools/macos_embed_guest.py turns it into sources for the host
executable, which carries the guest code in its signed __TEXT.

The guest takes the desktop branches of the platform layer (HALO_ANDROID is
not defined): HALO_GUEST selects the ILP32 guest ABI and HALO_MACOS the
macOS specifics.

Nothing is downloaded at configure time: musl and the Khronos OpenGL ES
headers come from tools/macos_fetch.py, SDL3's headers from pkg-config (the
same SDL3 the host links, so that the structures the two halves share
agree), and ld.lld from the developer's toolchain.
"""

import json
import shutil
import subprocess
from pathlib import Path
from typing import Any, Dict, List, Optional

from .android_build import (EXPAT_DIR, EXPAT_SOURCES, GUEST_CODE_FLAGS, KCP_DIR, MONOCYPHER_DIR, MUSL_DIRECTORIES,
                            MUSL_EXCLUDE, MUSL_FILES, MUSL_THREAD_PREFIXES, TOML_DIR, VARIADIC_PROTOTYPE_FILES,
                            ZLIB_DEFINES, ZLIB_DIR, ZLIB_SOURCES)
from .embed_assets import hud_assets_build
from .linux_build import (LINUX_PROFILE, MUSL_MATH_DIR, XDK_INCLUDE, compile_launcher, game_defines_and_includes,
                          game_sources, musl_math_sources, pgo_mode, pgo_profile, profile_use_flags, xdk_headers)
from .macos_fetch import GL_INCLUDE, MUSL_DIR, MUSL_VERSION
from .ninja_syntax import Writer

ANDROID_DIR = Path("port/android")
LINUX_DIR = Path("port/linux")
PORT_DIR = Path("port/macos")
BUILD = Path("build/macos")
GUEST_DIR = BUILD / "guest"
IMAGE = BUILD / "halo_guest.elf"
EMBED_DIR = BUILD / "embed"
EMBEDDED_HEADER = EMBED_DIR / "guest_image.h"
EMBEDDED_SOURCE = EMBED_DIR / "guest_image.S"
LINKER_SCRIPT = PORT_DIR / "guest.ld"
LIFT_TOOL = Path("tools/macos_asm_lift.py")
EMBED_TOOL = Path("tools/macos_embed_guest.py")

# the Android guest's ABI (tools/android_build.py GUEST_ABI_FLAGS) for macOS:
# no HALO_ANDROID; x15 and x27 reserved for the arena pass; no compressed jump
# tables, whose entries the arena pass cannot see
GUEST_ABI_FLAGS = [
    "--target=arm64_32-apple-watchos",
    "-U__APPLE__",
    "-U__MACH__",
    "-fno-define-target-os-macros",
    "-Wno-incompatible-sysroot",
    "-D__linux__=1",
    "-D__unix__=1",
    "-DHALO_GUEST=1",
    "-DHALO_MACOS=1",
    "-DHALO_GLES=1",
    "-mcpu=cortex-a53",
    "-ffixed-x15",
    "-ffixed-x27",
    "-mllvm",
    "-aarch64-enable-compress-jump-tables=false",
    "-nostdinc",
    "-fshort-wchar",
    "-fno-stack-protector",
    "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
    "-femulated-tls",
    "-mllvm",
    "-aarch64-neon-syntax=generic",
    "-ffp-contract=off",
    "-O2",
]


def find_lld(sln: Any) -> Optional[str]:
    """--macos-lld, else ld.lld on the PATH, else Homebrew's lld formula"""
    explicit = getattr(sln, "macos_lld", None)
    if explicit:
        return explicit
    found = shutil.which("ld.lld")
    if found:
        return found
    for prefix in ("/opt/homebrew/opt/lld/bin/ld.lld", "/usr/local/opt/lld/bin/ld.lld"):
        if Path(prefix).is_file():
            return prefix
    return None


def sdl3_include_dir() -> Optional[Path]:
    """the directory holding SDL3/SDL.h, from pkg-config sdl3"""
    pkg_config = shutil.which("pkg-config")
    if not pkg_config:
        return None
    try:
        result = subprocess.run([pkg_config, "--variable=includedir", "sdl3"], capture_output=True, text=True,
                                check=False)
    except OSError:
        return None
    directory = Path(result.stdout.strip()) if result.returncode == 0 and result.stdout.strip() else None
    return directory if directory and (directory / "SDL3" / "SDL.h").is_file() else None


def _musl_sources() -> List[Path]:
    src = MUSL_DIR / "src"
    result = set()
    for directory in MUSL_DIRECTORIES:
        result.update((src / directory).glob("*.c"))
    for name in MUSL_FILES:
        result.add(src / name)
    for path in (src / "thread").glob("*.c"):
        if path.name.startswith(MUSL_THREAD_PREFIXES):
            result.add(path)
    sources = []
    for path in sorted(result):
        relative = path.relative_to(src).as_posix()
        if relative in MUSL_EXCLUDE or any(relative.startswith(e + "/") for e in MUSL_EXCLUDE):
            continue
        sources.append(path)
    return sources


def missing_dependencies(sln: Any) -> List[str]:
    problems = []
    if not (MUSL_DIR / "src").is_dir() or not (GL_INCLUDE / "GLES3" / "gl32.h").is_file():
        problems.append("musl and the OpenGL ES headers: run python3 tools/macos_fetch.py")
    if not find_lld(sln):
        problems.append("ld.lld: install LLVM's lld (Homebrew: brew install lld) or pass --macos-lld")
    if not sdl3_include_dir():
        problems.append("SDL3's headers: SDL3 must be visible to pkg-config as sdl3 (Homebrew: brew install sdl3 pkgconf)")
    return problems


def guest_rules(n: Writer, sln: Any) -> None:
    """The compile, assemble and link rules shared by the game guest and the
    arena probe (tools/macos_build.py)."""
    n.variable("macos_guest_cc", getattr(sln, "macos_guest_cc", None) or "clang")
    n.variable("macos_lld", find_lld(sln) or "ld.lld")
    python = "$python"
    # C -> Darwin arm64_32 assembly -> lifted ELF assembly -> object
    n.rule(
        name="macos_guest_cc",
        command=(f"{compile_launcher(sln)}$macos_guest_cc -MMD -MF $out.d $cflags -S $in -o $out.darwin.s && "
                 f"{python} {LIFT_TOOL} $out.darwin.s $out.s && "
                 "$macos_guest_cc --target=aarch64-none-elf -c $out.s -o $out"),
        description="MACOS GUEST CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    # hand-written guest assembly (the import stubs), which is not lifted
    n.rule(
        name="macos_guest_as",
        command="$macos_guest_cc --target=aarch64-none-elf -c $in -o $out",
        description="MACOS GUEST AS $out",
    )
    n.rule(
        name="macos_guest_link",
        command=(f"$macos_lld -m aarch64linux -static -nostdlib -T {LINKER_SCRIPT} -Map $out.map -o $out "
                 "@$out.rsp $libraries"),
        description="MACOS GUEST LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.rule(
        name="macos_guest_embed",
        command=f"{python} {EMBED_TOOL} $in $embed_dir",
        description="MACOS GUEST EMBED $embed_dir",
    )
    n.rule(
        name="macos_guest_imports",
        command=f"{python} tools/android_imports.py $out $in",
        description="MACOS GUEST IMPORTS $out",
    )


def generate_macos_guest_build(n: Writer, sln: Any) -> Optional[Dict[str, Path]]:
    """Emits ``macos_guest``; returns the embedded image's header and source,
    or None (with a ``macos_guest`` target that explains what is missing)."""
    problems = missing_dependencies(sln)
    if problems:
        message = "; ".join(problems).replace("'", "")
        n.rule(
            name="macos_guest_missing",
            command=f"echo 'macOS guest build needs: {message}; then rerun python3 configure.py' >&2; exit 1",
            description="MACOS GUEST DEPENDENCIES MISSING",
        )
        n.build(outputs="macos_guest", rule="macos_guest_missing")
        return None

    config: Dict[str, Any] = json.loads((LINUX_DIR / "port.json").read_text(encoding="utf-8"))
    python = "$python"
    obj_dir = GUEST_DIR / "obj"
    gen_dir = GUEST_DIR / "gen"
    libc_include = GUEST_DIR / "libc_include"
    libc_internal = GUEST_DIR / "libc_internal"
    sdl_include = GUEST_DIR / "sdl_include"
    arch = ANDROID_DIR / "guest" / "libc" / "arch" / "arm64_32"
    semantics_header = Path("build/linux/halo_msvc_semantics.h")
    platform_semantics_header = Path("build/linux/platform_msvc_semantics.h")
    prefix_header = LINUX_DIR / "include" / "halo_linux_prefix.h"

    n.comment("macOS guest image (ninja macos_guest); see docs/macos-port-audit.md")

    # ---------- generated headers and sources

    alltypes = libc_include / "bits" / "alltypes.h"
    syscall_h = libc_include / "bits" / "syscall.h"
    version_h = libc_internal / "version.h"
    n.rule(name="macos_alltypes", command=f"mkdir -p $$(dirname $out) && sed -f {MUSL_DIR}/tools/mkalltypes.sed $in > $out",
           description="MACOS MUSL $out")
    n.build(outputs=alltypes, rule="macos_alltypes",
            inputs=[arch / "bits" / "alltypes.h.in", MUSL_DIR / "include" / "alltypes.h.in"])
    n.rule(name="macos_syscall_h", command="mkdir -p $$(dirname $out) && cp $in $out && sed -n -e s/__NR_/SYS_/p < $in >> $out",
           description="MACOS MUSL $out")
    n.build(outputs=syscall_h, rule="macos_syscall_h", inputs=arch / "bits" / "syscall.h.in")
    n.rule(name="macos_version_h", command=f"mkdir -p $$(dirname $out) && echo '#define VERSION \"{MUSL_VERSION}\"' > $out",
           description="MACOS MUSL $out")
    n.build(outputs=version_h, rule="macos_version_h")

    # SDL3's headers alone, so that the guest sees nothing else of the
    # Homebrew prefix
    sdl_stamp = sdl_include / "stamp"
    n.rule(name="macos_sdl_include",
           command=f"mkdir -p {sdl_include} && ln -sfn {sdl3_include_dir()}/SDL3 {sdl_include}/SDL3 && touch $out",
           description="MACOS SDL3 HEADERS")
    n.build(outputs=sdl_stamp, rule="macos_sdl_include")

    guest_gl_c = gen_dir / "guest_gl.c"
    gl_imports = gen_dir / "gl_imports.list"
    n.rule(
        name="macos_gl_stubs",
        command=(f"mkdir -p {gen_dir} && {python} tools/android_gl_stubs.py {LINUX_DIR}/src/gl.h "
                 f"{GL_INCLUDE}/GLES3/gl32.h {GL_INCLUDE}/GLES2/gl2ext.h {guest_gl_c} {gl_imports}"),
        description="MACOS GL STUBS",
    )
    n.build(outputs=[guest_gl_c, gl_imports], rule="macos_gl_stubs",
            implicit=[Path("tools/android_gl_stubs.py"), LINUX_DIR / "src" / "gl.h"])

    guest_posix_c = gen_dir / "guest_posix.c"
    posix_imports = gen_dir / "posix_imports.list"
    n.rule(
        name="macos_posix_stubs",
        command=(f"mkdir -p {gen_dir} && {python} tools/android_posix_stubs.py {LINUX_DIR}/src/posix.h "
                 f"{guest_posix_c} {posix_imports}"),
        description="MACOS POSIX STUBS",
    )
    n.build(outputs=[guest_posix_c, posix_imports], rule="macos_posix_stubs",
            implicit=[Path("tools/android_posix_stubs.py"), LINUX_DIR / "src" / "posix.h"])

    # the Android guest's imports and the macOS host's own; the host's table
    # (port/macos/host) is generated from the same lists
    imports_s = gen_dir / "imports.s"
    import_lists = [ANDROID_DIR / "host_imports.list", PORT_DIR / "host_imports.list", posix_imports, gl_imports]
    n.build(outputs=imports_s, rule="macos_guest_imports", inputs=import_lists,
            implicit=[Path("tools/android_imports.py")])

    generated_headers = [*xdk_headers(), alltypes, syscall_h, version_h, sdl_stamp,
                         semantics_header, platform_semantics_header]

    # ---------- compilation

    libc_includes = [
        f"-isystem {libc_include}", f"-isystem {arch}", f"-isystem {MUSL_DIR}/arch/generic",
        f"-isystem {MUSL_DIR}/include",
    ]
    guest_cc = getattr(sln, "macos_guest_cc", None) or "clang"
    guest_abi = " ".join(GUEST_ABI_FLAGS + (["-DHALO_RELEASE"] if getattr(sln, "port_release", False) else []))
    guest_code = " ".join(GUEST_CODE_FLAGS)
    tool_implicit = [LIFT_TOOL, Path("tools/android_asm_convert.py"), *generated_headers]
    profile = pgo_profile(sln, None, [LINUX_PROFILE], guest_cc) if pgo_mode(sln) != "train" else None
    profile_flags = " ".join(profile_use_flags(profile))
    if profile:
        tool_implicit.append(profile)

    def guest_object(source: Path, cflags: str, prefix: str = "") -> Path:
        if str(source).startswith(str(BUILD)):
            obj = obj_dir / prefix / source.relative_to(BUILD).with_suffix(".o")
        elif str(source).startswith("build/"):
            obj = obj_dir / prefix / source.relative_to("build").with_suffix(".o")
        else:
            obj = obj_dir / prefix / source.with_suffix(".o")
        n.build(outputs=obj, rule="macos_guest_cc", inputs=source, implicit=tool_implicit,
                variables={"cflags": cflags})
        return obj

    musl_cflags = " ".join([
        guest_abi, "-std=c99", "-ffreestanding", "-fno-common", "-D_XOPEN_SOURCE=700", "-w",
        f"-I{arch}", f"-I{MUSL_DIR}/arch/generic", f"-I{libc_internal}",
        f"-I{ANDROID_DIR}/guest/libc/src_include", f"-I{MUSL_DIR}/src/include",
        f"-I{MUSL_DIR}/src/internal", f"-I{libc_include}", f"-I{MUSL_DIR}/include",
    ])
    musl_objects = [guest_object(source, musl_cflags, "musl") for source in _musl_sources()]

    objects: List[Path] = []
    game_flags = [
        "-std=gnu89", "-D__STRICT_ANSI__", "-w",
        "-Wno-error=incompatible-pointer-types",
        "-Wno-error=incompatible-function-pointer-types",
        "-Wno-error=int-conversion",
        "-Wno-error=implicit-function-declaration",
        "-Wno-error=implicit-int",
        "-Wno-error=return-type",
    ]
    game_cflags = " ".join([
        guest_abi, guest_code, " ".join(game_flags), profile_flags,
        f"-include {prefix_header}", f"-include {semantics_header}",
        f"-I{LINUX_DIR}/include",
        f"-iquote {Path(config['game_sources'])}",
        game_defines_and_includes(config), *libc_includes, f"-idirafter {XDK_INCLUDE}",
    ])
    for source in game_sources(config):
        cflags = game_cflags
        if source.as_posix() in VARIADIC_PROTOTYPE_FILES:
            cflags += f" -include {ANDROID_DIR}/include/halo_android_variadic_prototypes.h"
        objects.append(guest_object(source, cflags))
    for source in sorted(Path(config["game_sources"]).glob("*.c")):
        objects.append(guest_object(source, game_cflags))

    platform_cflags = " ".join([
        guest_abi, guest_code, "-std=gnu11", "-D_GNU_SOURCE", "-DHALO_LINUX_PLATFORM_LAYER", "-w", profile_flags,
        f"-include {prefix_header}", f"-include {platform_semantics_header}",
        f"-I{LINUX_DIR}/src", f"-I{LINUX_DIR}/include", f"-I{ANDROID_DIR}/guest/runtime",
        f"-I{ANDROID_DIR}/include", f"-I{TOML_DIR}", f"-I{EXPAT_DIR}", f"-I{KCP_DIR}", f"-I{MONOCYPHER_DIR}",
        f"-I{ZLIB_DIR}", "-Isource -Isource/cseries",
        f"-I{sdl_include}", f"-I{GL_INCLUDE}", *libc_includes, f"-idirafter {XDK_INCLUDE}",
    ])
    # the host runs these: the file and socket functions (posix_*), and the
    # write tracking (memory_watch.c, replaced by guest_memory_watch.c)
    host_only = {"memory_watch.c"}
    for source in sorted((LINUX_DIR / "src").glob("*.c")):
        if source.name.startswith("posix_") or source.name in host_only:
            continue
        objects.append(guest_object(source, platform_cflags))
    for source in hud_assets_build(n, "macos", gen_dir / "hud_hires_assets.c"):
        objects.append(guest_object(source, platform_cflags))
    objects.append(guest_object(TOML_DIR / "tomlc17.c", platform_cflags))
    for name in EXPAT_SOURCES:
        objects.append(guest_object(EXPAT_DIR / name, platform_cflags))
    objects.append(guest_object(KCP_DIR / "ikcp.c", platform_cflags))
    for name in ("monocypher.c", "monocypher-ed25519.c"):
        objects.append(guest_object(MONOCYPHER_DIR / name, platform_cflags))
    for name in ZLIB_SOURCES:
        objects.append(guest_object(ZLIB_DIR / name, " ".join([platform_cflags, *ZLIB_DEFINES,
                                                               "-U__ARM_FEATURE_CRC32"])))
    musl_math_cflags = " ".join([
        guest_abi, "-std=gnu11", "-w", profile_flags, *libc_includes, f"-I{MUSL_MATH_DIR}/include",
        f"-include {MUSL_MATH_DIR}/include/libm.h",
    ])
    for source in musl_math_sources():
        objects.append(guest_object(source, musl_math_cflags))

    runtime_internal_cflags = " ".join([
        guest_abi, "-std=c99", "-ffreestanding", "-fno-common", "-D_XOPEN_SOURCE=700", "-D_GNU_SOURCE",
        f"-I{ANDROID_DIR}/guest/runtime", f"-I{ANDROID_DIR}/include",
        f"-I{arch}", f"-I{MUSL_DIR}/arch/generic", f"-I{libc_internal}",
        f"-I{ANDROID_DIR}/guest/libc/src_include", f"-I{MUSL_DIR}/src/include",
        f"-I{MUSL_DIR}/src/internal", f"-I{libc_include}", f"-I{MUSL_DIR}/include",
    ])
    runtime_cflags = " ".join([
        guest_abi, guest_code, "-std=gnu11", "-D_GNU_SOURCE",
        f"-I{ANDROID_DIR}/guest/runtime", f"-I{ANDROID_DIR}/include", f"-I{LINUX_DIR}/src",
        f"-I{sdl_include}", f"-I{GL_INCLUDE}", *libc_includes,
    ])
    # the Android guest runtime, then the macOS guest's own additions
    for runtime_dir in (ANDROID_DIR / "guest" / "runtime", PORT_DIR / "guest"):
        for source in sorted(runtime_dir.glob("*.c")):
            if source.name in ("guest_thread.c", "guest_start.c"):
                objects.append(guest_object(source, runtime_internal_cflags))
            elif source.name == "guest_memory_watch.c":
                objects.append(guest_object(source, platform_cflags))
            else:
                objects.append(guest_object(source, f"{runtime_cflags} -I{PORT_DIR}/guest"))
    objects.append(guest_object(guest_gl_c, runtime_cflags))
    objects.append(guest_object(guest_posix_c, runtime_cflags))
    imports_o = obj_dir / "gen" / "imports.o"
    n.build(outputs=imports_o, rule="macos_guest_as", inputs=imports_s)
    objects.append(imports_o)

    # ---------- the image, and the sources that embed it in the host

    # musl as an archive would be, without an archiver: ld.lld takes only
    # the members the image needs
    n.build(outputs=IMAGE, rule="macos_guest_link", inputs=objects, implicit=[*musl_objects, LINKER_SCRIPT],
            variables={"libraries": " ".join(["--start-lib", *map(str, musl_objects), "--end-lib"])})
    n.build(outputs=[EMBEDDED_HEADER, EMBEDDED_SOURCE], rule="macos_guest_embed", inputs=IMAGE,
            implicit=[EMBED_TOOL], variables={"embed_dir": str(EMBED_DIR)})
    n.build(outputs="macos_guest", rule="phony", inputs=[EMBEDDED_HEADER, EMBEDDED_SOURCE])
    return {"header": EMBEDDED_HEADER, "source": EMBEDDED_SOURCE, "image": IMAGE}
