#!/usr/bin/env python3
"""Validate the vendored imgui-node-editor source snapshot."""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path


REVISION = "55a7dbf"
EXPECTED_SHA256 = {
    "LICENSE": "2d176cd30f8dc07a5b8e922fb0de308ffea7b8f2d7ff5a5f2ee99b562ed05b0c",
    "crude_json.cpp": "e81eecfe7d55968b5369e57d7d3d38fda415d641aee068c25aac21ef4a892b93",
    "crude_json.h": "68120f5ff1f87379c53c02143c9999d4b511545ccf69f7964fb008344edd26f7",
    "imgui_bezier_math.h": "f2e111af449e782f00fa75212ddbf4758f32bca5abd2450892a1c83793850373",
    "imgui_bezier_math.inl": "122864804b047b5e071459b748dfa9b549202c1031ba2d9c1e5d3167592ffdf2",
    "imgui_canvas.cpp": "a62c9c18803d8976fbee1791c1f5b30377a7a8f0b1380f294209469a629822fc",
    "imgui_canvas.h": "0612018954976400b86894582037531077532fc02c548db70fc100bc847bfc51",
    "imgui_extra_math.h": "c96ca0a272bf4c966b2b99bb738705f0475411b3d8d017f366278fcae5b5349e",
    "imgui_extra_math.inl": "fec446094f474b96095f0c3bd43e0397e6b1cef3de3c9d432567f7ef7b1305b5",
    "imgui_node_editor.cpp": "91524ee17ecffd6fc5d2e9e809cea1a99e480f9eece268294c30dcb116703ae8",
    "imgui_node_editor_api.cpp": "4b6db23bc0ea245ee6b65b3a20ad6750ea3f1e6abdf4551df1c245943f0758c7",
    "imgui_node_editor.h": "82e72d2b1acf87d0b57d220f48023b59c39ed0b52d894c3b228b9266de010fdb",
    "imgui_node_editor_internal.h": "50e20d87b0ee2f1fee2079b1244c4b388bd82dc753833d59c7fff4fc0d60b25d",
    "imgui_node_editor_internal.inl": "545c91e1a0c19326661734440a98ffc78ee0e9b628248320feb804ec88c7c2c4",
}


def main(argv: list[str]) -> int:
    source = Path(__file__).resolve().parent / f"imgui-node-editor-{REVISION}"
    missing = [source / filename for filename in EXPECTED_SHA256 if not (source / filename).is_file()]
    if missing:
        print("Missing imgui-node-editor source files:", file=sys.stderr)
        for path in missing:
            print(f"  {path}", file=sys.stderr)
        return 1

    mismatches: list[tuple[Path, str, str]] = []
    for filename, expected_hash in EXPECTED_SHA256.items():
        path = source / filename
        actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        if actual_hash != expected_hash:
            mismatches.append((path, expected_hash, actual_hash))

    if mismatches:
        print("imgui-node-editor source hash mismatches:", file=sys.stderr)
        for path, expected_hash, actual_hash in mismatches:
            print(f"  {path}", file=sys.stderr)
            print(f"    expected: {expected_hash}", file=sys.stderr)
            print(f"    actual:   {actual_hash}", file=sys.stderr)
        return 1

    print(f"imgui-node-editor vendored source ready: {source}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
