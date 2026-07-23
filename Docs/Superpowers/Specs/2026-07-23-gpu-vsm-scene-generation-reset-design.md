# GPU VSM Scene Generation Reset Design

## Problem

Each persistent render view owns a `VirtualShadowViewCache`. The cache correctly survives normal camera movement and camera cuts so compatible absolute virtual pages can be reused.

Entering Play mode and stopping Play mode replace the active `Scene` and `RTScene`, but the Editor currently reports only a camera cut to the Scene and Game render views. GPU VSM therefore keeps page mappings, physical page contents, and caster invalidation history that belong to the previous scene generation.

Pages outside the current camera working region remain dormant. After Stop, moving the Scene camera can request those pages again and expose stale Play-mode contents as triangular or page-sized shadow fragments.

## Selected Approach

Add an explicit virtual-shadow cache revision to each persistent render view.

This revision represents a wholesale change to the scene contents consumed by the view. It is independent from the existing camera-cut revision:

- Camera cuts preserve compatible VSM pages.
- Scene-content revision changes reset VSM mappings and invalidation history.

The explicit revision is preferred over comparing `RTScene` addresses because it has stable semantics and cannot be confused by allocator address reuse. It is also preferred over flushing on every camera cut because ordinary camera movement and cuts should retain cache reuse.

## Data Flow

1. `RenderViewState` exposes a scene-content-change request that atomically increments a revision stored by `RTRenderViewState`.
2. The Editor requests a scene-content change for both Scene View and Game View after successful wholesale scene replacement, including:
   - entering Play;
   - stopping Play;
   - opening or reloading a scene;
   - reloading a scene after script compilation.
3. Player-side scene loading requests the same change on its persistent render view.
4. `BaseRenderer` passes the revision into `VirtualShadowViewCache` while preparing the frame.
5. When the cache observes a new revision, it:
   - clears caster invalidation tracking;
   - invalidates CPU fallback page contents and request history;
   - sets `resetGpuCache` for the next enabled GPU VSM frame.
6. The existing GPU clear pass discards every physical-page mapping. Normal receiver marking and allocation then rebuild only the pages needed by the current view.

The revision must remain pending across frames where VSM cannot submit work, such as a temporarily missing shadow-casting light or invalid camera projection. The first subsequent valid VSM frame performs the reset.

## Scope

This change does not alter:

- clipmap selection;
- physical atlas dimensions;
- receiver page marking;
- page allocation policy;
- page caster clipping;
- the rule that ordinary camera cuts preserve compatible pages.

The previously proposed page-clipping work is not part of this fix.

## Testing

Add focused unit coverage for the cache lifecycle:

- a stable scene-content revision preserves cached GPU mappings;
- a camera-cut revision change alone does not reset GPU mappings;
- a scene-content revision change forces a GPU cache reset even when the camera, light, caster IDs, revisions, and bounds are otherwise identical;
- a revision change observed during an unrenderable frame remains pending until a valid enabled frame.

Run the virtual-shadow unit executable, the relevant Windows build, and an Editor regression using:

1. open `DemoProject`;
2. keep Scene View stationary;
3. enter Play and let physics objects move;
4. stop Play;
5. move the Scene camera across the previous working-region boundary;
6. verify that no stale triangular or page-sized shadow fragments appear.
