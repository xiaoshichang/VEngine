# Editor Performance Footer Design

## Goal

Use the Editor window's additional 50 pixels for a structured performance footer. Replace the current single-line FPS status bar with stable
Common, Render, VSM, and Physics regions. Populate Common and VSM now, while preserving explicit placeholders for later Render and Physics
statistics.

## Layout

Increase the footer height from 24 pixels to 74 pixels. The main editing content continues to end immediately above the footer, so the additional
window height is consumed by diagnostics rather than reducing the prior content area.

Render one ImGui table with vertical separators and four horizontal columns:

| Region | Width | Initial content |
|---|---:|---|
| Common | 18% | Average FPS |
| Render | 18% | Em dash placeholder |
| VSM | 46% | Physical-pool and current-frame page statistics |
| Physics | 18% | Em dash placeholder |

Each region has a subdued title and vertically stacked values. The compact VSM layout is:

```text
Physical: 2048 total | 783 allocated
Frame: 412 requested | 367 cached | 45 redraw
Alloc: 12 new | 0 unmapped
```

FPS retains the existing one-second average. VSM values represent the latest completed GPU frame and are not smoothed.

## VSM Metric Contract

Statistics aggregate the current `RTScene` and its complete `RenderViewFamily`. The physical pool is scene-shared; per-frame counters include all
views in family order.

- `totalPhysicalPages`: physical-page capacity of the scene cache.
- `allocatedPhysicalPages`: valid resident physical pages after finalization.
- `requestedPages`: unique logical-page requests generated for the complete family during the frame.
- `cachedPages`: requests resolved to already resident physical pages.
- `newlyAllocatedPages`: requests assigned a new or repurposed physical page during the frame.
- `redrawnPages`: physical pages rasterized during the frame because they were new or dirty.
- `unmappedPages`: requested logical pages that remained unmapped because no physical page was available.

Physical-pool exhaustion continues to use the existing unmapped-page rendering behavior. It does not disable the complete view or steal pinned pages
from an earlier view.

## GPU Collection

`VirtualShadowSceneCache` owns a compact GPU statistics buffer. The family pre-render step clears it once, and existing VSM stages update it with
atomic counters:

- Request compaction contributes requested pages.
- Hit resolution contributes cached pages.
- Allocation contributes newly allocated and unmapped pages.
- Render completion contributes redrawn pages.
- A final physical-page scan contributes allocated physical pages.

The total capacity is known from the scene cache and is copied into the published snapshot.

## Asynchronous Readback

Extend the common RHI with the minimum buffer-copy and completed-readback operations needed by the statistics path. D3D11 and D3D12 implement these
operations naturally with staging/readback resources.

Each in-flight `FrameContext` uses an independent readback slot. Recording copies the compact statistics buffer into that slot. CPU code reads only
after the context's existing completion fence has finished, so statistics introduce no new synchronous GPU wait. The displayed values normally lag
the frame currently being recorded by one or two frames.

Metal continues to report VSM statistics as unavailable while VSM itself is unsupported.

## Publication And Ownership

The Render Thread publishes a value-only `RenderPerformanceStatistics` snapshot containing a source frame index, availability state, and VSM
statistics. The Editor/Main Thread reads the latest completed snapshot through the RenderSystem facade and never accesses `RTScene`,
`VirtualShadowSceneCache`, readback buffers, or other RHI objects.

The snapshot structure leaves explicit Common, Render, VSM, and Physics extension points so later metrics do not require redesigning the footer.

Scene identity participates in publication. Statistics from a previous scene are not shown after the Editor switches scenes.

## Failure Behavior

- Before the first completed sample, the VSM region displays an em dash.
- No shadow-casting directional light, unsupported VSM, or no active scene produces an unavailable sample.
- A readback or statistics-resource failure drops that sample and does not fail rendering.
- A stale sample from another scene is rejected.
- Render and Physics display placeholders until their counters are implemented.

## Verification

- Unit tests cover metric meanings, unavailable state, scene identity, and thread-safe value snapshot publication.
- Recording-RHI tests verify clear, counter, finalization, and copy ordering.
- D3D11 and D3D12 smoke tests verify real statistics-buffer copy and completed readback.
- Editor tests cover the four-region footer data model and unavailable rendering.
- Full Windows test, Editor, and Player builds remain successful.
