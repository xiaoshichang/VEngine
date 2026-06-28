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


def test_jolt_source(path: Path) -> bool:
    return (path / "Build" / "CMakeLists.txt").exists() and (path / "Jolt" / "Jolt.h").exists() and (path / "Jolt" / "Core" / "Core.h").exists()


def expand_archive(archive_path: Path, source_dir: Path) -> None:
    if not archive_path.exists():
        raise RuntimeError(f"Jolt Physics archive was not found: {archive_path}")

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
        if not test_jolt_source(extracted_root):
            raise RuntimeError("Archive does not contain a valid Jolt Physics source tree.")

        shutil.rmtree(source_dir, ignore_errors=True)
        shutil.move(str(extracted_root), str(source_dir))
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)


def normalize_targets(raw_targets: list[str]) -> list[str]:
    normalized: list[str] = []
    for raw_target in raw_targets:
        for target in raw_target.split(","):
            target = target.strip()
            if target:
                normalized.append(target)
    return normalized


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", default="v5.5.0")
    parser.add_argument("--archive", default="JoltPhysics-5.5.0.zip")
    parser.add_argument("--configuration", default="Debug")
    parser.add_argument("--build-tests-and-demos", action="store_true")
    parser.add_argument("--include-viewer", action="store_true")
    parser.add_argument("--targets", nargs="*", default=[])
    args = parser.parse_args(argv[1:])

    root = Path(__file__).resolve().parent
    source_dir = root / "Source"
    build_dir = root / "Build" / ("Mac" if sys.platform == "darwin" else "Windows64") / args.tag
    archive_path = root / args.archive

    if not source_dir.exists():
        expand_archive(archive_path, source_dir)
    elif not test_jolt_source(source_dir):
        print(f"Jolt Physics source is incomplete, recreating from archive: {source_dir}")
        shutil.rmtree(source_dir, ignore_errors=True)
        expand_archive(archive_path, source_dir)

    if not test_jolt_source(source_dir):
        raise RuntimeError(f"Jolt Physics Source exists but is not a valid source tree: {source_dir}")

    if not args.build_tests_and_demos:
        print(f"Jolt Physics source ready: {source_dir}")
        return 0

    build_targets = normalize_targets(args.targets)
    if not build_targets:
        build_targets = ["UnitTests", "HelloWorld", "PerformanceTest", "Samples"]
        if args.include_viewer:
            build_targets.append("JoltViewer")
    elif "All" in build_targets:
        build_targets = ["UnitTests", "HelloWorld", "PerformanceTest", "Samples", "JoltViewer"]

    build_unit_tests = "UnitTests" in build_targets
    build_hello_world = "HelloWorld" in build_targets
    build_performance_test = "PerformanceTest" in build_targets
    build_samples = "Samples" in build_targets
    build_viewer = "JoltViewer" in build_targets

    cmake_args = [
        "cmake",
        "-S",
        str(source_dir / "Build"),
        "-B",
        str(build_dir),
        "-DBUILD_SHARED_LIBS=OFF",
        "-DENABLE_INSTALL=OFF",
        "-DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF",
        f"-DTARGET_UNIT_TESTS={build_unit_tests}",
        f"-DTARGET_HELLO_WORLD={build_hello_world}",
        f"-DTARGET_PERFORMANCE_TEST={build_performance_test}",
        f"-DTARGET_SAMPLES={build_samples}",
        f"-DTARGET_VIEWER={build_viewer}",
    ]
    if sys.platform == "darwin":
        cmake_args.extend(["-G", "Xcode"])
    else:
        cmake_args.extend(["-G", "Visual Studio 17 2022", "-A", "x64", "-T", "v143"])
    run(*cmake_args)

    for target in build_targets:
        run("cmake", "--build", str(build_dir), "--config", args.configuration, "--target", target)

    print(f"Jolt Physics targets built: {', '.join(build_targets)}")
    print(f"Jolt Physics build directory: {build_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
