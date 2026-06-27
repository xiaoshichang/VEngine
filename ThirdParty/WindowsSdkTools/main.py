#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import shutil
import sys
from pathlib import Path


SDK_VERSION = "10.0.26100.0"
SHA256 = "005EFF830845789C7EFB2831A0B41950EE6954E9BCD93BAF50DE67AD537728B2"


def sha256_hex(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parent
    tool_dir = root / "Tools" / "x64"
    fxc_exe = tool_dir / "fxc.exe"

    if fxc_exe.exists() and sha256_hex(fxc_exe) == SHA256.upper():
        print(f"FXC ready: {fxc_exe}")
        return 0

    sdk_root = Path(r"C:\Program Files (x86)\Windows Kits\10\bin") / SDK_VERSION / "x64"
    sdk_fxc_exe = sdk_root / "fxc.exe"
    if not sdk_fxc_exe.exists():
        raise RuntimeError(f"fxc.exe was not found in the installed Windows SDK path: {sdk_fxc_exe}")

    if sha256_hex(sdk_fxc_exe) != SHA256.upper():
        raise RuntimeError("Installed fxc.exe hash mismatch.")

    tool_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(sdk_fxc_exe, fxc_exe)

    if sha256_hex(fxc_exe) != SHA256.upper():
        raise RuntimeError(f"Copied fxc.exe hash mismatch after writing: {fxc_exe}")

    print(f"FXC ready: {fxc_exe}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
