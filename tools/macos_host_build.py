"""Ninja rules for the macOS host (``ninja macos_host``).

The host is the native arm64 program that runs the guest image
(tools/macos_guest_build.py) in its arena: port/macos/host, the generated
bridges for the guest's imports (tools/macos_host_bridges.py), the guest image
itself embedded in its signed __TEXT (tools/macos_embed_guest.py), and the
file and socket helpers of port/linux/src/posix_*.c, built for macOS. It links
SDL3 and OpenGL.
"""

from pathlib import Path
from typing import Any, Dict, List

from .linux_build import MINIUPNPC_DEFINES, MINIUPNPC_DIR, miniupnpc_sources
from .macos_fetch import GL_INCLUDE
from .ninja_syntax import Writer

PORT_DIR = Path("port/macos")
ANDROID_DIR = Path("port/android")
LINUX_DIR = Path("port/linux")
BUILD = Path("build/macos")
HOST_BUILD = BUILD / "host"
EXECUTABLE = BUILD / "halo-ce"
BRIDGES = HOST_BUILD / "host_bridges.c"
BRIDGES_TOOL = Path("tools/macos_host_bridges.py")


def generate_macos_host_build(n: Writer, guest: Dict[str, Path], sdl_cflags: str, sdl_libs: str) -> Path:
    """Emits ``macos_host``; returns the executable."""
    obj_dir = HOST_BUILD / "obj"
    gen_dir = BUILD / "guest" / "gen"
    import_lists = [ANDROID_DIR / "host_imports.list", PORT_DIR / "host_imports.list",
                    gen_dir / "posix_imports.list", gen_dir / "gl_imports.list"]

    n.comment("macOS host (ninja macos_host); see docs/macos-port-audit.md")
    n.rule(
        name="macos_host_bridges",
        command=f"$python {BRIDGES_TOOL} $out $in",
        description="MACOS HOST BRIDGES $out",
    )
    n.build(outputs=BRIDGES, rule="macos_host_bridges", inputs=import_lists,
            implicit=[BRIDGES_TOOL, Path("tools/android_gl_stubs.py"),
                      ANDROID_DIR / "guest" / "runtime" / "guest_host.h", PORT_DIR / "guest" / "macos_guest_host.h",
                      LINUX_DIR / "src" / "posix.h", LINUX_DIR / "src" / "gl.h"])

    n.rule(
        name="macos_host_cc",
        command="$macos_cc -arch arm64 -MMD -MF $out.d $cflags -c $in -o $out",
        description="MACOS HOST CC $out",
        depfile="$out.d",
        deps="gcc",
    )
    host_cflags = " ".join([
        "-O2", "-g", "-std=gnu11", "-Wall", "-Wno-unused-function", "-D_FILE_OFFSET_BITS=64",
        f"-I{PORT_DIR}/host", f"-I{PORT_DIR}/guest", f"-I{ANDROID_DIR}/include", f"-I{ANDROID_DIR}/guest/runtime",
        f"-I{LINUX_DIR}/src", f"-I{guest['header'].parent}", f"-I{ANDROID_DIR}/guest/libc/arch/arm64_32",
        f"-I{GL_INCLUDE}", sdl_cflags,
    ])
    objects: List[Path] = []
    sources = sorted((PORT_DIR / "host").glob("*.c")) + [
        LINUX_DIR / "src" / "posix_files.c", LINUX_DIR / "src" / "posix_net.c",
    ]
    for source in sources:
        obj = obj_dir / (source.name + ".o")
        n.build(outputs=obj, rule="macos_host_cc", inputs=source, implicit=[guest["header"]],
                variables={"cflags": host_cflags})
        objects.append(obj)
    for source in [PORT_DIR / "host" / "guest_call.S", guest["source"]]:
        obj = obj_dir / (source.name + ".o")
        n.build(outputs=obj, rule="macos_host_cc", inputs=source,
                implicit=[guest["image"]] if source == guest["source"] else [],
                variables={"cflags": "-O2"})
        objects.append(obj)
    bridges_obj = obj_dir / "host_bridges.c.o"
    n.build(outputs=bridges_obj, rule="macos_host_cc", inputs=BRIDGES, implicit=[guest["header"]],
            variables={"cflags": host_cflags + " -Wno-unused-parameter"})
    objects.append(bridges_obj)
    # internet play's UPnP (posix_upnp.c, with port/third_party/miniupnpc)
    miniupnpc_cflags = " ".join([host_cflags, f"-I{MINIUPNPC_DIR / 'include'}", f"-I{MINIUPNPC_DIR / 'src'}",
                                 *MINIUPNPC_DEFINES])
    for source in [LINUX_DIR / "src" / "posix_upnp.c", *miniupnpc_sources()]:
        name = ("miniupnpc_" + source.name) if source.parent.parent == MINIUPNPC_DIR else source.name
        obj = obj_dir / (name + ".o")
        n.build(outputs=obj, rule="macos_host_cc", inputs=source,
                variables={"cflags": miniupnpc_cflags + (" -w" if source.name != "posix_upnp.c" else "")})
        objects.append(obj)

    n.rule(
        name="macos_host_link",
        command="$macos_cc -arch arm64 -o $out @$out.rsp $libs -framework OpenGL -framework Foundation",
        description="MACOS HOST LINK $out",
        rspfile="$out.rsp",
        rspfile_content="$in_newline",
    )
    n.build(outputs=EXECUTABLE, rule="macos_host_link", inputs=objects, variables={"libs": sdl_libs})
    n.build(outputs="macos_host", rule="phony", inputs=EXECUTABLE)
    return EXECUTABLE
