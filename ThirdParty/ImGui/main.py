#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path


def validate_source(root: Path) -> None:
    required_files = [
        root / "imgui.cpp",
        root / "imgui.h",
        root / "imgui_demo.cpp",
        root / "imgui_draw.cpp",
        root / "imgui_tables.cpp",
        root / "imgui_widgets.cpp",
    ]

    if sys.platform == "darwin":
        required_files.extend(
            [
                root / "backends" / "imgui_impl_osx.h",
                root / "backends" / "imgui_impl_osx.mm",
                root / "backends" / "imgui_impl_metal.h",
                root / "backends" / "imgui_impl_metal.mm",
            ]
        )
    else:
        required_files.extend(
            [
                root / "backends" / "imgui_impl_win32.cpp",
                root / "backends" / "imgui_impl_dx11.cpp",
                root / "backends" / "imgui_impl_dx12.cpp",
            ]
        )

    missing = [str(path) for path in required_files if not path.exists()]
    if missing:
        raise RuntimeError("Missing Dear ImGui source files:\n" + "\n".join(missing))


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parent / "imgui-1.92.8"
    validate_source(root)
    print(f"Dear ImGui vendored source ready: {root}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
