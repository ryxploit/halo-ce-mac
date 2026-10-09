#!/usr/bin/env python3
"""Lift the macOS guest's memory accesses into its 4 GB arena.

The guest is the Android port's ILP32 code (tools/android_asm_convert.py).
A macOS arm64 process cannot map the low 4 GB that the guest's 32-bit
pointers address (docs/macos-port-audit.md), so the host reserves a 4 GB
arena aligned to 4 GB, keeps its base in x27, and every guest memory access
and indirect branch is rewritten to land inside it:

    ldr w0, [x1, #8]      ->    mov w15, w1
                                orr x15, x15, x27
                                ldr w0, [x15, #8]

The compiler reserves x15 (scratch) and x27 (the arena base) with
-ffixed-x15 -ffixed-x27. Pointers stay 32-bit, so the structure layouts and
the game's data do not change. Direct branches and PC-relative references
keep their offsets; adr/adrp results, which are native addresses because the
code runs inside the arena, are truncated back to guest offsets. Accesses
based on sp are left alone: the guest's stacks are in the arena, so sp is
already a native address. The host's import stubs (tools/android_imports.py)
are not lifted: they branch to 64-bit host functions, which preserve x27
because it is callee-saved.

The technique and this pass come from the iOS port by Nicholas Dominici
(github.com/NicholasDominici/halo-ce-ios, tools/ios_asm_convert.py, CC0 1.0).

Usage: macos_asm_lift.py input.darwin.s output.s
(input is the compiler's arm64_32 Darwin assembly; output is lifted ELF assembly)
"""

import re
import sys
from pathlib import Path

from android_asm_convert import ConvertError, Converter

RESERVED_REGISTER = re.compile(r"\b[wx](15|27)\b")
SCRATCH = re.compile(r"\b[wx]15\b")
SAVE_PAIR = re.compile(r"(?:stp|ldp)\s.*\[sp")
MEMORY_OPERAND = re.compile(r"\[(x\d+|sp)([^\]]*)\](!?)(.*)$")
IMMEDIATE = re.compile(r"#-?\d+")


def lift(assembly: str) -> str:
    """Rewrite ELF assembly; unsupported addressing forms raise ConvertError."""
    output = []
    for number, line in enumerate(assembly.splitlines(), 1):
        text = line.strip()
        if not text or text.startswith(".") or text.endswith(":"):
            output.append(line)
            continue
        # -ffixed-x27 keeps x27 from being allocated, but a prologue can
        # still save and restore it as the pair of x28 (stp x28, x27, [sp...]),
        # which leaves its value unchanged; nothing else may touch x15 or x27
        if RESERVED_REGISTER.search(text) and not (SAVE_PAIR.match(text) and not SCRATCH.search(text)):
            raise ConvertError(f"line {number}: reserved register used: {text}")
        op, _, args = text.partition(" ")
        if op in ("adr", "adrp"):
            # the backend assumes symbol addresses are zero-extended 32-bit
            # values; the real PC-relative result includes the arena's base
            register = args.split(",", 1)[0].strip()
            output.extend([line, f"\tmov w{register[1:]}, w{register[1:]}"])
            continue
        if op in ("br", "blr"):
            target = args.strip()
            if not re.fullmatch(r"x\d+", target):
                raise ConvertError(f"line {number}: unsupported branch: {text}")
            output.extend([f"\tmov w15, w{target[1:]}", "\torr x15, x15, x27", f"\t{op} x15"])
            continue
        match = MEMORY_OPERAND.search(args)
        if not match:
            output.append(line)
            continue
        base, offset, pre_index, tail = match.groups()
        if base == "sp":
            output.append(line)
            continue
        # immediate pre- and post-indexed forms: the access goes through x15,
        # and the original base register is updated afterwards
        update = None
        if pre_index:
            increment = offset.lstrip(", ")
            if not IMMEDIATE.fullmatch(increment):
                raise ConvertError(f"line {number}: unsupported pre-index: {text}")
            update = increment
        elif tail.strip():
            increment = tail.lstrip(", ")
            if not IMMEDIATE.fullmatch(increment):
                raise ConvertError(f"line {number}: unsupported post-index: {text}")
            update = increment
        output.append(f"\tmov w15, w{base[1:]}")
        output.append("\torr x15, x15, x27")
        rewritten = args[:match.start()] + "[x15" + offset + "]" + pre_index + tail
        output.append(f"\t{op} {rewritten}")
        if update:
            value = int(update[1:])
            output.append(f"\t{'sub' if value < 0 else 'add'} {base}, {base}, #{abs(value)}")
    return "\n".join(output) + "\n"


def main() -> None:
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    source, destination = Path(sys.argv[1]), Path(sys.argv[2])
    lines = source.read_text(encoding="utf-8", errors="surrogateescape").split("\n")
    try:
        result = lift(Converter(lines).run())
    except ConvertError as error:
        print(f"{source}: {error}", file=sys.stderr)
        sys.exit(1)
    destination.write_text(result, encoding="utf-8", errors="surrogateescape")


if __name__ == "__main__":
    main()
