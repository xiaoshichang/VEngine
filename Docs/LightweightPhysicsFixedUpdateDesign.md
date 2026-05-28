# Lightweight Physics And Fixed Update Design

This document refines `DevelopmentPlan.md` Milestone 9. It defines the first lightweight physics slice for VEngine:
oriented box and sphere colliders, one collider and one rigid body per `GameObject`, fixed-step simulation, scene
picking, and a small rigid body model.

## 1. Goals

- Add a lightweight physics module that is useful for simple gameplay, scene picking, and Editor selection.
- Support exactly two first-stage collider shapes: oriented box and sphere.
- Support at most one `ColliderComponent` and at most one `RigidBodyComponent` on a single `GameObject`.
- Run physics from a fixed update loop so simulation is stable even when render frame rate varies.
- Provide a clear `FixedUpdate` timing contract for native components and C# scripts.
- Support enough angular dynamics for a box to fall, hit the ground off-center, rotate from contact impulses, and settle
  in the sample scene.
- Keep the implementation small enough to remain engine-owned and easy to debug.

## 2. Non-Goals

- Multiple colliders on one `GameObject`.
- Compound collider authoring.
- Mesh, capsule, cylinder, convex hull, heightfield, or terrain colliders.
- Production-grade rigid body simulation.
- Constraints, joints, character controllers, continuous collision detection, sleeping islands, and physics materials.
- A third-party physics SDK integration.
- A separate physics thread in the first implementation.
- Deterministic lockstep networking.

## 3. References From Unity And Unreal

Unity's useful lesson is the split between variable frame `Update` and fixed-step physics. Unity calls `FixedUpdate` on
a fixed interval controlled by `Time.fixedDeltaTime`; depending on render frame time, a rendered frame may run zero,
one, or multiple fixed updates before the normal frame update. VEngine should follow this user-facing mental model:
physics-facing gameplay code belongs in fixed update, while visual and per-render-frame behavior belongs in update. See
Unity's [Fixed updates](https://docs.unity3d.com/6000.0/Documentation/Manual/fixed-updates.html),
[`MonoBehaviour.FixedUpdate`](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/MonoBehaviour.FixedUpdate.html),
and [`Time.fixedDeltaTime`](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/Time-fixedDeltaTime.html)
documentation for the reference behavior.

Unreal's useful lesson is the explicit relationship between gameplay ticking and physics. Unreal exposes tick groups
such as pre-physics and post-physics so systems can choose whether they run before or after simulation, and Unreal's
physics sub-stepping divides large frame times into smaller simulation steps. VEngine should adopt named physics phases
on the Game Thread, but defer Unreal-style tick dependency graphs, async physics threads, and sub-step callback
complexity. See Unreal's
[Actor Ticking](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-ticking-in-unreal-engine) and
[Physics Sub-Stepping](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-sub-stepping-in-unreal-engine)
documentation for the reference behavior.

## 4. Component Model

### 4.1 ColliderComponent

`ColliderComponent` describes the collision shape for a `GameObject`.

First-stage serialized properties:

- `shape`: `Box` or `Sphere`.
- `center`: local-space shape center.
- `boxSize`: local-space box size, used when `shape == Box`.
- `sphereRadius`: local-space radius, used when `shape == Sphere`.
- `layer`: physics layer bit.
- `collidesWith`: layer mask.
- `isTrigger`: whether the collider reports overlaps without collision response.
- `enabled`: whether the collider participates in queries and simulation.

First-stage rules:

- A `GameObject` may have zero or one `ColliderComponent`.
- A collider with no `RigidBodyComponent` is treated as a static collider.
- Box colliders are the only box-like shape in Milestone 9. Their simulation shape is an oriented box derived from
  Transform position, rotation, scale, local center, and box size. An enclosing `AABB` may be used for broad phase and
  fast bounds queries, but dynamic collision response must use the oriented box so a falling box can rotate against the
  ground without the collider visually drifting away from the mesh.
- Collider shape changes during play are allowed on the Game Thread, but they take effect at the next fixed-step
  boundary.

### 4.2 RigidBodyComponent

`RigidBodyComponent` describes how a `GameObject` moves under fixed-step simulation.

First-stage serialized properties:

- `bodyType`: `Static`, `Kinematic`, or `Dynamic`.
- `mass`: positive scalar mass for dynamic bodies.
- `useGravity`: whether world gravity affects the body.
- `gravityScale`: scalar multiplier for world gravity.
- `linearVelocity`: world-space velocity.
- `angularVelocity`: world-space angular velocity in radians per second.
- `linearDamping`: linear velocity damping per fixed step.
- `angularDamping`: angular velocity damping per fixed step.
- `interpolationMode`: `None`, `Interpolate`, or `Extrapolate` for render presentation, defaulting to `Interpolate`
  for dynamic bodies.
- `isKinematic`: compatibility alias or editor-facing shorthand for `bodyType == Kinematic` if useful.
- `freezePositionX/Y/Z`: optional per-axis position freeze flags if the sample needs them.
- `freezeRotationX/Y/Z`: optional per-axis rotation freeze flags if the sample needs them.

First-stage rules:

- A `GameObject` may have zero or one `RigidBodyComponent`.
- A dynamic or kinematic body may exist without a collider, but it will not generate contacts. The Editor should warn
  when this looks accidental.
- A collider with `bodyType == Static` or no rigid body does not move during physics response.
- A kinematic rigid body is moved by game code and participates as a moving obstacle, but is not affected by forces or
  impulses.
- A dynamic rigid body is integrated by physics and can be affected by gravity, linear velocity, angular velocity,
  forces, torques, impulses, and simple collision response.
- First-stage angular dynamics must include angular velocity integration, accumulated torque, shape-derived inertia, and
  contact impulses that affect both linear and angular velocity.
- Box inertia is derived from mass and scaled box dimensions. Sphere inertia is derived from mass and scaled radius.
- The first implementation may keep inertia diagonal in local body space and transform inverse inertia to world space
  each step from the current orientation.
- Center of mass defaults to the body's world transform position plus the collider's transformed local center. A custom
  center-of-mass authoring property is out of scope for Milestone 9.

### 4.3 AddComponent Rules

The Editor and runtime component factory should enforce the one-collider and one-rigid-body rules consistently:

- `AddComponent<ColliderComponent>` fails or is disabled if the target already has a `ColliderComponent`.
- `AddComponent<RigidBodyComponent>` fails or is disabled if the target already has a `RigidBodyComponent`.
- Scene deserialization should reject duplicate collider or rigid body components on one `GameObject`, or load with a
  clear diagnostic and ignore duplicates if strict rejection would make recovery painful.
- Reflection metadata should mark both components as normal native components, but the Inspector should make duplicate
  additions impossible.

## 5. Physics Ownership

Physics belongs to the Game Thread.

First-stage ownership:

```text
EngineRuntime
  -> GameThreadSystem
      -> Scene
      -> PhysicsSystem
```

Rules:

- Game Thread owns fixed-step scheduling and scene mutation.
- Physics reads `TransformComponent`, `ColliderComponent`, and `RigidBodyComponent` state only on the Game Thread.
- Physics writes simulation results back through normal Transform mutation rules.
- Physics keeps transient previous/current simulation pose history for rigid bodies that need render interpolation.
- Render Thread sees physics results only through the normal render extraction/snapshot path.
- Render extraction may use a presentation pose for physics-driven objects; that pose is render-only and must not be
  written back into `TransformComponent`.
- Worker jobs may be used later for broad phase or narrow phase work, but they must not directly mutate live scene
  state.

## 6. Fixed Update Timing

VEngine already has `ve::Time` fixed-step budgeting:

```text
fixedAccumulatorSeconds += deltaSeconds
fixedStepCount = floor(fixedAccumulatorSeconds / fixedDeltaSeconds)
fixedAccumulatorSeconds -= fixedStepCount * fixedDeltaSeconds
```

The first physics scheduler should consume `Time::GetFixedStepCount()` during the Game Thread frame. `Time` continues
to own clock measurement and fixed-step budgeting; `GameThreadSystem` owns execution.

### 6.1 Frame Order

The recommended first implementation frame order is:

```text
GameThreadSystem::TickFrame()
  BeginFrame
    Time::Tick() or Time::Advance()
    Capture stable TimeSnapshot
    Consume latest input snapshot from Main Thread
    Flush scene/component lifecycle queues that must happen before simulation

  FixedStepLoop
    for stepIndex in [0, TimeSnapshot.fixedStepCount)
      FixedStepBegin(fixedDeltaSeconds)
      Dispatch native Component::OnFixedUpdate(fixedDeltaSeconds)
      Dispatch ScriptBehaviour.OnFixedUpdate(fixedDeltaSeconds)
      PhysicsSystem::SyncFromScene()
      PhysicsSystem::Step(fixedDeltaSeconds)
      PhysicsSystem::WriteBackTransforms()
      Scene::UpdateTransforms()
      PhysicsSystem::DispatchFixedStepEvents()
      FixedStepEnd()

  VariableUpdate
    Dispatch native Component::OnUpdate(deltaSeconds)
    Dispatch ScriptBehaviour.OnUpdate(deltaSeconds)
    Runtime world picking, if enabled for the frame
    Scene::UpdateTransforms()

  RenderExtraction
    Build physics presentation poses from fixed-step remainder
    Extract render and debug snapshots for RenderSystem

  EndFrame
```

### 6.2 Why FixedUpdate Runs Before Physics

`FixedUpdate` runs before `PhysicsSystem::Step()` so gameplay code can affect the same physics step:

- Apply force or impulse.
- Set linear velocity.
- Set angular velocity.
- Apply torque.
- Move a kinematic body.
- Enable or disable a collider.
- Perform raycasts against the previous completed physics state before the next step is solved.

After `FixedUpdate`, physics synchronizes scene state, integrates dynamic bodies, detects contacts, resolves simple
collisions, writes back transforms, updates transform hierarchy state, and dispatches fixed-step events.

### 6.3 Zero, One, Or Many Fixed Steps

The fixed loop can run zero, one, or multiple times during a rendered frame:

- If render frames are faster than the fixed delta, the frame can run no fixed steps and still run `OnUpdate`.
- If render frames are near the fixed delta, the frame usually runs one fixed step.
- If the frame hitches, the frame can run several fixed steps before one `OnUpdate`.

This is intentional. Gameplay code that affects physics should use `FixedUpdate`; frame-rate-dependent visual behavior
should use `Update`.

### 6.4 Catch-Up And Step Limits

The existing `Time` module clamps `deltaSeconds` through `maxDeltaSeconds`, which bounds the number of fixed steps that
can be budgeted after a hitch. With the current defaults:

```text
fixedDeltaSeconds = 1 / 60
maxDeltaSeconds = 0.25
maximum budgeted fixed steps after clamp = 15
```

The first scheduler should execute the budgeted steps rather than silently dropping them. If a later milestone adds
`MaxPhysicsStepsPerFrame`, then fixed-step accumulator ownership should move from `Time` into the scheduler or expose a
safe consume API so dropped or deferred simulation time is explicit.

### 6.5 Input Visibility

The same input snapshot is visible to all fixed steps in a rendered frame. This keeps the first implementation simple
and predictable:

- Input is captured once near frame start.
- Multiple fixed steps caused by catch-up do not each poll OS input again.
- World picking consumes the frame input snapshot during variable update.

### 6.6 Scene Mutation During FixedUpdate

Scene mutation during `FixedUpdate` is allowed because it happens on the Game Thread, but physics must observe stable
step boundaries:

- Components added during a fixed step participate no earlier than the next fixed step.
- Components removed during a fixed step are marked inactive for the current step and removed before the next step.
- Transform writes before `PhysicsSystem::SyncFromScene()` affect the current physics step.
- Transform writes after physics write-back should be treated like normal gameplay writes and may be overwritten by the
  next dynamic body simulation step unless the body is kinematic.

### 6.7 Simulation, Transform, And Presentation Poses

Fixed-step physics and variable-rate rendering must not be treated as one pose stream. Otherwise a dynamic body updates
only at the physics rate while the renderer samples every frame, which causes visible stepping when the render frame
rate is higher than the fixed rate.

The first physics implementation should use three pose concepts:

- Simulation pose: the current fixed-step pose owned by physics and used for collision response, contacts, rigid body
  integration, and gameplay physics queries.
- Scene Transform: the normal `TransformComponent` state visible to gameplay. For dynamic rigid bodies, physics writes
  the completed simulation pose into `TransformComponent` after each fixed step.
- Presentation pose: a render-only pose computed during `RenderExtraction`. It is used by render/debug extraction and
  optionally visual editor picking, but is never serialized and is not visible to scripts as `Transform`.

`PhysicsSystem` should retain two transient pose samples for each active rigid body:

```text
previousSimulationPose
currentSimulationPose
```

Pose history rules:

- When a fixed step begins, copy the body's current simulation pose to `previousSimulationPose`.
- After integration and collision response, store the completed pose in `currentSimulationPose`;
  `PhysicsSystem::WriteBackTransforms()` then writes it back to `TransformComponent`.
- If multiple fixed steps run in one rendered frame, `previousSimulationPose` and `currentSimulationPose` should span
  the last completed fixed step.
- If zero fixed steps run, `TransformComponent` remains at the last completed simulation pose, while presentation may
  still advance visually between the two latest simulation samples.
- On creation, teleport, scene load, body type changes, or direct non-kinematic Transform correction, reset
  `previousSimulationPose = currentSimulationPose` so interpolation does not sweep across a discontinuity.

Presentation modes:

- `None`: use `currentSimulationPose` directly. This is useful for debugging exact physics state.
- `Interpolate`: use the last two completed simulation samples and
  `alpha = clamp(fixedAccumulatorSeconds / fixedDeltaSeconds, 0, 1)`. Position uses linear interpolation, rotation uses
  spherical interpolation, and scale uses the current `TransformComponent` scale. This is the default mode for dynamic
  bodies because it is stable around contacts and avoids visual jitter at the cost of one fixed-step of visual latency.
- `Extrapolate`: advance `currentSimulationPose` by linear and angular velocity using the fixed-step remainder, clamped
  to at most one fixed delta. This mode can reduce visual latency, but it must be explicit because collision correction
  can make extrapolated poses overshoot, penetrate, or visibly snap back. Extrapolation should be disabled for a frame
  after contact correction, teleport, or sleeping/waking transitions if those are later added.

Gameplay and rendering visibility:

- Native components, C# scripts, collision detection, and gameplay `Physics.Raycast` should read the simulation pose or
  the `TransformComponent` written from the latest completed physics step, not the presentation pose.
- Render extraction should prefer the presentation pose for dynamic rigid bodies with interpolation enabled.
- Editor viewport selection can use presentation collider snapshots when selecting what the user currently sees, but
  gameplay queries should stay simulation-state based.
- Kinematic bodies moved from `FixedUpdate` can use the same history and presentation path as dynamic bodies. Kinematic
  bodies moved from variable `Update` should present their current `TransformComponent` directly unless a later API adds
  explicit kinematic interpolation samples.

## 7. Physics Step Details

The first `PhysicsSystem::Step(fixedDeltaSeconds)` should be deliberately small:

```text
Sync active colliders and rigid bodies from Scene
Store previous simulation poses for active rigid bodies
Apply gravity to dynamic bodies
Apply accumulated forces, torques, impulses, and angular impulses
Compute or refresh inverse mass and world inverse inertia tensors
Integrate linear velocity
Integrate angular velocity
Integrate position with semi-implicit Euler
Integrate rotation from angular velocity and normalize orientation
Recompute world collider shapes and enclosing bounds
Find candidate pairs with deterministic linear scan
Run narrow phase for sphere/sphere, box/box, and sphere/box
Generate contact points, normals, penetration depth, and contact arms
Resolve non-trigger contacts with positional correction and linear/angular impulses
Record trigger overlaps and contact diagnostics
Publish current simulation poses for transform write-back
Clear per-step forces and torques
```

Narrow phase expectations:

- Sphere vs sphere can use center distance.
- Sphere vs box should use closest point on oriented box.
- Box vs box should use oriented-box axes, for example a small separating axis test over the two boxes' local axes and
  their cross axes.
- Contact generation can start with one stable contact point for each pair, but box/ground response should move toward a
  small manifold of corner or face contacts if a single point jitters too much.
- Enclosing `AABB` bounds are allowed for candidate filtering only; they are not the collision shape used for dynamic
  box response.

Angular dynamics requirements:

- Store `angularVelocity` in world space as radians per second.
- Accumulate torque in world space for the current fixed step.
- Integrate angular velocity with `angularVelocity += worldInverseInertia * torque * fixedDeltaSeconds`.
- Integrate orientation from angular velocity every fixed step and normalize the resulting quaternion.
- Clear accumulated torque after the step, just like accumulated force.
- For a box with dimensions `(width, height, depth)`, local diagonal inertia should use
  `Ixx = (1 / 12) * mass * (height^2 + depth^2)`, `Iyy = (1 / 12) * mass * (width^2 + depth^2)`, and
  `Izz = (1 / 12) * mass * (width^2 + height^2)`.
- For a solid sphere with radius `r`, local inertia should use `I = (2 / 5) * mass * r^2` on all three axes.
- World inverse inertia should be computed as `orientation * inverseLocalInertia * transpose(orientation)` or the
  equivalent matrix form used by the math library.
- Freeze-rotation axes, if implemented, should zero the corresponding inverse inertia component and angular velocity
  component before response is applied.

Recommended collision response:

- Dynamic vs static: move the dynamic body out of penetration and apply a contact impulse at the contact point. An
  off-center contact must be able to change angular velocity.
- Dynamic vs dynamic: split positional correction by inverse mass and apply a simple impulse using both inverse mass and
  inverse inertia.
- Trigger contacts: do not modify velocity or position.
- Kinematic contacts: treat kinematic bodies as infinite-mass moving colliders for dynamic response.
- Friction can start as a simple Coulomb-style tangent impulse or a small damping approximation. It only needs to be
  good enough for the box-drop sample to settle without sliding forever.
- Restitution can start as a single global coefficient or per-body scalar if that stays small.

For each contact point, response should use the contact arms from each body's center of mass to the contact point.
Assuming the contact normal points from body B toward body A:

```text
rA = contactPoint - centerOfMassA
rB = contactPoint - centerOfMassB
velocityAtContactA = linearVelocityA + cross(angularVelocityA, rA)
velocityAtContactB = linearVelocityB + cross(angularVelocityB, rB)
relativeVelocity = velocityAtContactA - velocityAtContactB

normalImpulseDenominator =
  inverseMassA + inverseMassB +
  dot(normal,
      cross(worldInverseInertiaA * cross(rA, normal), rA) +
      cross(worldInverseInertiaB * cross(rB, normal), rB))

impulseMagnitude =
  -(1 + restitution) * dot(relativeVelocity, normal) / normalImpulseDenominator
```

Clamp `impulseMagnitude` to zero when the contact points are separating. The resulting normal impulse updates both
linear and angular velocity:

```text
normalImpulse = normal * impulseMagnitude

linearVelocityA += normalImpulse * inverseMassA
angularVelocityA += worldInverseInertiaA * cross(rA, normalImpulse)

linearVelocityB -= normalImpulse * inverseMassB
angularVelocityB -= worldInverseInertiaB * cross(rB, normalImpulse)
```

This is the minimum rotational collision response needed for the demo. A box dropped with a slight horizontal offset,
initial tilt, or off-center ground contact should gain angular velocity, rotate visibly, lose energy through damping and
friction, and eventually settle.

This is not physically complete, but it should be enough for a dynamic box or sphere to fall, collide with a static
ground box, rotate from off-center contacts, lose energy through damping/friction, and settle for a convincing demo.

## 8. Query And Picking API

First native query APIs:

- `RaycastClosest(ray, mask, includeTriggers)`.
- `RaycastAll(ray, mask, includeTriggers)`.
- `OverlapSphere(center, radius, mask, includeTriggers)`.
- `OverlapBox(center, size, rotation, mask, includeTriggers)`.

`RaycastHit` should contain:

- `gameObjectId`.
- `componentIndex` or stable collider id.
- `distance`.
- `position`.
- `normal`.
- `isTrigger`.
- `layer`.

Editor picking should convert viewport coordinates to a world ray, run closest raycast against colliders, and select the
nearest hit. Mesh triangle picking and gizmo-specific hit proxies can be added later.

## 9. Script API Direction

Native physics should land first. The C# API should stay narrow:

- `ScriptBehaviour.OnFixedUpdate(float fixedDeltaTime)`.
- `Physics.Raycast(...)`.
- `Physics.OverlapSphere(...)`.
- `Physics.OverlapBox(...)` with an orientation parameter once the managed API exposes box queries.
- `RigidBody.LinearVelocity`.
- `RigidBody.AngularVelocity`.
- `RigidBody.AddForce(...)`, `AddTorque(...)`, `AddImpulse(...)`, and `AddAngularImpulse(...)` only after native force
  and torque accumulation are stable.

C# collision or trigger callbacks should not be added until native contact event ordering is stable. The first scripting
integration can rely on polling queries and rigid body control from `OnFixedUpdate`.

## 10. Tests

Focused test coverage:

- Ray vs sphere.
- Ray vs box.
- Sphere vs sphere.
- Oriented box vs oriented box.
- Sphere vs oriented box.
- Transform-aware bounds.
- One collider per `GameObject` enforcement.
- One rigid body per `GameObject` enforcement.
- Collider and rigid body serialization.
- Fixed-step budget consumption by the scheduler.
- Zero, one, and multiple `FixedUpdate` calls per rendered frame.
- `FixedUpdate` before physics step ordering.
- Physics pose history across zero, one, and multiple fixed steps.
- Interpolated presentation pose generation without mutating `TransformComponent`.
- Teleport and scene-load pose history reset.
- Dynamic body gravity integration.
- Dynamic body angular velocity integration.
- Torque accumulation and clearing.
- Shape-derived inertia for box and sphere.
- World inverse inertia updates when a box rotates.
- Dynamic vs static collision response.
- Dynamic vs dynamic collision response.
- Contact impulse angular response for an off-center box hit.
- Tilted box falling onto static ground rotates, loses energy, and settles.
- Trigger overlap filtering.
- Layer mask filtering.
- Closest raycast ordering.
- Editor screen-ray picking math.
