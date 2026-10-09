#!/usr/bin/env python3
"""Generate the macOS host's bridges for the guest's imports.

Every import of the macOS guest (port/android/host_imports.list,
port/macos/host_imports.list and the generated POSIX and OpenGL ES lists) is a
host function the guest's stub branches to with the guest's arguments in
their registers (tools/android_imports.py). The guest's pointers are offsets
into the host's arena (port/macos/host/macos_host.h), so each import gets a
bridge here that takes its pointer arguments as integers and translates them
with host_pointer before calling the host's function:

- host_<name>: declared in port/android/guest/runtime/guest_host.h and
  port/macos/guest/macos_guest_host.h, defined in port/macos/host;
- hostposix_<name>: port/linux/src/posix.h's posix_<name>, built for the host;
- hostgl_<function>: the OpenGL entry point, resolved through SDL (the
  context is OpenGL 4.1 core; port/macos/host/host_gl.c has the functions
  that differ from OpenGL ES). The guest's entry points
  (tools/android_gl_stubs.py) have already widened GLsizeiptr and GLintptr
  and the arguments beyond the eighth integer one to 64 bits, and the array
  of glShaderSource; the bridges take them so. Vertex attribute and index
  "pointers" are offsets into buffer objects, which the renderer always
  binds, and are not translated.

It also writes host_resolve_import, the name-to-bridge table, and fails for
an import that has no declaration.

Usage: macos_host_bridges.py output.c import_list...
"""

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from android_gl_stubs import FLOAT_TYPES, WIDE_TYPES, android_functions, prototypes, split_parameter  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
HOST_HEADERS = [ROOT / "port/android/guest/runtime/guest_host.h", ROOT / "port/macos/guest/macos_guest_host.h"]
POSIX_HEADER = ROOT / "port/linux/src/posix.h"
GL_HEADER = ROOT / "port/linux/src/gl.h"
GL_INCLUDE = ROOT / "build/third_party/gl_include"
INTEGER_REGISTER_COUNT = 8

# pointer arguments that are handles of the host's, not guest memory
OPAQUE = {("posix_directory_next", 0), ("posix_directory_close", 0)}
# OpenGL pointer arguments that are offsets into a bound buffer object
GL_OFFSETS = {
    ("glVertexAttribPointer", 5), ("glVertexAttribIPointer", 4), ("glDrawElements", 3),
    ("glDrawElementsBaseVertex", 3), ("glDrawRangeElements", 5), ("glDrawElementsInstanced", 3),
}
# OpenGL functions the host implements itself (host_gl.c), with the bridge's arguments
GL_SPECIAL = {
    "glShaderSource": ("void", ["GLuint a0", "GLsizei a1", "uint64_t a2", "uint64_t a3"],
                       "host_gl_shader_source(a0, a1, host_pointer(a2), host_pointer(a3))"),
    "glGetIntegerv": ("void", ["GLenum a0", "uint64_t a1"], "host_gl_get_integerv(a0, host_pointer(a1))"),
    "glInvalidateFramebuffer": ("void", ["GLenum a0", "GLsizei a1", "uint64_t a2"],
                                "host_gl_invalidate_framebuffer(a0, a1, host_pointer(a2))"),
}


def declarations(path: Path, pattern: str):
    text = re.sub(r"/\*.*?\*/", "", path.read_text(encoding="utf-8"), flags=re.S)
    text = re.sub(r"__attribute__\s*\(\(.*?\)\)", "", text)
    return re.findall(r"^([A-Za-z_][\w \t*]*?)\b(" + pattern + r")\s*\(([^;]*?)\)\s*;", text, re.M | re.S)


def parameters(text: str):
    text = " ".join(text.split())
    return [] if text in ("void", "") else [split_parameter(part) for part in text.split(",")]


def native_bridge(name: str, ret: str, params, bridge: str):
    """a bridge for a host_ or posix_ function: guest pointers to native ones"""
    ret = " ".join(ret.split())
    declared, arguments = [], []
    for index, (kind, _) in enumerate(params):
        argument = f"a{index}"
        if "*" in kind:
            declared.append(f"uint64_t {argument}")
            if (name, index) in OPAQUE:
                arguments.append(f"({kind})(uintptr_t){argument}")
            else:
                arguments.append(f"({kind})host_pointer({argument})")
        else:
            declared.append(f"{kind} {argument}")
            arguments.append(argument)
    returns_pointer = "*" in ret
    lines = [f"static {'uint32_t' if returns_pointer else ret} {bridge}({', '.join(declared) or 'void'})", "{"]
    call = f"{name}({', '.join(arguments)})"
    if returns_pointer:
        # (a handle of the host's, or memory in the arena)
        call = f"(uint32_t)(uintptr_t){call}"
    lines.append(f"\t{'' if ret == 'void' else 'return '}{call};")
    lines += ["}", ""]
    return lines


def gl_bridge(name: str, ret: str, params, bridge: str):
    if name in GL_SPECIAL:
        special_ret, declared, call = GL_SPECIAL[name]
        return [f"static {special_ret} {bridge}({', '.join(declared)})", "{", f"\t{call};", "}", ""]
    declared, arguments = [], []
    integer_index = 0
    for index, (kind, _) in enumerate(params):
        argument = f"a{index}"
        pointer = "*" in kind
        base = kind.replace("const", "").strip()
        if base in FLOAT_TYPES and not pointer:
            declared.append(f"{kind} {argument}")
            arguments.append(argument)
            continue
        on_stack = integer_index >= INTEGER_REGISTER_COUNT
        integer_index += 1
        if pointer:
            declared.append(f"uint64_t {argument}")
            if (name, index) in GL_OFFSETS:
                arguments.append(f"({kind})(uintptr_t){argument}")
            else:
                arguments.append(f"({kind})host_pointer({argument})")
        elif base in WIDE_TYPES or on_stack:
            declared.append(f"long long {argument}")
            arguments.append(f"({kind}){argument}")
        else:
            declared.append(f"{kind} {argument}")
            arguments.append(argument)
    signature = ", ".join(kind for kind, _ in params) or "void"
    lines = [
        f"static {ret} {bridge}({', '.join(declared) or 'void'})",
        "{",
        f"\ttypedef {ret} (GL_APIENTRY *function_type)({signature});",
        "\tstatic function_type function;",
        "",
        "\tif (!function)",
        f"\t\tfunction = (function_type)gl_function(\"{name}\");",
        f"\t{'' if ret == 'void' else 'return '}function({', '.join(arguments)});",
        "}",
        "",
    ]
    return lines


def main() -> None:
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    output = Path(sys.argv[1])
    imports = []
    for path in sys.argv[2:]:
        for line in Path(path).read_text(encoding="utf-8").splitlines():
            line = line.split("#", 1)[0].strip()
            if line and line not in imports:
                imports.append(line)

    host_functions = {}
    for header in HOST_HEADERS:
        for ret, name, params in declarations(header, r"host_\w+"):
            host_functions[name] = (ret, parameters(params))
    posix_functions = {name: (ret, parameters(params)) for ret, name, params in declarations(POSIX_HEADER, r"posix_\w+")}
    gl_names = set(android_functions(str(GL_HEADER)))
    gl_prototypes = prototypes(str(GL_INCLUDE / "GLES3/gl32.h"), str(GL_INCLUDE / "GLES2/gl2ext.h"))

    lines = [
        "/* generated by tools/macos_host_bridges.py; do not edit */",
        "#include \"macos_host.h\"",
        "#include \"guest_host.h\"",
        "#include \"macos_guest_host.h\"",
        "#include \"posix.h\"",
        "",
        "#include <SDL3/SDL.h>",
        "#include <GLES3/gl32.h>",
        "#include <GLES2/gl2ext.h>",
        "#include <string.h>",
        "",
        "/* host_gl.c */",
        "void host_gl_shader_source(GLuint shader, GLsizei count, const uint64_t *strings, const GLint *lengths);",
        "void host_gl_get_integerv(GLenum name, GLint *value);",
        "void host_gl_invalidate_framebuffer(GLenum target, GLsizei count, const GLenum *attachments);",
        "",
        "static void gl_unavailable(void)",
        "{",
        "\thost_logf(HOST_LOG_ERROR, \"the game called an OpenGL function this context does not have\");",
        "}",
        "",
        "static void *gl_function(const char *name)",
        "{",
        "\tvoid *function = (void *)SDL_GL_GetProcAddress(name);",
        "",
        "\tif (!function)",
        "\t{",
        "\t\thost_logf(HOST_LOG_ERROR, \"OpenGL entry point unavailable: %s\", name);",
        "\t\tfunction = (void *)gl_unavailable;",
        "\t}",
        "\treturn function;",
        "}",
        "",
    ]
    table = []
    missing = []
    for name in imports:
        if name.startswith("hostgl_"):
            function = name[len("hostgl_"):]
            if function not in gl_names or function not in gl_prototypes:
                missing.append(name)
                continue
            ret, params = gl_prototypes[function]
            bridge = "bridge_" + name
            lines += gl_bridge(function, ret, parameters(params), bridge)
        elif name.startswith("hostposix_"):
            function = "posix_" + name[len("hostposix_"):]
            if function not in posix_functions:
                missing.append(name)
                continue
            ret, params = posix_functions[function]
            bridge = "bridge_" + function
            lines += native_bridge(function, ret, params, bridge)
        else:
            if name not in host_functions:
                missing.append(name)
                continue
            ret, params = host_functions[name]
            bridge = "bridge_" + name
            lines += native_bridge(name, ret, params, bridge)
        table.append((name, bridge))
    if missing:
        raise SystemExit("imports without a declaration: " + ", ".join(missing))

    lines += ["static const struct", "{", "\tconst char *name;", "\tvoid *function;", "} bridges[] =", "{"]
    lines += [f"\t{{ \"{name}\", (void *){bridge} }}," for name, bridge in table]
    lines += [
        "};",
        "",
        "void *host_resolve_import(const char *name)",
        "{",
        "\tsize_t index;",
        "",
        "\tfor (index = 0; index < sizeof(bridges) / sizeof(bridges[0]); index++)",
        "\t{",
        "\t\tif (!strcmp(bridges[index].name, name))",
        "\t\t\treturn bridges[index].function;",
        "\t}",
        "\treturn NULL;",
        "}",
        "",
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


if __name__ == "__main__":
    main()
