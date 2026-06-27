#!/usr/bin/env python3
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


def run(*args: str) -> None:
    result = subprocess.run(args, check=False)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def test_source(source_dir: Path) -> bool:
    return (source_dir / "CMakeLists.txt").exists() and (source_dir / "spirv_cross.hpp").exists() and (source_dir / "main.cpp").exists()


def expand_archive(archive_path: Path, source_dir: Path) -> None:
    if not archive_path.exists():
        raise RuntimeError(f"SPIRV-Cross archive was not found: {archive_path}")

    temp_dir = source_dir.parent / "Source.extracting"
    shutil.rmtree(temp_dir, ignore_errors=True)
    temp_dir.mkdir(parents=True, exist_ok=True)

    try:
        with zipfile.ZipFile(archive_path) as archive:
            archive.extractall(temp_dir)

        extracted_roots = [item for item in temp_dir.iterdir() if item.is_dir()]
        if len(extracted_roots) != 1:
            raise RuntimeError(f"Expected archive to contain one root directory, found {len(extracted_roots)}.")

        extracted_root = extracted_roots[0]
        if not test_source(extracted_root):
            raise RuntimeError("Archive does not contain a valid SPIRV-Cross source tree.")

        shutil.rmtree(source_dir, ignore_errors=True)
        shutil.move(str(extracted_root), str(source_dir))
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--configuration", default="Release")
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args(argv[1:])

    root = Path(__file__).resolve().parent
    source_dir = root / "Source"
    build_dir = root / "Build" / "Windows64" / "vulkan-sdk-1.4.309.0"
    archive_path = root / "SPIRV-Cross-MoltenVK-1.1.5.zip"
    built_exe = build_dir / args.configuration / "spirv-cross.exe"

    if source_dir.exists() and not test_source(source_dir):
        shutil.rmtree(source_dir, ignore_errors=True)
    if not source_dir.exists():
        expand_archive(archive_path, source_dir)
    elif not test_source(source_dir):
        print(f"SPIRV-Cross source is incomplete, recreating from archive: {source_dir}")
        shutil.rmtree(source_dir, ignore_errors=True)
        expand_archive(archive_path, source_dir)

    if not test_source(source_dir):
        raise RuntimeError(f"SPIRV-Cross source exists but is not valid: {source_dir}")

    if args.skip_build:
        print(f"SPIRV-Cross source ready: {source_dir}")
        return 0

    run(
        "cmake",
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        "-G",
        "Visual Studio 17 2022",
        "-A",
        "x64",
        "-T",
        "v143",
        "-DSPIRV_CROSS_CLI=ON",
        "-DSPIRV_CROSS_ENABLE_TESTS=OFF",
        "-DSPIRV_CROSS_ENABLE_GLSL=ON",
        "-DSPIRV_CROSS_ENABLE_MSL=ON",
        "-DSPIRV_CROSS_ENABLE_REFLECT=ON",
        "-DSPIRV_CROSS_ENABLE_UTIL=ON",
        "-DSPIRV_CROSS_SHARED=OFF",
    )
    run("cmake", "--build", str(build_dir), "--config", args.configuration, "--target", "spirv-cross")

    if not built_exe.exists():
        raise RuntimeError(f"SPIRV-Cross build completed, but spirv-cross.exe was not found: {built_exe}")

    print(f"SPIRV-Cross ready: {built_exe}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
