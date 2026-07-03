#!/usr/bin/env python3
"""Third-party dependency setup entry point."""

from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path


def run(script: Path, *args: str) -> int:
    return subprocess.call([sys.executable, str(script), *args], cwd=script.parent)


def get_host_platform() -> str:
    return platform.system()


def get_ios_deployment_target() -> str:
    return os.environ.get("VE_IOS_DEPLOYMENT_TARGET", "17.0") or "17.0"


def is_valid_ios_deployment_target(value: str) -> bool:
    if not value or value.startswith(".") or value.endswith("."):
        return False

    has_dot = False
    previous_dot = False
    for character in value:
        if character == ".":
            if previous_dot:
                return False

            has_dot = True
            previous_dot = True
            continue

        if character < "0" or character > "9":
            return False

        previous_dot = False

    return has_dot


def validate_ios_deployment_target() -> int:
    deployment_target = get_ios_deployment_target()
    if is_valid_ios_deployment_target(deployment_target):
        return 0

    print(f"Invalid VE_IOS_DEPLOYMENT_TARGET for iOS third-party setup: {deployment_target}. Use a numeric version such as 17.0.")
    return 1


def build_default_steps(root: Path) -> list[tuple[str, list[Path | str]]]:
    host_platform = get_host_platform()

    if host_platform == "Darwin":
        boost_target = "Mac"
    else:
        boost_target = "Windows64"

    steps: list[tuple[str, list[Path | str]]] = [
        ("boost", [root / "Boost" / "main.py", "1.85.0", boost_target]),
        ("slang", [root / "Slang" / "main.py"]),
        ("spirv-cross", [root / "SPIRV-Cross" / "main.py"]),
        ("jolt", [root / "Jolt" / "main.py", "--build-tests-and-demos"]),
    ]

    if host_platform == "Darwin":
        steps.insert(1, ("imgui", [root / "ImGui" / "main.py"]))
        steps.insert(2, ("dotnet", [root / "DotNet" / "main.py"]))
    else:
        steps.insert(1, ("directxshadercompiler", [root / "DirectXShaderCompiler" / "main.py"]))
        steps.insert(3, ("dotnet", [root / "DotNet" / "main.py"]))
        steps.insert(4, ("windowssdktools", [root / "WindowsSdkTools" / "main.py"]))

    return steps


def build_ios_steps(root: Path) -> list[tuple[str, list[Path | str]]]:
    return [
        ("boost-ios", [root / "Boost" / "main.py", "1.85.0", "IOS"]),
        ("jolt-source", [root / "Jolt" / "main.py"]),
    ]


def validate_ios_host() -> int:
    if get_host_platform() != "Darwin":
        print("iOS third-party setup must run on macOS because it requires Xcode and Apple iOS SDKs.")
        return 1

    if shutil.which("xcodebuild") is None:
        print("iOS third-party setup requires xcodebuild. Install Xcode and select it with xcode-select before running ThirdParty/Build_IOS.sh.")
        return 1

    return 0


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parent
    if len(argv) == 1:
        steps = build_default_steps(root)

        for name, command in steps:
            print(f"==> {name}")
            code = subprocess.call([sys.executable, *map(str, command)], cwd=command[0].parent)
            if code != 0:
                return code

        return 0

    dependency = argv[1].lower()
    args = argv[2:]

    if dependency == "ios":
        validation_result = validate_ios_deployment_target()
        if validation_result != 0:
            return validation_result

        validation_result = validate_ios_host()
        if validation_result != 0:
            return validation_result

        for name, command in build_ios_steps(root):
            print(f"==> {name}")
            code = subprocess.call([sys.executable, *map(str, command)], cwd=command[0].parent)
            if code != 0:
                return code
        return 0

    if dependency == "boost":
        return run(root / "Boost" / "main.py", *args)
    if dependency == "imgui":
        return run(root / "ImGui" / "main.py", *args)
    if dependency == "directxshadercompiler":
        return run(root / "DirectXShaderCompiler" / "main.py", *args)
    if dependency == "dotnet":
        return run(root / "DotNet" / "main.py", *args)
    if dependency == "jolt":
        return run(root / "Jolt" / "main.py", *args)
    if dependency == "windowssdktools":
        return run(root / "WindowsSdkTools" / "main.py", *args)
    if dependency == "slang":
        return run(root / "Slang" / "main.py", *args)
    if dependency == "spirv-cross":
        return run(root / "SPIRV-Cross" / "main.py", *args)

    print(f"Unknown dependency: {argv[1]}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
