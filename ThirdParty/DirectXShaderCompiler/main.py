#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

def sha256_hex(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def run(*args: str) -> None:
    result = subprocess.run(args, check=False)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", "-Version", required=True)
    parser.add_argument("--sha256", "-Sha256", required=True)
    args = parser.parse_args(argv[1:])

    root = Path(__file__).resolve().parent
    package_root = root / "Build" / "Windows64" / args.version
    package_file = package_root / f"Microsoft.Direct3D.DXC.{args.version}.nupkg"
    archive_file = package_root / f"Microsoft.Direct3D.DXC.{args.version}.zip"
    extract_dir = package_root / "Extract"
    legacy_package_dir = package_root / "Package"
    tool_dir = package_root / "Tools" / "x64"
    dxc_exe = tool_dir / "dxc.exe"
    package_url = f"https://www.nuget.org/api/v2/package/Microsoft.Direct3D.DXC/{args.version}"

    package_root.mkdir(parents=True, exist_ok=True)

    if package_file.exists() and sha256_hex(package_file) != args.sha256.upper():
        package_file.unlink()

    if not package_file.exists():
        print(f"Downloading Microsoft.Direct3D.DXC {args.version}")
        urllib.request.urlretrieve(package_url, package_file)

    if sha256_hex(package_file) != args.sha256.upper():
        raise RuntimeError("Microsoft.Direct3D.DXC package hash mismatch.")

    if not dxc_exe.exists():
        shutil.rmtree(extract_dir, ignore_errors=True)
        shutil.rmtree(legacy_package_dir, ignore_errors=True)
        shutil.rmtree(tool_dir, ignore_errors=True)
        if archive_file.exists():
            archive_file.unlink()

        extract_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(package_file, archive_file)

        with zipfile.ZipFile(archive_file) as archive:
            archive.extractall(extract_dir)

        extracted_tool_dir = extract_dir / "build" / "native" / "bin" / "x64"
        if not (extracted_tool_dir / "dxc.exe").exists():
            raise RuntimeError("dxc.exe was not found inside the Microsoft.Direct3D.DXC package.")

        tool_dir.mkdir(parents=True, exist_ok=True)
        for item in extracted_tool_dir.iterdir():
            target = tool_dir / item.name
            if item.is_dir():
                shutil.copytree(item, target, dirs_exist_ok=True)
            else:
                shutil.copy2(item, target)

        shutil.rmtree(extract_dir, ignore_errors=True)
        if archive_file.exists():
            archive_file.unlink()

    if legacy_package_dir.exists():
        shutil.rmtree(legacy_package_dir, ignore_errors=True)
    if extract_dir.exists():
        shutil.rmtree(extract_dir, ignore_errors=True)

    if not dxc_exe.exists():
        raise RuntimeError("dxc.exe was not found after extracting Microsoft.Direct3D.DXC.")

    print(f"DXC ready: {dxc_exe}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
