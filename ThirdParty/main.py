#!/usr/bin/env python3
"""Third-party dependency setup entry point."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


def run(script: Path, *args: str) -> int:
    return subprocess.call([sys.executable, str(script), *args], cwd=script.parent)


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parent
    if len(argv) == 1:
        steps = [
            ("boost", [root / "Boost" / "main.py", "1.85.0", "Windows64"]),
            ("directxshadercompiler", [root / "DirectXShaderCompiler" / "main.py"]),
            ("slang", [root / "Slang" / "main.py"]),
            ("dotnet", [root / "DotNet" / "main.py"]),
            ("windowssdktools", [root / "WindowsSdkTools" / "main.py"]),
            ("spirv-cross", [root / "SPIRV-Cross" / "main.py"]),
            ("jolt", [root / "Jolt" / "main.py", "--build-tests-and-demos"]),
        ]

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
