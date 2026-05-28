# Lightweight Physics Query World Design

## Summary

This spec defines the first implementation slice for Milestone 9: a lightweight physics query world for collider
queries and future picking. It intentionally stops before rigid body simulation, fixed-step scheduling, collision
response, presentation interpolation, Editor viewport UI, and C# script bindings.

The slice adds:

- Geometry primitives and query helpers for ray, sphere, oriented box, and AABB.
- A serializable `ColliderComponent` with box and sphere shapes.
- A minimal `PhysicsWorld` that owns synchronized collider query proxies.
- Runtime query APIs over that synchronized query state: closest raycast, raycast-all, overlap sphere, and overlap box.
- Focused unit tests for geometry, scene synchronization, filtering, query ordering, and collider serialization.

The key design decision is that runtime physics queries read `PhysicsWorld` query state, not live
`TransformComponent` state and not render presentation poses. This keeps the first slice compatible with a future
Unity-style split between simulation pose, scene transform, and render-only interpolation.

## Goals

- Establish the `Engine/Runtime/Physics` module with small, explicit types.
- Support oriented box and sphere collider queries.
- Provide deterministic query behavior through a linear scan over synchronized collider proxies.
- Use 64-bit physics layers and masks from the start.
- Register `ColliderComponent` through Reflection and scene serialization.
- Enforce at most one `ColliderComponent` per `GameObject`.
- Preserve a clear path for future rigid body simulation and Editor picking without changing the public query model.

## Non-Goals

- `RigidBodyComponent`.
- Fixed-step scheduling.
- Gravity, velocity, force, torque, inertia, or collision response.
- Trigger or collision event dispatch.
- Physics broad-phase acceleration structures.
- Presentation pose interpolation or extrapolation.
- Editor viewport picking UI.
- Managed C# `Physics` APIs.
- Mesh, capsule, cylinder, convex hull, heightfield, terrain, or compound collider shapes.

## Architecture

The first slice introduces a query-only physics world:

```text
Scene / TransformComponent / ColliderComponent
  -> PhysicsWorld::SyncFromScene(scene)
      builds ColliderProxy records
  -> PhysicsWorld::RaycastClosest(ray, queryMask, includeTriggers)
  -> PhysicsWorld::RaycastAll(ray, queryMask, includeTriggers)
  -> PhysicsWorld::OverlapSphere(sphere, queryMask, includeTriggers)
  -> PhysicsWorld::OverlapBox(box, queryMask, includeTriggers)
```

`PhysicsWorld` does not mutate `Scene`, `GameObject`, `Component`, or `TransformComponent`. It owns a vector of
collider proxies built from the scene at an explicit sync point. Query calls read only that vector.

The initial sync source is live scene transform state because there is no rigid body simulation yet. Future rigid body
work will change the sync source for dynamic bodies to simulation poses while keeping the query API unchanged. Render
presentation poses remain separate and do not affect default runtime queries.

## Query Semantics

Runtime physics queries are simulation/query-state based:

- A query observes the most recent `PhysicsWorld::SyncFromScene(scene)` result.
- Transform changes after sync are not visible to queries until the next sync.
- Queries are read-only from the caller's perspective.
- Query ordering is deterministic and independent of pointer ordering.
- Closest raycast returns the nearest valid hit by distance, with stable tie-breaking by `SceneObjectId`.
- Raycast-all returns valid hits sorted by distance, then `SceneObjectId`.
- Overlap queries return matching objects in stable scene synchronization order.

This differs from a raw scene scan by creating an explicit sync boundary. That boundary is required so future dynamic
rigid bodies can query simulation poses instead of render interpolation poses.

## Layer And Trigger Filtering

Physics layers and masks are 64-bit.

```text
ColliderComponent::layer = UInt64
ColliderComponent::collidesWith = UInt64
Physics query mask = UInt64
```

Defaults:

```text
layer = 1ull
collidesWith = ~0ull
queryMask = ~0ull
```

Runtime query filtering uses:

```cpp
if ((colliderLayer & queryMask) == 0)
{
    skip;
}

if (!includeTriggers && colliderIsTrigger)
{
    skip;
}
```

The `collidesWith` property is serialized now because it belongs to the collider authoring model, but the first query
APIs do not use it for world queries. Future collider-vs-collider simulation should require both sides to opt in:

```cpp
(a.layer & b.collidesWith) != 0 &&
(b.layer & a.collidesWith) != 0
```

## Geometry Types

Add focused value types under `Engine/Runtime/Physics`:

- `Ray`: normalized or normalizable origin/direction plus optional helper for `GetPoint(distance)`.
- `Sphere`: center and radius.
- `OrientedBox`: center, half extents, and rotation.
- `Aabb`: min and max corners.
- `RaycastHit`: object identity and hit details.
- `OverlapHit`: object identity and collider details for overlap queries.

The query helpers should be free functions or small stateless functions:

- Ray vs sphere.
- Ray vs oriented box.
- Sphere vs sphere.
- Sphere vs oriented box.
- Oriented box vs oriented box using SAT.
- AABB construction for sphere and oriented box.

The math should be deterministic, readable, and easy to test. AABB is used for broad filtering and bounds queries, not
as a replacement for oriented shape tests.

## ColliderComponent

`ColliderComponent` is a native scene component.

Properties:

```text
shape: ColliderShape
center: Vector3
boxSize: Vector3
sphereRadius: Float32
layer: UInt64
collidesWith: UInt64
isTrigger: bool
enabled: bool
```

Defaults:

```text
shape = ColliderShape::Box
center = Vector3::Zero()
boxSize = Vector3::One()
sphereRadius = 0.5f
layer = 1ull
collidesWith = ~0ull
isTrigger = false
enabled = true
```

Shape construction rules:

- A collider without `TransformComponent` is skipped during sync.
- Disabled colliders are skipped during sync.
- Inactive `GameObject` hierarchy state is respected.
- Box colliders use Transform position, rotation, scale, local center, and local box size to create an oriented box.
- Sphere colliders use Transform position, rotation, scale, local center, and local radius. Non-uniform scale uses the
  maximum absolute axis scale for the world radius.
- Negative or zero sizes/radii are clamped to non-negative safe values during shape construction; setters preserve
  authored values only if they are valid.

## One Collider Rule

A `GameObject` may have at most one `ColliderComponent`.

Runtime add behavior:

- `GameObject::AddComponent<ColliderComponent>()` returns the existing collider if one already exists, rather than
  adding a duplicate.
- Adding other component types keeps existing behavior.

Deserialization behavior:

- The first `ColliderComponent` on an object is loaded.
- Duplicate collider components on the same object are skipped with a scene warning.
- Deserialization continues so a recoverable scene file can still load.

Editor duplicate prevention can build on this rule later.

## PhysicsWorld

`PhysicsWorld` owns synchronized query proxies:

```text
ColliderProxy
  gameObjectId
  collider pointer or stable component identity for internal use
  ColliderShape
  layer
  collidesWith
  isTrigger
  world sphere or oriented box
  enclosing Aabb
```

Public API shape:

```cpp
class PhysicsWorld
{
public:
    void Clear();
    void SyncFromScene(const Scene& scene);

    [[nodiscard]] std::optional<RaycastHit>
    RaycastClosest(const Ray& ray, UInt64 queryMask = ~0ull, bool includeTriggers = false) const;

    [[nodiscard]] std::vector<RaycastHit>
    RaycastAll(const Ray& ray, UInt64 queryMask = ~0ull, bool includeTriggers = false) const;

    [[nodiscard]] std::vector<OverlapHit>
    OverlapSphere(const Sphere& sphere, UInt64 queryMask = ~0ull, bool includeTriggers = false) const;

    [[nodiscard]] std::vector<OverlapHit>
    OverlapBox(const OrientedBox& box, UInt64 queryMask = ~0ull, bool includeTriggers = false) const;
};
```

The exact signatures can be adjusted to match local style during implementation, but the behavior and ownership model
should remain stable.

## Reflection And Serialization

Add `ColliderShape` as a reflected enum:

```text
Box
Sphere
```

Register `ColliderComponent` in `RegisterSceneReflectionTypes()`.

Serialization should round-trip:

- `shape`
- `center`
- `boxSize`
- `sphereRadius`
- `layer`
- `collidesWith`
- `isTrigger`
- `enabled`

Because the current reflection property set does not include a dedicated 64-bit integer property type, the
implementation should add the smallest necessary reflection support for unsigned 64-bit values instead of storing masks
as strings or floats.

## Testing

Add a focused physics unit test target. The tests should be independent from rendering and platform code.

Geometry tests:

- Ray hits sphere at expected distance, position, and normal.
- Ray misses sphere when outside radius.
- Ray hits oriented box after rotation.
- Sphere overlaps sphere.
- Sphere overlaps oriented box.
- Oriented box overlaps oriented box with rotation.
- Oriented box misses oriented box across a separating axis.
- AABB encloses transformed sphere and oriented box.

Component and serialization tests:

- `ColliderComponent` defaults are stable.
- Collider setters/getters preserve expected values.
- `GameObject` enforces one collider per object.
- Scene serialization round-trips collider shape and 64-bit masks.
- Scene deserialization skips duplicate colliders on one object.

PhysicsWorld tests:

- `SyncFromScene()` skips inactive objects, disabled colliders, and objects without transforms.
- Sphere and box proxies respect Transform position, rotation, scale, and local center.
- Closest raycast picks the nearest collider.
- Raycast-all sorts by distance and stable object id.
- Query mask filters 64-bit layers.
- Trigger filtering excludes triggers by default and includes them when requested.
- Overlap sphere returns expected object ids.
- Overlap box returns expected object ids.
- Transform changes after sync do not affect queries until the next sync.

## Integration Plan Direction

The implementation should proceed test-first:

1. Add geometry tests and minimal geometry helpers.
2. Add `ColliderComponent` tests and component implementation.
3. Add reflection and serialization tests for collider properties and 64-bit masks.
4. Add `PhysicsWorld` sync and query tests.
5. Register the new source files and test target in CMake.
6. Run the Windows CMake test preset through `CMake/Scripts/WithMsvc.bat`.

## Future Extensions

This query world becomes the stable foundation for the rest of Milestone 9:

- `RigidBodyComponent` will update dynamic collider proxies from simulation poses.
- Fixed-step scheduling will call physics sync at explicit frame boundaries.
- Collision response can reuse the same geometry helpers.
- Runtime picking can call `PhysicsWorld` queries.
- Editor visual picking can optionally query a separate presentation snapshot when the user needs to select what is
  currently rendered.
- C# `Physics.Raycast` can bind to the native query API after native behavior is stable.
