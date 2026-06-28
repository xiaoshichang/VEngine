#!/usr/bin/env python3
from __future__ import annotations

import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


ARCHIVE_BY_PLATFORM = {
    "Windows": ("slang-2026.12-windows-x86_64.zip", "slang-2026.12-windows-x86_64"),
    "Darwin": ("slang-2026.12-macos-aarch64.tar.gz", "slang-2026.12-macos-aarch64"),
}


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parent
    host_key = "Darwin" if sys.platform == "darwin" else "Windows"
    archive_name, package_root_name = ARCHIVE_BY_PLATFORM[host_key]
    package_root = root / package_root_name
    archive_file = root / archive_name
    slang_exe = package_root / "bin" / ("slangc.exe" if host_key == "Windows" else "slangc")
    slang_config = package_root / "lib" / "cmake" / "slang" / "slangConfig.cmake"
    slang_targets = package_root / "lib" / "cmake" / "slang" / "slangTargets.cmake"

    if package_root.exists() and not slang_exe.exists():
        shutil.rmtree(package_root, ignore_errors=True)

    if slang_exe.exists() and slang_config.exists() and slang_targets.exists():
        print(f"Slang ready: {slang_exe}")
        return 0

    if not archive_file.exists():
        raise RuntimeError(f"Slang archive was not found: {archive_file}")

    shutil.rmtree(package_root, ignore_errors=True)
    package_root.mkdir(parents=True, exist_ok=True)
    if archive_file.suffix == ".zip":
        with zipfile.ZipFile(archive_file) as archive:
            archive.extractall(package_root)
    else:
        subprocess.run(["tar", "xzf", str(archive_file), "-C", str(package_root), "--strip-components=1"], check=True)

    if not (slang_exe.exists() and slang_config.exists() and slang_targets.exists()):
        raise RuntimeError(f"Slang was not ready after extraction: {package_root}")

    print(f"Slang ready: {slang_exe}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
