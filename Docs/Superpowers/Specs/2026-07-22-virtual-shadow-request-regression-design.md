# Virtual Shadow Request Regression Design

## Goal

Measure and correct the first confirmed content-side cause of the DemoProject virtual-shadow request explosion without changing the VSM cache or rendering algorithm in the same experiment.

## Evidence

- The stationary DemoProject produces about 17,800 requests for Scene View and 18,100 for Game View every frame.
- `ResolveRequests` dominates steady-state render-thread time because most requests cannot fit in the 256/1024-page physical caches.
- The Ground uses a unit cube mesh with local vertices in `[-0.5, 0.5]`, a transform scale of `(16, 0.5, 10)`, and serialized mesh bounds extents of `(8, 0.25, 5)`.
- Render preparation treats serialized bounds as local bounds and transforms them by the object transform. The Ground bounds are therefore scaled twice and become approximately `(128, 0.125, 50)` in world-space extents instead of `(8, 0.25, 5)`.

## First Repair

Change only the DemoProject Ground mesh bounds extents to `(0.5, 0.5, 0.5)`. Keep its transform scale unchanged. This makes the bounds match the unit cube mesh and produces the intended world-space extents after the normal render-world transform.

Do not change virtual resolution, clipmap count, shadow distance, physical atlas sizes, page-cache behavior, or shader sampling in this experiment.

## Verification

Open the same DemoProject with the existing temporary VSM diagnostics and compare:

- unique requested pages;
- missing pages;
- request-build and request-resolve time;
- VSM preparation time for both isolated views;
- frame cadence from the 30-frame log interval.

After collecting the comparison, remove the temporary timing/statistics logs and rebuild the Editor. This content configuration change does not add an engine- or RHI-bound unit test.
