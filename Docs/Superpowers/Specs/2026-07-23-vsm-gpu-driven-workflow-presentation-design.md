# VEngine GPU Driven VSM Workflow Presentation Design

## Communication Goal

By the end of the presentation, VEngine technical leads and developers should understand how the current Windows GPU-driven directional-light Virtual Shadow Maps path turns camera depth into same-frame shadow pages, why the path removes CPU page-request readback, and which parts are implemented or planned.

## Audience And Tone

- Audience: internal technical sharing for technical leads and developers.
- Tone: technically rigorous, implementation-grounded, and concise enough for a live walkthrough.
- Language: Simplified Chinese for audience-facing copy; engine types, pass names, shader stages, and source paths remain in English.
- Target length: approximately 20 widescreen slides.

## Narrative

Use an end-to-end workflow as the primary narrative. Introduce the CPU-to-GPU motivation briefly, establish the virtual-page and clipmap model, then follow the current RenderGraph pass order from receiver depth through page marking, compaction, coarse-to-fine cache resolution and allocation, physical-page rendering, finalization, and forward sampling. Close with cache invalidation, GPU-only platform behavior, runtime evidence, and current follow-up work.

## Slide Structure

1. Cover: VEngine GPU Driven Virtual Shadow Maps.
2. Executive workflow: inputs, GPU workflow, and shadowed output.
3. Motivation: CPU request and per-page draw construction do not scale cleanly.
4. VSM fundamentals: 16K virtual resolution, 128-by-128 pages, and a bounded physical atlas.
5. Four clipmap levels: fine near coverage with coarse fallback.
6. Current implementation boundary: one directional light, opaque rigid meshes, Windows GPU path.
7. Per-view resources: page marks, compact request list and counts, dense page table, physical metadata, and atlas.
8. RenderGraph overview: receiver depth through forward scene rendering.
9. Receiver depth prepass.
10. Clear and reset: clear frame-local flags, preserve compatible mappings, and apply invalidation keys.
11. Mark pages: reconstruct world positions from camera depth and mark center pages.
12. Compact requests: expand conservatively and append unique per-level requests.
13. Resolve cache hits: process clipmap levels from coarse to fine.
14. Allocate cache misses: free pages, bounded age-based eviction, and missing-entry fallback.
15. Render pages without CPU readback: instanced page clear and caster draws across physical capacity.
16. Finalize and forward sample: dense lookup, atlas addressing, gutter-safe PCF, and coarse fallback.
17. Cross-frame cache and dynamic invalidation for moved, added, removed, or disabled casters.
18. Engineering safeguards: D3D11 and D3D12 GPU path, failure-triggered VSM disablement, Metal unsupported status, and per-view isolation.
19. Runtime verification: DemoProject screenshots and acceptance scenarios.
20. Summary and follow-up work: GPU Scene, indirect draw compaction, diagnostics, and native Metal compute.

## Visual System

- Format: 16:9 widescreen.
- Background: deep navy with subtle grid or page motifs.
- Primary GPU flow color: cyan.
- Cache-hit and reuse color: green.
- Allocation and eviction color: amber.
- Invalidation and fallback color: red.
- Planned or unsupported work: desaturated gray.
- Titles: at least 35 pt; deck title at least 50 pt; body copy at least 16 pt.
- Flow diagrams use editable PowerPoint shapes. One core visual carries each slide.
- Slides 8 through 16 reuse the same workflow vocabulary while highlighting only the active stage.
- Source excerpts remain short and use a monospaced font with an adjacent plain-language interpretation.

## Evidence And Source Policy

- Ground all implementation claims in the current repository state.
- Prefer current source code over older design intentions when they differ.
- Use `GpuVirtualShadowRenderPass.cpp`, `VirtualShadowViewCache.cpp`, `VirtualShadowTypes.h`, `BaseRenderer.cpp`, and `BasicMesh.hlsl` as primary implementation sources.
- Use the GPU-driven VSM design and development-plan sections only for context and declared follow-up work.
- Include three to five real Editor or DemoProject screenshots when the current build can be launched reliably with `--project`.
- Do not invent performance gains, profiler counters, debug views, or visual output that the current build does not expose.

## Current-State Accuracy

- The current GPU RenderGraph includes receiver depth, clear, mark, compact, four coarse-to-fine resolve/allocate pairs, physical-page rendering, and finalization.
- Page rendering uses instancing across physical-page capacity rather than GPU-to-CPU request readback.
- The dense page table is the only page-table path sampled by the forward shader.
- D3D11 and D3D12 use the GPU path when resources and pipelines are available.
- Metal currently has no VSM because native GPU-driven compute encoding has not been completed.
- Diagnostics counters, dedicated page-debug views, native Metal compute, GPU Scene, and indirect caster submission are follow-up work.

## Acceptance Criteria

- Deliver one editable `.pptx` with approximately 20 slides.
- Preserve a coherent end-to-end workflow narrative.
- Include diagrams, source-grounded callouts, and real runtime imagery where feasible.
- Render and inspect every slide.
- Fix unintended overlaps, clipping, broken connectors, and title wrapping.
- Run slide overflow validation before delivery.
