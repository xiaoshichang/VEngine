# VSM Fail-Fast Simplification Design

Date: 2026-07-28

## Status And Scope

This design makes Virtual Shadow Maps mandatory for every rendered view family and removes the VSM disabled, placeholder, and recoverable transaction models.

It supersedes the placeholder-binding, disabled-backend, pipeline fallback, and prepared-frame transaction sections of
`2026-07-28-view-family-frame-graph-renderer-design.md`. The view-family renderer, shared scene cache, global physical-page pool, FrameGraph pass decomposition, and per-view page-table slices remain unchanged.

The priority is a small and explicit model. Recovery from rendering failures is intentionally out of scope.

## Mandatory VSM Contract

Every renderer view family must have:

- a valid scene;
- one selected shadow-casting directional light;
- a supported VSM backend;
- a valid camera for every renderer view;
- a completed generic DepthPrePass output for every renderer view;
- all required VSM shaders, pipelines, buffers, textures, samplers, and uniform allocations.

The absence of any required item is an unrecoverable error. This includes:

- Metal, until the Metal VSM implementation is added;
- no directional light;
- a directional light with shadows disabled;
- missing camera or receiver depth;
- invalid view identity or page-table slice;
- resource, shader, pipeline, descriptor, uniform, command-recording, graph-compilation, graph-execution, or submission failure.

The failure path logs an error with relevant context and invokes the engine's always-on assertion/termination path. It does not disable shadows, publish fallback state, retry, or continue scene rendering.

## Data Model

`VirtualShadowViewResult` contains only usable VSM sampling data:

- the prepared frame packet;
- the real scene atlas;
- the comparison sampler;
- the real scene page-table buffer;
- the view's page-table offset and size.

The following concepts are removed:

- `enabled` and equivalent disabled-state flags;
- placeholder or fallback atlas fields;
- placeholder or fallback page-table fields;
- placeholder resource publication helpers;
- manager-owned placeholder texture, sampler, and page table;
- scene-cache placeholder atlas;
- disabled or mixed-family normalization.

Every view in a family is VSM-active. Opaque and Transparent passes require valid final atlas and page-table handles and terminate if their contracts are violated.

Shaders always sample valid VSM data. The `virtualShadowEnabled` uniform and shader-side disabled-shadow early return are removed.

## FrameGraph Flow

The renderer builds one graph for the family in this order:

1. register one generic DepthPrePass for each view;
2. prepare the family VSM state;
3. import the real scene-shared VSM resources once;
4. register request, residency, page rendering, finalization, and statistics-copy passes;
5. publish the final atlas and per-view page-table handles;
6. register Opaque and Transparent consumers;
7. register the product output pass.

`VirtualShadowManager::AddToFrameGraph` has one successful path. It does not have an unsupported-backend branch, a no-light branch, a disabled-shadow branch, or a placeholder-return branch.

Standalone, Mobile, Editor, and Player use this same topology. Mobile running on Metal terminates at VSM graph construction until Metal support exists.

## Scene Cache Lifecycle

FrameGraph preparation updates the VSM scene cache directly. Those updates are final for the process:

- there is no prepared-frame ticket;
- there is no begin/commit/abort prepared-frame protocol;
- there is no rollback guard;
- there is no same-frame retry;
- there is no recovery after graph or submission failure.

The following types and APIs are removed:

- `VirtualShadowPreparedFrameTicket`;
- `VirtualShadowPreparedFrameScope`;
- `BeginPreparedFrame`;
- `CommitPreparedFrame`;
- `AbortPreparedFrame`;
- `RollbackFramePreparation`;
- `FrameRenderPipelineData::virtualShadowPreparedFrame`;
- RenderSystem commit/abort coordination for VSM.

If a later graph, command-list, or queue operation fails, the program terminates. Partially updated cache state is therefore never consumed by a subsequent frame.

## Statistics Readback Lifecycle

Statistics retain only the state required for asynchronous GPU readback:

1. select a readback slot using the frame index;
2. assert that the slot's previous fence has completed before reuse;
3. record the exact statistics buffer copy in the FrameGraph;
4. after successful submission, associate the submitted fence with the slot;
5. read the slot only after that fence completes.

This is a one-way GPU lifetime, not a recoverable transaction. Statistics fallback and abort state are removed, including `BeginFrameStatistics`, `AbortFrameStatistics`, unavailable-result publication, and retry behavior.

Submission failure terminates the program, so no abort API is required.

## Error Handling

VSM implementation functions use `void` unless a value is part of their successful result. Expected internal failures do not return `ErrorCode` or `bool`.

Each failure site:

1. logs one actionable error including the pass, view, resource, or backend involved;
2. invokes an always-on assertion or a common non-returning VSM failure helper;
3. terminates the program.

The focused FrameGraph pass callbacks remain `void`. No failure flag, fallback result, or disabled snapshot is introduced for VSM.

Legacy VSM adapters and their `ErrorCode` paths are deleted as part of the remaining renderer cleanup.

## Validation

Tests focus on the successful mandatory path:

- D3D11 and D3D12 build and execute the complete VSM graph;
- a two-view family shares one scene cache, atlas, and physical-page pool;
- views retain distinct page-table slices;
- every view has one DepthPrePass output;
- request, residency, page rendering, finalization, and readback dependencies use the exact produced resource versions;
- a real caster draw exercises shader/pipeline binding, object uniforms, vertex/index buffers, and indexed drawing;
- final atlas and page-table handles are consumed by Opaque and Transparent;
- statistics readback remains fence-delayed and performs no wait during graph execution;
- no placeholder resource, disabled snapshot, prepared ticket, commit, abort, rollback, or legacy pre-render scheduling symbol remains.

No new death-test framework is introduced. Fail-fast branches are verified through always-on invariant structure, focused code review, and supported-backend success tests.

## Deferred Work

- Metal VSM implementation;
- optional shadows or scenes without a shadow-casting directional light;
- device-loss recovery;
- shader or pipeline fallback;
- resource-allocation recovery;
- cache rollback or frame retry.

Any future optional-shadow feature must introduce an explicit shader and renderer topology rather than restoring descriptor placeholders.
