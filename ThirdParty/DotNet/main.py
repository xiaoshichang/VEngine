#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path


DOTNET_RUNTIME_VERSION = "10.0.9"
DOTNET_RUNTIME_RID = "win-x64"
DOTNET_RUNTIME_FILE_NAME = "dotnet-runtime-win-x64.zip"
RELEASE_METADATA_URL = "https://builds.dotnet.microsoft.com/dotnet/release-metadata/10.0/releases.json"


def sha512_hex(path: Path) -> str:
    digest = hashlib.sha512()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().lower()


def run(*args: str) -> None:
    result = subprocess.run(args, check=False)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def test_expected_hash(path: Path, expected_hash: str) -> bool:
    return path.exists() and sha512_hex(path) == expected_hash.lower()


def main(_: list[str]) -> int:
    root = Path(__file__).resolve().parent
    download_root = root / "Downloads"
    extract_root = root / "Extract"
    runtime_root = root / DOTNET_RUNTIME_RID / DOTNET_RUNTIME_VERSION
    archive_file = download_root / f"dotnet-runtime-{DOTNET_RUNTIME_VERSION}-{DOTNET_RUNTIME_RID}.zip"
    extract_dir = extract_root / f"{DOTNET_RUNTIME_VERSION}-{DOTNET_RUNTIME_RID}"
    dotnet_exe = runtime_root / "dotnet.exe"
    host_root = runtime_root / "host" / "fxr"
    shared_root = runtime_root / "shared" / "Microsoft.NETCore.App" / DOTNET_RUNTIME_VERSION
    coreclr_dll = shared_root / "coreclr.dll"

    if dotnet_exe.exists() and coreclr_dll.exists() and host_root.exists():
        hostfxr = next(host_root.rglob("hostfxr.dll"), None)
        if hostfxr is not None:
            print(f".NET runtime ready: {runtime_root}")
            return 0

    print(f"Reading .NET {DOTNET_RUNTIME_VERSION} release metadata.")
    metadata = json.loads(urllib.request.urlopen(RELEASE_METADATA_URL).read().decode("utf-8"))
    release = next((item for item in metadata["releases"] if item["release-version"] == DOTNET_RUNTIME_VERSION), None)
    if release is None:
        raise RuntimeError(f".NET release {DOTNET_RUNTIME_VERSION} was not found.")

    runtime_file = next(
        (
            item
            for item in release["runtime"]["files"]
            if item["rid"] == DOTNET_RUNTIME_RID and item["name"] == DOTNET_RUNTIME_FILE_NAME
        ),
        None,
    )
    if runtime_file is None:
        raise RuntimeError("Runtime file metadata not found.")

    expected_hash = runtime_file["hash"]
    runtime_url = runtime_file["url"]
    download_root.mkdir(parents=True, exist_ok=True)

    if archive_file.exists() and not test_expected_hash(archive_file, expected_hash):
        archive_file.unlink()

    if not archive_file.exists():
        print(f"Downloading .NET runtime {DOTNET_RUNTIME_VERSION} for {DOTNET_RUNTIME_RID}")
        urllib.request.urlretrieve(runtime_url, archive_file)

    if not test_expected_hash(archive_file, expected_hash):
        raise RuntimeError("Runtime package hash mismatch.")

    shutil.rmtree(extract_dir, ignore_errors=True)
    shutil.rmtree(runtime_root, ignore_errors=True)
    extract_dir.mkdir(parents=True, exist_ok=True)
    runtime_root.mkdir(parents=True, exist_ok=True)

    try:
        with zipfile.ZipFile(archive_file) as archive:
            archive.extractall(extract_dir)

        if not (extract_dir / "dotnet.exe").exists():
            raise RuntimeError("dotnet.exe was not found inside the .NET runtime archive.")

        for item in extract_dir.iterdir():
            target = runtime_root / item.name
            if item.is_dir():
                shutil.copytree(item, target, dirs_exist_ok=True)
            else:
                shutil.copy2(item, target)
    finally:
        shutil.rmtree(extract_dir, ignore_errors=True)

    if not (dotnet_exe.exists() and coreclr_dll.exists() and host_root.exists()):
        raise RuntimeError(f".NET runtime was not ready after extraction: {runtime_root}")

    print(f".NET runtime ready: {runtime_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
