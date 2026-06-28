#!/usr/bin/env python3
"""Third-party dependency setup entry point."""

from __future__ import annotations

import platform
import subprocess
import sys
from pathlib import Path


def run(script: Path, *args: str) -> int:
    return subprocess.call([sys.executable, str(script), *args], cwd=script.parent)


def get_host_platform() -> str:
    return platform.system()


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
