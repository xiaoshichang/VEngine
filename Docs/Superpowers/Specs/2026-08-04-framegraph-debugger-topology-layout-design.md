# FrameGraph Debugger Topology Layout Design

**Date:** 2026-08-04

**Status:** Approved

## Scope

This design refines the existing FrameGraph Debugger topology view. It changes only automatic node placement, pin presentation, and the visible color legend. Capture behavior, snapshot ownership, preview generation, filtering, tables, and details remain unchanged.

## Left-To-Right Dependency Layout

The initial layout uses dependency depth rather than a fixed registration-order grid.

- Every visible pass is assigned a horizontal layer.
- Passes without visible predecessors start in layer zero.
- A pass with predecessors is assigned one layer after the maximum layer of its visible predecessors.
- Passes within the same layer are ordered vertically by compiled index and then registration index.
- Culled or uncompiled passes remain visible when `Show Culled` permits them. They are placed after executable layers while preserving registration order.
- The layout uses stable horizontal and vertical spacing so dependency links normally travel from left to right.
- Automatic placement runs once for a captured snapshot. User-dragged node positions are not overwritten on later UI frames.

The captured dependency graph is expected to follow FrameGraph execution order. If malformed data cannot produce a valid layered position, the panel fails closed through its existing topology validation rather than indexing invalid passes.

## Node And Pin Layout

Each pass node uses three visual columns:

1. Input pins on the left.
2. Pass name, pass type, and culled state in the center.
3. Output pins on the right.

Input pins represent consumed logical resource versions. Output pins represent produced logical resource versions. A read/write access therefore shows its input version on the left and its output version on the right. Duplicate pins for the same pass, resource, version, and direction remain suppressed.

Texture pins remain cyan. Buffer pins remain orange.

## Color Legend

A compact legend is rendered below the search/filter controls and above the node-editor canvas. It is outside the pannable and zoomable canvas so its meaning remains visible while inspecting the graph. The legend wraps when horizontal space is insufficient.

Node colors:

- Blue: Raster pass.
- Purple: Compute pass.
- Gray: Culled pass. Culled state takes precedence over pass type color.

Dependency-link colors:

- Green `RAW`: an earlier pass writes a resource version that a later pass reads.
- Yellow `WAR`: an earlier pass reads a resource version before a later pass writes.
- Red `WAW`: an earlier and a later pass both write the resource.

Pin colors:

- Cyan: Texture.
- Orange: Buffer.

## Testing And Verification

Focused tests cover:

- Dependency-depth calculation for a chain, fan-out, fan-in, and independent passes.
- Stable vertical ordering inside a layer.
- Placement of culled or uncompiled passes after executable layers.
- Input/output pin classification for read-only, write-only, and read/write accesses.
- Node, dependency-link, and pin color semantic mappings.

Windows verification includes building `VEngineWinEditor`, running the FrameGraph debug tests, and a D3D12 Editor capture using `--project D:\github-desktop\VEngine\DemoProject`. The runtime check confirms left-to-right flow, left input pins, right output pins, and a readable persistent legend.

## Non-Goals

- Changing capture or pause semantics.
- Changing dependency construction or hazard classification.
- Persisting user node positions between captures or Editor sessions.
- Adding graph-editing operations.
- Adding new node, link, or resource categories.
