#!/usr/bin/env python3
from __future__ import annotations

import shutil
import sys
import zipfile
from pathlib import Path


ARCHIVE_NAME = "slang-2026.12-windows-x86_64.zip"
PACKAGE_ROOT_NAME = "slang-2026.12-windows-x86_64"


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parent
    package_root = root / PACKAGE_ROOT_NAME
    archive_file = root / ARCHIVE_NAME
    slang_exe = package_root / "bin" / "slangc.exe"
    slang_config = package_root / "cmake" / "slangConfig.cmake"
    slang_targets = package_root / "cmake" / "slangTargets.cmake"

    if package_root.exists() and not slang_exe.exists():
        shutil.rmtree(package_root, ignore_errors=True)

    if slang_exe.exists() and slang_config.exists() and slang_targets.exists():
        print(f"Slang ready: {slang_exe}")
        return 0

    if not archive_file.exists():
        raise RuntimeError(f"Slang archive was not found: {archive_file}")

    shutil.rmtree(package_root, ignore_errors=True)
    package_root.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive_file) as archive:
        archive.extractall(package_root)

    if not (slang_exe.exists() and slang_config.exists() and slang_targets.exists()):
        raise RuntimeError(f"Slang was not ready after extraction: {package_root}")

    print(f"Slang ready: {slang_exe}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
