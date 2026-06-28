#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import json
import platform
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


DOTNET_RUNTIME_VERSION = "10.0.9"
RELEASE_METADATA_URL = "https://builds.dotnet.microsoft.com/dotnet/release-metadata/10.0/releases.json"
REQUEST_TIMEOUT_SECONDS = 30
RUNTIME_PACKAGES = {
    "Windows": {
        "rid": "win-x64",
        "archive_name": "dotnet-runtime-win-x64.zip",
        "runtime_exe": "dotnet.exe",
        "hostfxr_name": "hostfxr.dll",
    },
    "Darwin": {
        "rid": "osx-arm64",
        "archive_name": "dotnet-runtime-osx-arm64.tar.gz",
        "runtime_exe": "dotnet",
        "hostfxr_name": "hostfxr.dylib",
    },
}


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


def run_capture(*args: str) -> str:
    result = subprocess.run(args, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "Command failed.")
    return result.stdout


def test_expected_hash(path: Path, expected_hash: str) -> bool:
    return path.exists() and sha512_hex(path) == expected_hash.lower()


def get_host_key() -> str:
    host_system = platform.system()
    if host_system not in RUNTIME_PACKAGES:
        raise RuntimeError(f"Unsupported host platform for .NET runtime setup: {host_system}")
    return host_system


def find_runtime_root(root: Path, rid: str) -> Path:
    return root / rid / DOTNET_RUNTIME_VERSION


def locate_runtime_artifacts(runtime_root: Path, runtime_exe_name: str, hostfxr_name: str) -> bool:
    runtime_exe = runtime_root / runtime_exe_name
    host_root = runtime_root / "host" / "fxr"
    if not runtime_exe.exists() or not host_root.exists():
        return False
    if platform.system() == "Darwin":
        return next(host_root.rglob("libhostfxr.dylib"), None) is not None
    return next(host_root.rglob(hostfxr_name), None) is not None


def fetch_text(url: str) -> str:
    if platform.system() == "Darwin":
        return run_capture("curl", "-fsSL", "--retry", "3", "--connect-timeout", str(REQUEST_TIMEOUT_SECONDS), url)

    import urllib.request
    from urllib.error import URLError

    try:
        with urllib.request.urlopen(url, timeout=REQUEST_TIMEOUT_SECONDS) as response:
            return response.read().decode("utf-8")
    except (URLError, TimeoutError) as error:
        raise RuntimeError("Failed to read .NET release metadata.") from error


def download_file(url: str, output_path: Path) -> None:
    if platform.system() == "Darwin":
        subprocess.run(
            ["curl", "-fL", "--retry", "3", "--connect-timeout", str(REQUEST_TIMEOUT_SECONDS), url, "-o", str(output_path)],
            check=True,
        )
        return

    import urllib.request
    from urllib.error import URLError

    try:
        urllib.request.urlretrieve(url, output_path)
    except (URLError, TimeoutError) as error:
        raise RuntimeError("Failed to download .NET runtime package.") from error


def main(_: list[str]) -> int:
    host_key = get_host_key()
    package = RUNTIME_PACKAGES[host_key]
    rid = package["rid"]
    root = Path(__file__).resolve().parent
    download_root = root / "Downloads"
    extract_root = root / "Extract"
    runtime_root = find_runtime_root(root, rid)
    archive_file = download_root / package["archive_name"]
    extract_dir = extract_root / f"{DOTNET_RUNTIME_VERSION}-{rid}"
    if locate_runtime_artifacts(runtime_root, package["runtime_exe"], package["hostfxr_name"]):
        print(f".NET runtime ready: {runtime_root}")
        return 0

    print(f"Reading .NET {DOTNET_RUNTIME_VERSION} release metadata.")
    try:
        metadata = json.loads(fetch_text(RELEASE_METADATA_URL))
    except RuntimeError as error:
        raise RuntimeError(
            "Failed to read .NET release metadata. Check network access and macOS trust store certificates."
        ) from error

    release = next((item for item in metadata["releases"] if item["release-version"] == DOTNET_RUNTIME_VERSION), None)
    if release is None:
        raise RuntimeError(f".NET release {DOTNET_RUNTIME_VERSION} was not found.")

    runtime_file = next(
        (
            item
            for item in release["runtime"]["files"]
            if item["rid"] == rid and item["name"] == package["archive_name"]
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
        print(f"Downloading .NET runtime {DOTNET_RUNTIME_VERSION} for {rid}")
        try:
            download_file(runtime_url, archive_file)
        except RuntimeError as error:
            raise RuntimeError(
                f"Failed to download .NET runtime package for {rid}. Check network access and certificate trust."
            ) from error

    if not test_expected_hash(archive_file, expected_hash):
        raise RuntimeError("Runtime package hash mismatch.")

    shutil.rmtree(extract_dir, ignore_errors=True)
    shutil.rmtree(runtime_root, ignore_errors=True)
    extract_dir.mkdir(parents=True, exist_ok=True)
    runtime_root.mkdir(parents=True, exist_ok=True)

    try:
        if archive_file.suffix == ".zip":
            with zipfile.ZipFile(archive_file) as archive:
                archive.extractall(extract_dir)
        else:
            subprocess.run(["tar", "xzf", str(archive_file), "-C", str(extract_dir), "--strip-components=1"], check=True)

        for item in extract_dir.iterdir():
            target = runtime_root / item.name
            if item.is_dir():
                shutil.copytree(item, target, dirs_exist_ok=True)
            else:
                shutil.copy2(item, target)
    finally:
        shutil.rmtree(extract_dir, ignore_errors=True)

    if not locate_runtime_artifacts(runtime_root, package["runtime_exe"], package["hostfxr_name"]):
        raise RuntimeError(f".NET runtime was not ready after extraction: {runtime_root}")

    print(f".NET runtime ready: {runtime_root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
