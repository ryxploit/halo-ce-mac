#!/usr/bin/env python3
"""Download the third-party sources the macOS guest build needs.

configure.py never downloads anything for macOS. Run this once, by hand:

    python3 tools/macos_fetch.py

It puts musl's sources and the Khronos OpenGL ES headers under
build/third_party/ (ignored by git). Each file has a pinned revision and a
SHA-256; a file that does not match is deleted and the script fails.
Files that are already there and match are not downloaded again.
"""

import hashlib
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

THIRD_PARTY = Path("build/third_party")
MUSL_VERSION = "1.2.5"
MUSL_DIR = THIRD_PARTY / f"musl-{MUSL_VERSION}"
MUSL_URL = f"https://musl.libc.org/releases/musl-{MUSL_VERSION}.tar.gz"
MUSL_SHA256 = "a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4"
GL_INCLUDE = THIRD_PARTY / "gl_include"

GL_REVISION = "1cdd228e34966dd6b95bd203e9f84faba0f371a1"
EGL_REVISION = "db3425b8246136faccb5e2782b5694960bd6edf1"
KHRONOS = "https://raw.githubusercontent.com/KhronosGroup"
HEADERS = {
    "GLES3/gl32.h": (f"{KHRONOS}/OpenGL-Registry/{GL_REVISION}/api/GLES3/gl32.h",
                     "f203257863a7de3fce851efa61490907171db82082381d05f86f5d9bd39e8e17"),
    "GLES3/gl3platform.h": (f"{KHRONOS}/OpenGL-Registry/{GL_REVISION}/api/GLES3/gl3platform.h",
                            "a9e060dae5a2b11c5a889b679692b7089a10a7e03ebfbb6cf28217f6e322fb08"),
    "GLES2/gl2ext.h": (f"{KHRONOS}/OpenGL-Registry/{GL_REVISION}/api/GLES2/gl2ext.h",
                       "9afc725e9dda7c8b476e7337e75b22fa660ed462c169e203ec28c10e62f91f4c"),
    "GLES2/gl2platform.h": (f"{KHRONOS}/OpenGL-Registry/{GL_REVISION}/api/GLES2/gl2platform.h",
                            "f5da0747540a50be5f44aad264aae45bdf157a192c40f17487dd9a2f99c71b6c"),
    "KHR/khrplatform.h": (f"{KHRONOS}/EGL-Registry/{EGL_REVISION}/api/KHR/khrplatform.h",
                          "7b1e01aaa7ad8f6fc34b5c7bdf79ebf5189bb09e2c4d2e79fc5d350623d11e83"),
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def download(url: str, destination: Path, expected: str) -> None:
    if destination.is_file() and sha256(destination) == expected:
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    print(f"downloading {url}")
    if not shutil.which("curl"):
        raise SystemExit("curl is required to download the third-party sources")
    subprocess.run(["curl", "-sSfL", "-o", str(destination), url], check=True)
    actual = sha256(destination)
    if actual != expected:
        destination.unlink()
        raise SystemExit(f"{destination}: SHA-256 {actual}, expected {expected}")


def fetch_musl() -> None:
    if (MUSL_DIR / "src").is_dir() and (MUSL_DIR / ".sha256").is_file() and \
            (MUSL_DIR / ".sha256").read_text().strip() == MUSL_SHA256:
        return
    with tempfile.TemporaryDirectory(dir=THIRD_PARTY) as temporary:
        archive = Path(temporary) / f"musl-{MUSL_VERSION}.tar.gz"
        download(MUSL_URL, archive, MUSL_SHA256)
        with tarfile.open(archive) as tar:
            # the archive's own entries only, never above its folder
            tar.extractall(temporary, filter="data")
        if MUSL_DIR.exists():
            shutil.rmtree(MUSL_DIR)
        (Path(temporary) / f"musl-{MUSL_VERSION}").rename(MUSL_DIR)
    (MUSL_DIR / ".sha256").write_text(MUSL_SHA256 + "\n")


def main() -> None:
    THIRD_PARTY.mkdir(parents=True, exist_ok=True)
    fetch_musl()
    for name, (url, expected) in HEADERS.items():
        download(url, GL_INCLUDE / name, expected)
    print(f"musl {MUSL_VERSION}: {MUSL_DIR}")
    print(f"OpenGL ES headers: {GL_INCLUDE}")


if __name__ == "__main__":
    if Path.cwd().resolve() != Path(__file__).resolve().parents[1]:
        sys.exit("run from the repository root: python3 tools/macos_fetch.py")
    main()
