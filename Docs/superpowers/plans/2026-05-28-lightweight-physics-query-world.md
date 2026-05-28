# Lightweight Physics Query World Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the first Milestone 9 slice: a query-only `PhysicsWorld` with box/sphere colliders, 64-bit layer masks, scene synchronization, raycasts, overlap queries, and serialization.

**Architecture:** Add a focused `Engine/Runtime/Physics` module. Runtime queries read synchronized `PhysicsWorld` collider proxies instead of live `TransformComponent` state, preserving the future split between simulation pose and render presentation pose. Geometry helpers stay stateless and testable; `ColliderComponent` remains a normal scene component registered through Reflection.

**Tech Stack:** C++20, CMake, Boost.JSON through existing scene serialization, VEngine Reflection, CTest via `CMake/Scripts/WithMsvc.bat`.

---

## File Structure

- Create `Engine/Runtime/Physics/PhysicsGeometry.h`: value types and query helper declarations for `Ray`, `Sphere`, `OrientedBox`, `Aabb`, shape ray hits, and overlap tests.
- Create `Engine/Runtime/Physics/PhysicsGeometry.cpp`: ray/sphere, ray/oriented-box, sphere/sphere, sphere/oriented-box, oriented-box/oriented-box SAT, and AABB construction.
- Create `Engine/Runtime/Physics/ColliderComponent.h`: `ColliderShape` enum and serializable collider component API.
- Create `Engine/Runtime/Physics/ColliderComponent.cpp`: collider component getters/setters and world shape construction from `TransformComponent`.
- Create `Engine/Runtime/Physics/PhysicsWorld.h`: query proxy records, `RaycastHit`, `OverlapHit`, and `PhysicsWorld` public API.
- Create `Engine/Runtime/Physics/PhysicsWorld.cpp`: `SyncFromScene`, filtering, raycast sorting, and overlap query logic.
- Create `Tests/Unit/PhysicsTests.cpp`: all new physics unit tests.
- Modify `CMake/Targets/Engine.cmake`: add new physics source/header files to `VEngine`.
- Modify `CMake/Targets/Tests.cmake`: add `VEnginePhysicsTests`.
- Modify `Engine/Runtime/Reflection/ReflectionRegistry.h`: add `ReflectedPropertyType::UInt64`.
- Modify `Engine/Runtime/Reflection/ReflectionRegistry.cpp`: add UInt64 JSON conversion and register `ColliderShape`/`ColliderComponent`.
- Modify `Engine/Runtime/Scene/GameObject.h`: return the actual component returned by `AddComponent(std::unique_ptr<Component>)` from templated `AddComponent`.
- Modify `Engine/Runtime/Scene/GameObject.cpp`: enforce one `ColliderComponent` per `GameObject`.
- Modify `Engine/Runtime/Scene/Serialization/SceneSerialization.cpp`: skip duplicate reflected components when `AddComponent` returns an existing component.
- Modify `Tests/Unit/SceneTests.cpp`: add a focused duplicate collider deserialization regression if it fits better there; otherwise keep it in `PhysicsTests.cpp`.

## Test Command Reference

Use the repository-required Windows MSVC wrapper:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEnginePhysicsTests --output-on-failure
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
```

When a step says "run the test", run the narrow build/test command first. Run the full preset at the end of the plan and after any task that touches shared reflection or serialization behavior.

---

### Task 1: Physics Test Target And Geometry Skeleton

**Files:**
- Create: `Tests/Unit/PhysicsTests.cpp`
- Create: `Engine/Runtime/Physics/PhysicsGeometry.h`
- Create: `Engine/Runtime/Physics/PhysicsGeometry.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: `CMake/Targets/Tests.cmake`

- [ ] **Step 1: Write the failing geometry smoke tests**

Create `Tests/Unit/PhysicsTests.cpp` with this initial content:

```cpp
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Physics/PhysicsGeometry.h"

#include <iostream>

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool TestRayHitsSphere()
    {
        const ve::Ray ray(ve::Vector3(0.0f, 0.0f, -5.0f), ve::Vector3::UnitZ());
        const ve::Sphere sphere{ve::Vector3::Zero(), 1.0f};
        const auto hit = ve::RaycastSphere(ray, sphere);

        bool passed = true;
        passed &= Expect(hit.has_value(), "Ray should hit sphere");
        if (hit)
        {
            passed &= Expect(ve::NearlyEqual(hit->distance, 4.0f), "Sphere hit distance should be nearest surface");
            passed &= Expect(hit->position.IsNearlyEqual(ve::Vector3(0.0f, 0.0f, -1.0f)),
                             "Sphere hit position should be on near cap");
            passed &= Expect(hit->normal.IsNearlyEqual(ve::Vector3(0.0f, 0.0f, -1.0f)),
                             "Sphere hit normal should point outward");
        }

        return passed;
    }

    bool TestRayMissesSphere()
    {
        const ve::Ray ray(ve::Vector3(3.0f, 0.0f, -5.0f), ve::Vector3::UnitZ());
        const ve::Sphere sphere{ve::Vector3::Zero(), 1.0f};
        return Expect(!ve::RaycastSphere(ray, sphere).has_value(), "Ray outside radius should miss sphere");
    }
}

int main()
{
    bool passed = true;
    passed &= TestRayHitsSphere();
    passed &= TestRayMissesSphere();
    return passed ? 0 : 1;
}
```

- [ ] **Step 2: Register the target before implementation**

Add the future physics files to `CMake/Targets/Engine.cmake`:

```cmake
            Engine/Runtime/Physics/ColliderComponent.cpp
            Engine/Runtime/Physics/PhysicsGeometry.cpp
            Engine/Runtime/Physics/PhysicsWorld.cpp
```

and the public headers:

```cmake
            Engine/Runtime/Physics/ColliderComponent.h
            Engine/Runtime/Physics/PhysicsGeometry.h
            Engine/Runtime/Physics/PhysicsWorld.h
```

Add this test target to `CMake/Targets/Tests.cmake` after `VEngineMathTests`:

```cmake
    add_executable(VEnginePhysicsTests
        Tests/Unit/PhysicsTests.cpp
    )

    target_link_libraries(VEnginePhysicsTests
        PRIVATE
            VEngine
    )

    ve_configure_target(VEnginePhysicsTests)

    add_test(
        NAME VEnginePhysicsTests
        COMMAND $<TARGET_FILE:VEnginePhysicsTests>
    )
```

- [ ] **Step 3: Run test to verify RED**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
```

Expected: build fails because `Engine/Runtime/Physics/PhysicsGeometry.h` does not exist.

- [ ] **Step 4: Add minimal geometry files**

Create `Engine/Runtime/Physics/PhysicsGeometry.h`:

```cpp
#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"

#include <array>
#include <optional>

namespace ve
{
    struct Ray
    {
        Vector3 origin = Vector3::Zero();
        Vector3 direction = Vector3::UnitZ();

        Ray() = default;
        Ray(const Vector3& rayOrigin, const Vector3& rayDirection) noexcept;

        [[nodiscard]] Vector3 GetPoint(Float32 distance) const noexcept;
    };

    struct Sphere
    {
        Vector3 center = Vector3::Zero();
        Float32 radius = 0.5f;
    };

    struct OrientedBox
    {
        Vector3 center = Vector3::Zero();
        Vector3 halfExtents = Vector3(0.5f, 0.5f, 0.5f);
        std::array<Vector3, 3> axes = {Vector3::UnitX(), Vector3::UnitY(), Vector3::UnitZ()};
    };

    struct Aabb
    {
        Vector3 minimum = Vector3::Zero();
        Vector3 maximum = Vector3::Zero();
    };

    struct ShapeRaycastHit
    {
        Float32 distance = 0.0f;
        Vector3 position = Vector3::Zero();
        Vector3 normal = Vector3::UnitY();
    };

    [[nodiscard]] std::optional<ShapeRaycastHit> RaycastSphere(const Ray& ray, const Sphere& sphere) noexcept;
}
```

Create `Engine/Runtime/Physics/PhysicsGeometry.cpp`:

```cpp
#include "Engine/Runtime/Physics/PhysicsGeometry.h"

#include "Engine/Runtime/Math/Math.h"

namespace ve
{
    Ray::Ray(const Vector3& rayOrigin, const Vector3& rayDirection) noexcept
        : origin(rayOrigin)
        , direction(rayDirection.Normalized())
    {
        if (direction == Vector3::Zero())
        {
            direction = Vector3::UnitZ();
        }
    }

    Vector3 Ray::GetPoint(Float32 distance) const noexcept
    {
        return origin + (direction * distance);
    }

    std::optional<ShapeRaycastHit> RaycastSphere(const Ray& ray, const Sphere& sphere) noexcept
    {
        const Vector3 toSphere = ray.origin - sphere.center;
        const Float32 a = Vector3::Dot(ray.direction, ray.direction);
        const Float32 b = 2.0f * Vector3::Dot(toSphere, ray.direction);
        const Float32 c = Vector3::Dot(toSphere, toSphere) - (sphere.radius * sphere.radius);
        const Float32 discriminant = (b * b) - (4.0f * a * c);
        if (discriminant < 0.0f)
        {
            return std::nullopt;
        }

        const Float32 sqrtDiscriminant = Sqrt(discriminant);
        const Float32 inverseDenominator = 1.0f / (2.0f * a);
        Float32 distance = (-b - sqrtDiscriminant) * inverseDenominator;
        if (distance < 0.0f)
        {
            distance = (-b + sqrtDiscriminant) * inverseDenominator;
        }

        if (distance < 0.0f)
        {
            return std::nullopt;
        }

        ShapeRaycastHit hit;
        hit.distance = distance;
        hit.position = ray.GetPoint(distance);
        hit.normal = (hit.position - sphere.center).Normalized();
        return hit;
    }
}
```

Create empty stubs for future files so CMake can build:

`Engine/Runtime/Physics/ColliderComponent.h`

```cpp
#pragma once

namespace ve
{
}
```

`Engine/Runtime/Physics/ColliderComponent.cpp`

```cpp
#include "Engine/Runtime/Physics/ColliderComponent.h"
```

`Engine/Runtime/Physics/PhysicsWorld.h`

```cpp
#pragma once

namespace ve
{
}
```

`Engine/Runtime/Physics/PhysicsWorld.cpp`

```cpp
#include "Engine/Runtime/Physics/PhysicsWorld.h"
```

- [ ] **Step 5: Run test to verify GREEN**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEnginePhysicsTests --output-on-failure
```

Expected: `VEnginePhysicsTests` passes.

- [ ] **Step 6: Commit**

```text
git add CMake/Targets/Engine.cmake CMake/Targets/Tests.cmake Tests/Unit/PhysicsTests.cpp Engine/Runtime/Physics
git commit -m "feat: add physics geometry skeleton"
```

---

### Task 2: Oriented Box, AABB, And Overlap Geometry

**Files:**
- Modify: `Tests/Unit/PhysicsTests.cpp`
- Modify: `Engine/Runtime/Physics/PhysicsGeometry.h`
- Modify: `Engine/Runtime/Physics/PhysicsGeometry.cpp`

- [ ] **Step 1: Add failing geometry coverage**

Append these test functions to `Tests/Unit/PhysicsTests.cpp` and call them from `main()`:

```cpp
    bool TestRayHitsRotatedBox()
    {
        ve::OrientedBox box;
        box.center = ve::Vector3::Zero();
        box.halfExtents = ve::Vector3(1.0f, 2.0f, 0.5f);
        const ve::Quaternion rotation = ve::Quaternion::FromAxisAngle(ve::Vector3::UnitY(), ve::ToRadians(45.0f));
        box.axes = {rotation.RotateVector(ve::Vector3::UnitX()),
                    rotation.RotateVector(ve::Vector3::UnitY()),
                    rotation.RotateVector(ve::Vector3::UnitZ())};

        const ve::Ray ray(ve::Vector3(0.0f, 0.0f, -5.0f), ve::Vector3::UnitZ());
        const auto hit = ve::RaycastOrientedBox(ray, box);

        bool passed = true;
        passed &= Expect(hit.has_value(), "Ray should hit rotated oriented box");
        if (hit)
        {
            passed &= Expect(hit->distance > 4.0f && hit->distance < 5.0f,
                             "Rotated box hit should be before box center");
            passed &= Expect(hit->normal.Length() > 0.99f && hit->normal.Length() < 1.01f,
                             "Rotated box normal should be unit length");
        }
        return passed;
    }

    bool TestSphereAndBoxOverlaps()
    {
        const ve::Sphere a{ve::Vector3::Zero(), 1.0f};
        const ve::Sphere b{ve::Vector3(1.5f, 0.0f, 0.0f), 1.0f};
        ve::OrientedBox box;
        box.center = ve::Vector3(3.0f, 0.0f, 0.0f);
        box.halfExtents = ve::Vector3(1.0f, 1.0f, 1.0f);

        bool passed = true;
        passed &= Expect(ve::Overlaps(a, b), "Spheres should overlap when centers are closer than radii sum");
        passed &= Expect(!ve::Overlaps(a, ve::Sphere{ve::Vector3(3.0f, 0.0f, 0.0f), 0.5f}),
                         "Spheres should miss when separated");
        passed &= Expect(ve::Overlaps(b, box), "Sphere should overlap oriented box");
        return passed;
    }

    bool TestOrientedBoxSatAndAabb()
    {
        ve::OrientedBox first;
        first.center = ve::Vector3::Zero();
        first.halfExtents = ve::Vector3(1.0f, 1.0f, 1.0f);

        ve::OrientedBox second;
        second.center = ve::Vector3(1.25f, 0.0f, 0.0f);
        second.halfExtents = ve::Vector3(1.0f, 1.0f, 1.0f);
        const ve::Quaternion rotation = ve::Quaternion::FromAxisAngle(ve::Vector3::UnitZ(), ve::ToRadians(20.0f));
        second.axes = {rotation.RotateVector(ve::Vector3::UnitX()),
                       rotation.RotateVector(ve::Vector3::UnitY()),
                       rotation.RotateVector(ve::Vector3::UnitZ())};

        const ve::OrientedBox farBox{ve::Vector3(4.0f, 0.0f, 0.0f),
                                     ve::Vector3(1.0f, 1.0f, 1.0f),
                                     {ve::Vector3::UnitX(), ve::Vector3::UnitY(), ve::Vector3::UnitZ()}};

        const ve::Aabb bounds = ve::BuildAabb(second);

        bool passed = true;
        passed &= Expect(ve::Overlaps(first, second), "Rotated boxes should overlap");
        passed &= Expect(!ve::Overlaps(first, farBox), "Separated boxes should not overlap");
        passed &= Expect(bounds.minimum.GetX() < second.center.GetX() && bounds.maximum.GetX() > second.center.GetX(),
                         "AABB should enclose rotated box center on X");
        return passed;
    }
```

Update `main()`:

```cpp
    passed &= TestRayHitsRotatedBox();
    passed &= TestSphereAndBoxOverlaps();
    passed &= TestOrientedBoxSatAndAabb();
```

- [ ] **Step 2: Run test to verify RED**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
```

Expected: build fails with missing `RaycastOrientedBox`, `Overlaps`, and `BuildAabb`.

- [ ] **Step 3: Add declarations**

Add to `PhysicsGeometry.h`:

```cpp
    [[nodiscard]] std::optional<ShapeRaycastHit> RaycastOrientedBox(const Ray& ray,
                                                                    const OrientedBox& box) noexcept;
    [[nodiscard]] bool Overlaps(const Sphere& left, const Sphere& right) noexcept;
    [[nodiscard]] bool Overlaps(const Sphere& sphere, const OrientedBox& box) noexcept;
    [[nodiscard]] bool Overlaps(const OrientedBox& left, const OrientedBox& right) noexcept;
    [[nodiscard]] Aabb BuildAabb(const Sphere& sphere) noexcept;
    [[nodiscard]] Aabb BuildAabb(const OrientedBox& box) noexcept;
```

- [ ] **Step 4: Implement the geometry helpers**

In `PhysicsGeometry.cpp`, add local helpers:

```cpp
#include <algorithm>
#include <cmath>
```

and implement:

```cpp
    namespace
    {
        [[nodiscard]] Float32 Abs(Float32 value) noexcept
        {
            return std::abs(value);
        }

        [[nodiscard]] Float32 ComponentAt(const Vector3& value, SizeT index) noexcept
        {
            if (index == 0)
            {
                return value.GetX();
            }
            if (index == 1)
            {
                return value.GetY();
            }
            return value.GetZ();
        }

        [[nodiscard]] Vector3 ClosestPointOnBox(const Vector3& point, const OrientedBox& box) noexcept
        {
            Vector3 result = box.center;
            const Vector3 offset = point - box.center;
            for (SizeT axisIndex = 0; axisIndex < 3; ++axisIndex)
            {
                const Float32 distance = Vector3::Dot(offset, box.axes[axisIndex]);
                const Float32 clamped = Clamp(distance,
                                              -ComponentAt(box.halfExtents, axisIndex),
                                              ComponentAt(box.halfExtents, axisIndex));
                result += box.axes[axisIndex] * clamped;
            }
            return result;
        }

        [[nodiscard]] Float32 ProjectRadius(const OrientedBox& box, const Vector3& axis) noexcept
        {
            return (box.halfExtents.GetX() * Abs(Vector3::Dot(axis, box.axes[0]))) +
                   (box.halfExtents.GetY() * Abs(Vector3::Dot(axis, box.axes[1]))) +
                   (box.halfExtents.GetZ() * Abs(Vector3::Dot(axis, box.axes[2])));
        }

        [[nodiscard]] bool HasSeparatingAxis(const OrientedBox& left,
                                             const OrientedBox& right,
                                             const Vector3& axis) noexcept
        {
            if (axis.LengthSquared() <= Math::DefaultEpsilon)
            {
                return false;
            }

            const Vector3 normalized = axis.Normalized();
            const Float32 centerDistance = Abs(Vector3::Dot(right.center - left.center, normalized));
            return centerDistance > ProjectRadius(left, normalized) + ProjectRadius(right, normalized);
        }
    }
```

Then add:

```cpp
    std::optional<ShapeRaycastHit> RaycastOrientedBox(const Ray& ray, const OrientedBox& box) noexcept
    {
        Float32 minimumDistance = 0.0f;
        Float32 maximumDistance = 3.402823466e+38f;
        Vector3 hitNormal = Vector3::UnitY();
        const Vector3 offset = box.center - ray.origin;

        for (SizeT axisIndex = 0; axisIndex < 3; ++axisIndex)
        {
            const Vector3 axis = box.axes[axisIndex];
            const Float32 extent = ComponentAt(box.halfExtents, axisIndex);
            const Float32 e = Vector3::Dot(axis, offset);
            const Float32 f = Vector3::Dot(axis, ray.direction);

            if (Abs(f) <= Math::DefaultEpsilon)
            {
                if (-e - extent > 0.0f || -e + extent < 0.0f)
                {
                    return std::nullopt;
                }
                continue;
            }

            Float32 t1 = (e + extent) / f;
            Float32 t2 = (e - extent) / f;
            Vector3 candidateNormal = axis * (f > 0.0f ? -1.0f : 1.0f);
            if (t1 > t2)
            {
                std::swap(t1, t2);
                candidateNormal = -candidateNormal;
            }

            if (t1 > minimumDistance)
            {
                minimumDistance = t1;
                hitNormal = candidateNormal;
            }
            maximumDistance = std::min(maximumDistance, t2);
            if (minimumDistance > maximumDistance)
            {
                return std::nullopt;
            }
        }

        ShapeRaycastHit hit;
        hit.distance = minimumDistance;
        hit.position = ray.GetPoint(minimumDistance);
        hit.normal = hitNormal.Normalized();
        return hit;
    }

    bool Overlaps(const Sphere& left, const Sphere& right) noexcept
    {
        const Float32 radius = left.radius + right.radius;
        return (left.center - right.center).LengthSquared() <= radius * radius;
    }

    bool Overlaps(const Sphere& sphere, const OrientedBox& box) noexcept
    {
        const Vector3 closest = ClosestPointOnBox(sphere.center, box);
        return (closest - sphere.center).LengthSquared() <= sphere.radius * sphere.radius;
    }

    bool Overlaps(const OrientedBox& left, const OrientedBox& right) noexcept
    {
        for (SizeT index = 0; index < 3; ++index)
        {
            if (HasSeparatingAxis(left, right, left.axes[index]) ||
                HasSeparatingAxis(left, right, right.axes[index]))
            {
                return false;
            }
        }

        for (const Vector3& leftAxis : left.axes)
        {
            for (const Vector3& rightAxis : right.axes)
            {
                if (HasSeparatingAxis(left, right, Vector3::Cross(leftAxis, rightAxis)))
                {
                    return false;
                }
            }
        }

        return true;
    }

    Aabb BuildAabb(const Sphere& sphere) noexcept
    {
        const Vector3 extents(sphere.radius, sphere.radius, sphere.radius);
        return Aabb{sphere.center - extents, sphere.center + extents};
    }

    Aabb BuildAabb(const OrientedBox& box) noexcept
    {
        const Vector3 extents((Abs(box.axes[0].GetX()) * box.halfExtents.GetX()) +
                                  (Abs(box.axes[1].GetX()) * box.halfExtents.GetY()) +
                                  (Abs(box.axes[2].GetX()) * box.halfExtents.GetZ()),
                              (Abs(box.axes[0].GetY()) * box.halfExtents.GetX()) +
                                  (Abs(box.axes[1].GetY()) * box.halfExtents.GetY()) +
                                  (Abs(box.axes[2].GetY()) * box.halfExtents.GetZ()),
                              (Abs(box.axes[0].GetZ()) * box.halfExtents.GetX()) +
                                  (Abs(box.axes[1].GetZ()) * box.halfExtents.GetY()) +
                                  (Abs(box.axes[2].GetZ()) * box.halfExtents.GetZ()));
        return Aabb{box.center - extents, box.center + extents};
    }
```

- [ ] **Step 5: Run test to verify GREEN**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEnginePhysicsTests --output-on-failure
```

Expected: `VEnginePhysicsTests` passes.

- [ ] **Step 6: Commit**

```text
git add Tests/Unit/PhysicsTests.cpp Engine/Runtime/Physics/PhysicsGeometry.h Engine/Runtime/Physics/PhysicsGeometry.cpp
git commit -m "feat: add physics shape queries"
```

---

### Task 3: ColliderComponent And One-Collider Enforcement

**Files:**
- Modify: `Tests/Unit/PhysicsTests.cpp`
- Modify: `Engine/Runtime/Physics/ColliderComponent.h`
- Modify: `Engine/Runtime/Physics/ColliderComponent.cpp`
- Modify: `Engine/Runtime/Scene/GameObject.h`
- Modify: `Engine/Runtime/Scene/GameObject.cpp`

- [ ] **Step 1: Add failing component tests**

Add includes:

```cpp
#include "Engine/Runtime/Physics/ColliderComponent.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
```

Add tests and call them from `main()`:

```cpp
    bool TestColliderDefaultsAndSetters()
    {
        ve::Scene scene;
        ve::GameObject& object = scene.CreateGameObject("Collider");
        ve::ColliderComponent& collider = object.AddComponent<ve::ColliderComponent>();

        bool passed = true;
        passed &= Expect(collider.GetShape() == ve::ColliderShape::Box, "Collider default shape should be box");
        passed &= Expect(collider.GetCenter().IsNearlyEqual(ve::Vector3::Zero()), "Collider default center should be zero");
        passed &= Expect(collider.GetBoxSize().IsNearlyEqual(ve::Vector3::One()), "Collider default box size should be one");
        passed &= Expect(ve::NearlyEqual(collider.GetSphereRadius(), 0.5f), "Collider default sphere radius should be 0.5");
        passed &= Expect(collider.GetLayer() == 1ull, "Collider default layer should be bit 0");
        passed &= Expect(collider.GetCollidesWith() == ~0ull, "Collider default collision mask should include all layers");
        passed &= Expect(!collider.IsTrigger(), "Collider should not be trigger by default");
        passed &= Expect(collider.IsColliderEnabled(), "Collider should be enabled by default");

        collider.SetShape(ve::ColliderShape::Sphere);
        collider.SetCenter(ve::Vector3(1.0f, 2.0f, 3.0f));
        collider.SetBoxSize(ve::Vector3(2.0f, 4.0f, 6.0f));
        collider.SetSphereRadius(2.5f);
        collider.SetLayer(1ull << 40);
        collider.SetCollidesWith((1ull << 40) | (1ull << 7));
        collider.SetTrigger(true);
        collider.SetColliderEnabled(false);

        passed &= Expect(collider.GetShape() == ve::ColliderShape::Sphere, "Collider shape setter should persist");
        passed &= Expect(collider.GetCenter().IsNearlyEqual(ve::Vector3(1.0f, 2.0f, 3.0f)),
                         "Collider center setter should persist");
        passed &= Expect(collider.GetBoxSize().IsNearlyEqual(ve::Vector3(2.0f, 4.0f, 6.0f)),
                         "Collider box size setter should persist");
        passed &= Expect(ve::NearlyEqual(collider.GetSphereRadius(), 2.5f), "Collider radius setter should persist");
        passed &= Expect(collider.GetLayer() == (1ull << 40), "Collider should preserve 64-bit layer");
        passed &= Expect(collider.GetCollidesWith() == ((1ull << 40) | (1ull << 7)),
                         "Collider should preserve 64-bit mask");
        passed &= Expect(collider.IsTrigger(), "Collider trigger setter should persist");
        passed &= Expect(!collider.IsColliderEnabled(), "Collider enabled setter should persist");

        return passed;
    }

    bool TestOneColliderPerGameObject()
    {
        ve::Scene scene;
        ve::GameObject& object = scene.CreateGameObject("Object");
        ve::ColliderComponent& first = object.AddComponent<ve::ColliderComponent>();
        first.SetLayer(1ull << 3);
        ve::ColliderComponent& second = object.AddComponent<ve::ColliderComponent>();

        bool passed = true;
        passed &= Expect(&first == &second, "Adding a second collider should return the existing collider");
        passed &= Expect(object.GetComponents().size() == 1, "Object should still own one component");
        passed &= Expect(object.GetComponent<ve::ColliderComponent>() == &first, "Existing collider should remain attached");
        passed &= Expect(first.GetLayer() == (1ull << 3), "Duplicate add should not reset existing collider state");
        return passed;
    }
```

- [ ] **Step 2: Run test to verify RED**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
```

Expected: build fails because `ColliderComponent` and `ColliderShape` are not defined.

- [ ] **Step 3: Implement ColliderComponent API**

Replace `ColliderComponent.h` with:

```cpp
#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Physics/PhysicsGeometry.h"
#include "Engine/Runtime/Scene/Component.h"

namespace ve
{
    enum class ColliderShape
    {
        Box,
        Sphere,
    };

    class TransformComponent;

    class ColliderComponent final : public Component
    {
    public:
        [[nodiscard]] ColliderShape GetShape() const noexcept;
        void SetShape(ColliderShape shape);

        [[nodiscard]] const Vector3& GetCenter() const noexcept;
        void SetCenter(const Vector3& center);

        [[nodiscard]] const Vector3& GetBoxSize() const noexcept;
        void SetBoxSize(const Vector3& size);

        [[nodiscard]] Float32 GetSphereRadius() const noexcept;
        void SetSphereRadius(Float32 radius);

        [[nodiscard]] UInt64 GetLayer() const noexcept;
        void SetLayer(UInt64 layer);

        [[nodiscard]] UInt64 GetCollidesWith() const noexcept;
        void SetCollidesWith(UInt64 mask);

        [[nodiscard]] bool IsTrigger() const noexcept;
        void SetTrigger(bool trigger);

        [[nodiscard]] bool IsColliderEnabled() const noexcept;
        void SetColliderEnabled(bool enabled);

        [[nodiscard]] Sphere BuildWorldSphere(const TransformComponent& transform) const noexcept;
        [[nodiscard]] OrientedBox BuildWorldBox(const TransformComponent& transform) const noexcept;

    private:
        ColliderShape shape_ = ColliderShape::Box;
        Vector3 center_ = Vector3::Zero();
        Vector3 boxSize_ = Vector3::One();
        Float32 sphereRadius_ = 0.5f;
        UInt64 layer_ = 1ull;
        UInt64 collidesWith_ = ~0ull;
        bool isTrigger_ = false;
        bool enabled_ = true;
    };
}
```

Replace `ColliderComponent.cpp` with:

```cpp
#include "Engine/Runtime/Physics/ColliderComponent.h"

#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <algorithm>
#include <cmath>

namespace ve
{
    namespace
    {
        [[nodiscard]] Float32 Abs(Float32 value) noexcept
        {
            return std::abs(value);
        }

        [[nodiscard]] Float32 Max3(Float32 x, Float32 y, Float32 z) noexcept
        {
            return std::max(x, std::max(y, z));
        }
    }

    ColliderShape ColliderComponent::GetShape() const noexcept { return shape_; }
    void ColliderComponent::SetShape(ColliderShape shape) { GetScene().ValidateMutationAccess(); shape_ = shape; }
    const Vector3& ColliderComponent::GetCenter() const noexcept { return center_; }
    void ColliderComponent::SetCenter(const Vector3& center) { GetScene().ValidateMutationAccess(); center_ = center; }
    const Vector3& ColliderComponent::GetBoxSize() const noexcept { return boxSize_; }
    void ColliderComponent::SetBoxSize(const Vector3& size) { GetScene().ValidateMutationAccess(); boxSize_ = size; }
    Float32 ColliderComponent::GetSphereRadius() const noexcept { return sphereRadius_; }
    void ColliderComponent::SetSphereRadius(Float32 radius) { GetScene().ValidateMutationAccess(); sphereRadius_ = radius; }
    UInt64 ColliderComponent::GetLayer() const noexcept { return layer_; }
    void ColliderComponent::SetLayer(UInt64 layer) { GetScene().ValidateMutationAccess(); layer_ = layer; }
    UInt64 ColliderComponent::GetCollidesWith() const noexcept { return collidesWith_; }
    void ColliderComponent::SetCollidesWith(UInt64 mask) { GetScene().ValidateMutationAccess(); collidesWith_ = mask; }
    bool ColliderComponent::IsTrigger() const noexcept { return isTrigger_; }
    void ColliderComponent::SetTrigger(bool trigger) { GetScene().ValidateMutationAccess(); isTrigger_ = trigger; }
    bool ColliderComponent::IsColliderEnabled() const noexcept { return enabled_; }
    void ColliderComponent::SetColliderEnabled(bool enabled) { GetScene().ValidateMutationAccess(); enabled_ = enabled; }

    Sphere ColliderComponent::BuildWorldSphere(const TransformComponent& transform) const noexcept
    {
        const Matrix44& world = transform.GetWorldMatrix();
        const Vector3 axisX = world.TransformDirection(Vector3::UnitX());
        const Vector3 axisY = world.TransformDirection(Vector3::UnitY());
        const Vector3 axisZ = world.TransformDirection(Vector3::UnitZ());
        const Float32 scale = Max3(axisX.Length(), axisY.Length(), axisZ.Length());
        return Sphere{world.TransformPoint(center_), std::max(0.0f, sphereRadius_) * scale};
    }

    OrientedBox ColliderComponent::BuildWorldBox(const TransformComponent& transform) const noexcept
    {
        const Matrix44& world = transform.GetWorldMatrix();
        const Vector3 axisX = world.TransformDirection(Vector3::UnitX());
        const Vector3 axisY = world.TransformDirection(Vector3::UnitY());
        const Vector3 axisZ = world.TransformDirection(Vector3::UnitZ());
        const Float32 scaleX = axisX.Length();
        const Float32 scaleY = axisY.Length();
        const Float32 scaleZ = axisZ.Length();

        OrientedBox box;
        box.center = world.TransformPoint(center_);
        box.halfExtents = Vector3(Abs(boxSize_.GetX()) * scaleX * 0.5f,
                                  Abs(boxSize_.GetY()) * scaleY * 0.5f,
                                  Abs(boxSize_.GetZ()) * scaleZ * 0.5f);
        box.axes = {scaleX > Math::DefaultEpsilon ? axisX / scaleX : Vector3::UnitX(),
                    scaleY > Math::DefaultEpsilon ? axisY / scaleY : Vector3::UnitY(),
                    scaleZ > Math::DefaultEpsilon ? axisZ / scaleZ : Vector3::UnitZ()};
        return box;
    }
}
```

- [ ] **Step 4: Enforce one collider in GameObject**

In `GameObject.h`, change templated `AddComponent` body to:

```cpp
        T& AddComponent(Args&&... args)
        {
            static_assert(std::is_base_of_v<Component, T>, "T must derive from Component.");
            auto component = std::make_unique<T>(std::forward<Args>(args)...);
            return static_cast<T&>(AddComponent(std::move(component)));
        }
```

In `GameObject.cpp`, add include:

```cpp
#include "Engine/Runtime/Physics/ColliderComponent.h"
```

At the top of `GameObject::AddComponent(std::unique_ptr<Component> component)`, after the null assertion, add:

```cpp
        if (dynamic_cast<ColliderComponent*>(component.get()) != nullptr)
        {
            if (ColliderComponent* existingCollider = GetComponent<ColliderComponent>())
            {
                return *existingCollider;
            }
        }
```

- [ ] **Step 5: Run test to verify GREEN**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEnginePhysicsTests --output-on-failure
```

Expected: `VEnginePhysicsTests` passes.

- [ ] **Step 6: Commit**

```text
git add Tests/Unit/PhysicsTests.cpp Engine/Runtime/Physics/ColliderComponent.h Engine/Runtime/Physics/ColliderComponent.cpp Engine/Runtime/Scene/GameObject.h Engine/Runtime/Scene/GameObject.cpp
git commit -m "feat: add collider component"
```

---

### Task 4: Reflection And Scene Serialization For ColliderComponent

**Files:**
- Modify: `Tests/Unit/PhysicsTests.cpp`
- Modify: `Engine/Runtime/Reflection/ReflectionRegistry.h`
- Modify: `Engine/Runtime/Reflection/ReflectionRegistry.cpp`
- Modify: `Engine/Runtime/Scene/Serialization/SceneSerialization.cpp`

- [ ] **Step 1: Add failing serialization tests**

Add include:

```cpp
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Scene/Serialization/SceneSerialization.h"
```

Add tests and call them from `main()`:

```cpp
    bool TestColliderSerializationRoundTrip()
    {
        ve::ReflectionRegistry registry;
        ve::RegisterSceneReflectionTypes(registry);

        ve::Scene source;
        ve::GameObject& object = source.CreateGameObject("SerializedCollider");
        object.AddComponent<ve::TransformComponent>();
        ve::ColliderComponent& collider = object.AddComponent<ve::ColliderComponent>();
        collider.SetShape(ve::ColliderShape::Sphere);
        collider.SetCenter(ve::Vector3(1.0f, 2.0f, 3.0f));
        collider.SetBoxSize(ve::Vector3(2.0f, 3.0f, 4.0f));
        collider.SetSphereRadius(2.5f);
        collider.SetLayer(1ull << 45);
        collider.SetCollidesWith((1ull << 45) | (1ull << 12));
        collider.SetTrigger(true);
        collider.SetColliderEnabled(false);

        const std::string json = ve::SerializeSceneToJson(source, registry);

        ve::Scene loaded;
        bool passed = true;
        passed &= Expect(ve::DeserializeSceneFromJson(loaded, registry, json) == ve::ErrorCode::None,
                         "Collider scene should deserialize");
        ve::GameObject* loadedObject = loaded.FindGameObject(object.GetId());
        passed &= Expect(loadedObject != nullptr, "Loaded collider object should preserve id");
        const ve::ColliderComponent* loadedCollider =
            loadedObject != nullptr ? loadedObject->GetComponent<ve::ColliderComponent>() : nullptr;
        passed &= Expect(loadedCollider != nullptr, "Loaded object should have collider");
        if (loadedCollider)
        {
            passed &= Expect(loadedCollider->GetShape() == ve::ColliderShape::Sphere,
                             "Collider shape should round-trip");
            passed &= Expect(loadedCollider->GetCenter().IsNearlyEqual(ve::Vector3(1.0f, 2.0f, 3.0f)),
                             "Collider center should round-trip");
            passed &= Expect(loadedCollider->GetBoxSize().IsNearlyEqual(ve::Vector3(2.0f, 3.0f, 4.0f)),
                             "Collider box size should round-trip");
            passed &= Expect(ve::NearlyEqual(loadedCollider->GetSphereRadius(), 2.5f),
                             "Collider sphere radius should round-trip");
            passed &= Expect(loadedCollider->GetLayer() == (1ull << 45), "Collider 64-bit layer should round-trip");
            passed &= Expect(loadedCollider->GetCollidesWith() == ((1ull << 45) | (1ull << 12)),
                             "Collider 64-bit mask should round-trip");
            passed &= Expect(loadedCollider->IsTrigger(), "Collider trigger flag should round-trip");
            passed &= Expect(!loadedCollider->IsColliderEnabled(), "Collider enabled flag should round-trip");
        }
        return passed;
    }

    bool TestDuplicateColliderDeserializationSkipsSecondCollider()
    {
        ve::ReflectionRegistry registry;
        ve::RegisterSceneReflectionTypes(registry);

        const std::string json = R"({
            "version": 1,
            "scene": {"name": "Scene"},
            "gameObjects": [
                {
                    "id": 100,
                    "name": "DuplicateCollider",
                    "active": true,
                    "parent": 0,
                    "components": [
                        {
                            "type": "ColliderComponent",
                            "properties": {
                                "shape": "Box",
                                "layer": 8,
                                "collidesWith": 18446744073709551615,
                                "enabled": true
                            }
                        },
                        {
                            "type": "ColliderComponent",
                            "properties": {
                                "shape": "Sphere",
                                "layer": 16,
                                "collidesWith": 16,
                                "enabled": true
                            }
                        }
                    ]
                }
            ]
        })";

        ve::Scene scene;
        bool passed = true;
        passed &= Expect(ve::DeserializeSceneFromJson(scene, registry, json) == ve::ErrorCode::None,
                         "Duplicate collider scene should still deserialize");
        ve::GameObject* object = scene.FindGameObject(100);
        passed &= Expect(object != nullptr, "Duplicate collider object should load");
        passed &= Expect(object != nullptr && object->GetComponents().size() == 1,
                         "Duplicate collider should be skipped");
        const ve::ColliderComponent* collider = object != nullptr ? object->GetComponent<ve::ColliderComponent>() : nullptr;
        passed &= Expect(collider != nullptr && collider->GetShape() == ve::ColliderShape::Box,
                         "First collider should be preserved");
        passed &= Expect(collider != nullptr && collider->GetLayer() == 8ull,
                         "Skipped duplicate should not overwrite first collider properties");
        return passed;
    }
```

- [ ] **Step 2: Run test to verify RED**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEnginePhysicsTests --output-on-failure
```

Expected: tests fail because `ColliderComponent` is not registered through Reflection and duplicate deserialization overwrites the first collider.

- [ ] **Step 3: Add UInt64 reflection support**

In `ReflectionRegistry.h`, add `UInt64` to `ReflectedPropertyType`:

```cpp
        UInt64,
```

In `ReflectionRegistry.cpp`, include `ColliderComponent.h`:

```cpp
#include "Engine/Runtime/Physics/ColliderComponent.h"
```

Add helper:

```cpp
        [[nodiscard]] UInt64 ToUInt64(const value& jsonValue, UInt64 fallback) noexcept
        {
            if (jsonValue.is_uint64())
            {
                return jsonValue.as_uint64();
            }

            if (jsonValue.is_int64() && jsonValue.as_int64() >= 0)
            {
                return static_cast<UInt64>(jsonValue.as_int64());
            }

            return fallback;
        }
```

- [ ] **Step 4: Register ColliderShape and ColliderComponent**

In `RegisterSceneReflectionTypes`, after light enum registration, add:

```cpp
        registry.RegisterEnum(ReflectedEnumInfo{
            "ColliderShape",
            {{"Box", static_cast<Int32>(ColliderShape::Box)}, {"Sphere", static_cast<Int32>(ColliderShape::Sphere)}}});
```

Before `RegisterScriptReflectionTypes(registry);`, register collider:

```cpp
        ReflectedTypeInfo collider;
        collider.name = "ColliderComponent";
        collider.baseTypeName = "Component";
        collider.componentFactory = []() { return std::make_unique<ColliderComponent>(); };
        collider.properties.push_back(ReflectedPropertyInfo{
            "shape",
            ReflectedPropertyType::Enum,
            true,
            true,
            "ColliderShape",
            [](const Component& component)
            {
                return static_cast<const ColliderComponent&>(component).GetShape() == ColliderShape::Sphere ? "Sphere"
                                                                                                            : "Box";
            },
            [](Component& component, const value& jsonValue)
            {
                if (jsonValue.is_string())
                {
                    static_cast<ColliderComponent&>(component).SetShape(jsonValue.as_string() == "Sphere"
                                                                             ? ColliderShape::Sphere
                                                                             : ColliderShape::Box);
                }
            }});
        collider.properties.push_back(ReflectedPropertyInfo{
            "center",
            ReflectedPropertyType::Vector3,
            true,
            true,
            {},
            [](const Component& component)
            { return ToJson(static_cast<const ColliderComponent&>(component).GetCenter()); },
            [](Component& component, const value& jsonValue)
            {
                auto& colliderComponent = static_cast<ColliderComponent&>(component);
                colliderComponent.SetCenter(ToVector3(jsonValue, colliderComponent.GetCenter()));
            }});
        collider.properties.push_back(ReflectedPropertyInfo{
            "boxSize",
            ReflectedPropertyType::Vector3,
            true,
            true,
            {},
            [](const Component& component)
            { return ToJson(static_cast<const ColliderComponent&>(component).GetBoxSize()); },
            [](Component& component, const value& jsonValue)
            {
                auto& colliderComponent = static_cast<ColliderComponent&>(component);
                colliderComponent.SetBoxSize(ToVector3(jsonValue, colliderComponent.GetBoxSize()));
            }});
        collider.properties.push_back(ReflectedPropertyInfo{
            "sphereRadius",
            ReflectedPropertyType::Float32,
            true,
            true,
            {},
            [](const Component& component)
            { return static_cast<const ColliderComponent&>(component).GetSphereRadius(); },
            [](Component& component, const value& jsonValue)
            {
                if (jsonValue.is_double())
                {
                    static_cast<ColliderComponent&>(component).SetSphereRadius(static_cast<Float32>(jsonValue.as_double()));
                }
            }});
        collider.properties.push_back(ReflectedPropertyInfo{
            "layer",
            ReflectedPropertyType::UInt64,
            true,
            true,
            {},
            [](const Component& component)
            { return static_cast<std::uint64_t>(static_cast<const ColliderComponent&>(component).GetLayer()); },
            [](Component& component, const value& jsonValue)
            {
                auto& colliderComponent = static_cast<ColliderComponent&>(component);
                colliderComponent.SetLayer(ToUInt64(jsonValue, colliderComponent.GetLayer()));
            }});
        collider.properties.push_back(ReflectedPropertyInfo{
            "collidesWith",
            ReflectedPropertyType::UInt64,
            true,
            true,
            {},
            [](const Component& component)
            { return static_cast<std::uint64_t>(static_cast<const ColliderComponent&>(component).GetCollidesWith()); },
            [](Component& component, const value& jsonValue)
            {
                auto& colliderComponent = static_cast<ColliderComponent&>(component);
                colliderComponent.SetCollidesWith(ToUInt64(jsonValue, colliderComponent.GetCollidesWith()));
            }});
        collider.properties.push_back(ReflectedPropertyInfo{
            "isTrigger",
            ReflectedPropertyType::Bool,
            true,
            true,
            {},
            [](const Component& component) { return static_cast<const ColliderComponent&>(component).IsTrigger(); },
            [](Component& component, const value& jsonValue)
            {
                if (jsonValue.is_bool())
                {
                    static_cast<ColliderComponent&>(component).SetTrigger(jsonValue.as_bool());
                }
            }});
        collider.properties.push_back(ReflectedPropertyInfo{
            "enabled",
            ReflectedPropertyType::Bool,
            true,
            true,
            {},
            [](const Component& component)
            { return static_cast<const ColliderComponent&>(component).IsColliderEnabled(); },
            [](Component& component, const value& jsonValue)
            {
                if (jsonValue.is_bool())
                {
                    static_cast<ColliderComponent&>(component).SetColliderEnabled(jsonValue.as_bool());
                }
            }});
        registry.RegisterType(std::move(collider));
```

- [ ] **Step 5: Skip duplicate component deserialization**

In `SceneSerialization.cpp`, before adding the component:

```cpp
                Component* requestedComponent = component.get();
                Component& componentRef = gameObject.AddComponent(std::move(component));
                if (&componentRef != requestedComponent)
                {
                    VE_LOG_WARN_CATEGORY("Scene", "Skipping duplicate component type '{}' on GameObject '{}'.", typeName, gameObject.GetName());
                    continue;
                }
```

Replace the existing single line:

```cpp
                Component& componentRef = gameObject.AddComponent(std::move(component));
```

- [ ] **Step 6: Run test to verify GREEN**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEnginePhysicsTests --output-on-failure
```

Expected: `VEnginePhysicsTests` passes.

- [ ] **Step 7: Run scene tests because serialization changed**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineSceneTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineSceneTests --output-on-failure
```

Expected: `VEngineSceneTests` passes.

- [ ] **Step 8: Commit**

```text
git add Tests/Unit/PhysicsTests.cpp Engine/Runtime/Reflection/ReflectionRegistry.h Engine/Runtime/Reflection/ReflectionRegistry.cpp Engine/Runtime/Scene/Serialization/SceneSerialization.cpp
git commit -m "feat: serialize collider components"
```

---

### Task 5: PhysicsWorld Scene Synchronization

**Files:**
- Modify: `Tests/Unit/PhysicsTests.cpp`
- Modify: `Engine/Runtime/Physics/PhysicsWorld.h`
- Modify: `Engine/Runtime/Physics/PhysicsWorld.cpp`

- [ ] **Step 1: Add failing sync tests**

Add include:

```cpp
#include "Engine/Runtime/Physics/PhysicsWorld.h"
```

Add tests and call them from `main()`:

```cpp
    bool TestPhysicsWorldSyncBuildsColliderProxies()
    {
        ve::Scene scene;
        ve::GameObject& sphereObject = scene.CreateGameObject("Sphere");
        ve::TransformComponent& sphereTransform = sphereObject.AddComponent<ve::TransformComponent>();
        sphereTransform.SetLocalPosition(ve::Vector3(2.0f, 0.0f, 0.0f));
        sphereTransform.SetLocalScale(ve::Vector3(2.0f, 3.0f, 4.0f));
        ve::ColliderComponent& sphereCollider = sphereObject.AddComponent<ve::ColliderComponent>();
        sphereCollider.SetShape(ve::ColliderShape::Sphere);
        sphereCollider.SetCenter(ve::Vector3(1.0f, 0.0f, 0.0f));
        sphereCollider.SetSphereRadius(0.5f);
        sphereCollider.SetLayer(1ull << 9);

        ve::GameObject& boxObject = scene.CreateGameObject("Box");
        ve::TransformComponent& boxTransform = boxObject.AddComponent<ve::TransformComponent>();
        boxTransform.SetLocalPosition(ve::Vector3(-1.0f, 0.0f, 0.0f));
        boxTransform.SetLocalRotation(ve::Quaternion::FromAxisAngle(ve::Vector3::UnitY(), ve::ToRadians(90.0f)));
        ve::ColliderComponent& boxCollider = boxObject.AddComponent<ve::ColliderComponent>();
        boxCollider.SetBoxSize(ve::Vector3(2.0f, 4.0f, 6.0f));

        scene.UpdateTransforms();

        ve::PhysicsWorld world;
        world.SyncFromScene(scene);

        bool passed = true;
        passed &= Expect(world.GetColliderCount() == 2, "PhysicsWorld should sync active collider proxies");
        world.Clear();
        passed &= Expect(world.GetColliderCount() == 0, "PhysicsWorld::Clear should remove synced proxies");
        return passed;
    }

    bool TestPhysicsWorldSyncSkipsInactiveDisabledAndMissingTransform()
    {
        ve::Scene scene;
        ve::GameObject& active = scene.CreateGameObject("Active");
        active.AddComponent<ve::TransformComponent>();
        active.AddComponent<ve::ColliderComponent>();

        ve::GameObject& inactive = scene.CreateGameObject("Inactive");
        inactive.AddComponent<ve::TransformComponent>();
        inactive.AddComponent<ve::ColliderComponent>();
        inactive.SetActive(false);

        ve::GameObject& disabled = scene.CreateGameObject("Disabled");
        disabled.AddComponent<ve::TransformComponent>();
        disabled.AddComponent<ve::ColliderComponent>().SetColliderEnabled(false);

        ve::GameObject& missingTransform = scene.CreateGameObject("MissingTransform");
        missingTransform.AddComponent<ve::ColliderComponent>();

        scene.UpdateTransforms();
        ve::PhysicsWorld world;
        world.SyncFromScene(scene);

        bool passed = true;
        passed &= Expect(world.GetColliderCount() == 1,
                         "Sync should skip inactive, disabled, and missing-transform colliders");
        return passed;
    }
```

- [ ] **Step 2: Run test to verify RED**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
```

Expected: build fails because `PhysicsWorld` and proxy APIs are not defined.

- [ ] **Step 3: Implement PhysicsWorld sync API**

Replace `PhysicsWorld.h` with:

```cpp
#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Physics/ColliderComponent.h"
#include "Engine/Runtime/Physics/PhysicsGeometry.h"
#include "Engine/Runtime/Scene/SceneTypes.h"

#include <optional>
#include <vector>

namespace ve
{
    class Scene;

    struct ColliderProxy
    {
        SceneObjectId gameObjectId = InvalidSceneObjectId;
        const ColliderComponent* collider = nullptr;
        ColliderShape shape = ColliderShape::Box;
        UInt64 layer = 1ull;
        UInt64 collidesWith = ~0ull;
        bool isTrigger = false;
        Sphere sphere;
        OrientedBox box;
        Aabb bounds;
    };

    struct RaycastHit
    {
        SceneObjectId gameObjectId = InvalidSceneObjectId;
        const ColliderComponent* collider = nullptr;
        Float32 distance = 0.0f;
        Vector3 position = Vector3::Zero();
        Vector3 normal = Vector3::UnitY();
        UInt64 layer = 1ull;
        bool isTrigger = false;
    };

    struct OverlapHit
    {
        SceneObjectId gameObjectId = InvalidSceneObjectId;
        const ColliderComponent* collider = nullptr;
        UInt64 layer = 1ull;
        bool isTrigger = false;
    };

    class PhysicsWorld
    {
    public:
        void Clear();
        void SyncFromScene(const Scene& scene);

        [[nodiscard]] SizeT GetColliderCount() const noexcept;

    private:
        std::vector<ColliderProxy> colliderProxies_;
    };
}
```

Replace `PhysicsWorld.cpp` with:

```cpp
#include "Engine/Runtime/Physics/PhysicsWorld.h"

#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

namespace ve
{
    namespace
    {
        void SyncGameObject(const GameObject& gameObject, std::vector<ColliderProxy>& proxies)
        {
            if (!gameObject.IsActiveInHierarchy())
            {
                return;
            }

            const TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
            const ColliderComponent* collider = gameObject.GetComponent<ColliderComponent>();
            if (transform != nullptr && collider != nullptr && collider->IsColliderEnabled())
            {
                ColliderProxy proxy;
                proxy.gameObjectId = gameObject.GetId();
                proxy.collider = collider;
                proxy.shape = collider->GetShape();
                proxy.layer = collider->GetLayer();
                proxy.collidesWith = collider->GetCollidesWith();
                proxy.isTrigger = collider->IsTrigger();
                if (proxy.shape == ColliderShape::Sphere)
                {
                    proxy.sphere = collider->BuildWorldSphere(*transform);
                    proxy.bounds = BuildAabb(proxy.sphere);
                }
                else
                {
                    proxy.box = collider->BuildWorldBox(*transform);
                    proxy.bounds = BuildAabb(proxy.box);
                }
                proxies.push_back(proxy);
            }

            for (const GameObject* child : gameObject.GetChildren())
            {
                SyncGameObject(*child, proxies);
            }
        }
    }

    void PhysicsWorld::Clear()
    {
        colliderProxies_.clear();
    }

    void PhysicsWorld::SyncFromScene(const Scene& scene)
    {
        colliderProxies_.clear();
        for (const GameObject* root : scene.GetRootGameObjects())
        {
            SyncGameObject(*root, colliderProxies_);
        }
    }

    SizeT PhysicsWorld::GetColliderCount() const noexcept
    {
        return colliderProxies_.size();
    }
}
```

- [ ] **Step 4: Run test to verify GREEN**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEnginePhysicsTests --output-on-failure
```

Expected: `VEnginePhysicsTests` passes.

- [ ] **Step 5: Commit**

```text
git add Tests/Unit/PhysicsTests.cpp Engine/Runtime/Physics/PhysicsWorld.h Engine/Runtime/Physics/PhysicsWorld.cpp
git commit -m "feat: sync collider query proxies"
```

---

### Task 6: PhysicsWorld Raycast Queries

**Files:**
- Modify: `Tests/Unit/PhysicsTests.cpp`
- Modify: `Engine/Runtime/Physics/PhysicsWorld.h`
- Modify: `Engine/Runtime/Physics/PhysicsWorld.cpp`

- [ ] **Step 1: Add failing raycast tests**

Add tests and call them from `main()`:

```cpp
    bool TestPhysicsWorldRaycastClosestAndAll()
    {
        ve::Scene scene;
        ve::GameObject& nearObject = scene.CreateGameObject("Near");
        nearObject.AddComponent<ve::TransformComponent>().SetLocalPosition(ve::Vector3(0.0f, 0.0f, -2.0f));
        nearObject.AddComponent<ve::ColliderComponent>();

        ve::GameObject& farObject = scene.CreateGameObject("Far");
        farObject.AddComponent<ve::TransformComponent>().SetLocalPosition(ve::Vector3(0.0f, 0.0f, 2.0f));
        farObject.AddComponent<ve::ColliderComponent>();

        scene.UpdateTransforms();
        ve::PhysicsWorld world;
        world.SyncFromScene(scene);

        const ve::Ray ray(ve::Vector3(0.0f, 0.0f, -5.0f), ve::Vector3::UnitZ());
        const auto closest = world.RaycastClosest(ray);
        const std::vector<ve::RaycastHit> allHits = world.RaycastAll(ray);

        bool passed = true;
        passed &= Expect(closest.has_value(), "Closest raycast should hit");
        passed &= Expect(closest && closest->gameObjectId == nearObject.GetId(), "Closest raycast should return nearest object");
        passed &= Expect(allHits.size() == 2, "RaycastAll should return both hits");
        passed &= Expect(allHits.size() == 2 && allHits[0].gameObjectId == nearObject.GetId(),
                         "RaycastAll should sort nearest first");
        passed &= Expect(allHits.size() == 2 && allHits[1].gameObjectId == farObject.GetId(),
                         "RaycastAll should include farther hit second");
        return passed;
    }

    bool TestPhysicsWorldRaycastFilteringAndSyncBoundary()
    {
        ve::Scene scene;
        ve::GameObject& object = scene.CreateGameObject("Layered");
        ve::TransformComponent& transform = object.AddComponent<ve::TransformComponent>();
        transform.SetLocalPosition(ve::Vector3(0.0f, 0.0f, 0.0f));
        ve::ColliderComponent& collider = object.AddComponent<ve::ColliderComponent>();
        collider.SetLayer(1ull << 50);
        collider.SetTrigger(true);

        scene.UpdateTransforms();
        ve::PhysicsWorld world;
        world.SyncFromScene(scene);

        const ve::Ray ray(ve::Vector3(0.0f, 0.0f, -5.0f), ve::Vector3::UnitZ());

        bool passed = true;
        passed &= Expect(!world.RaycastClosest(ray, ~0ull, false).has_value(),
                         "Raycast should exclude triggers by default");
        passed &= Expect(world.RaycastClosest(ray, 1ull << 50, true).has_value(),
                         "Raycast should include trigger when requested");
        passed &= Expect(!world.RaycastClosest(ray, 1ull << 3, true).has_value(),
                         "Raycast should respect 64-bit query mask");

        transform.SetLocalPosition(ve::Vector3(10.0f, 0.0f, 0.0f));
        scene.UpdateTransforms();
        passed &= Expect(world.RaycastClosest(ray, 1ull << 50, true).has_value(),
                         "Raycast should use previous sync state after transform changes");
        world.SyncFromScene(scene);
        passed &= Expect(!world.RaycastClosest(ray, 1ull << 50, true).has_value(),
                         "Raycast should observe transform change after sync");
        return passed;
    }
```

- [ ] **Step 2: Run test to verify RED**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
```

Expected: build fails because `PhysicsWorld::RaycastClosest` and `RaycastAll` are not defined.

- [ ] **Step 3: Add raycast API declarations**

In `PhysicsWorld.h`, add public methods:

```cpp
        [[nodiscard]] std::optional<RaycastHit>
        RaycastClosest(const Ray& ray, UInt64 queryMask = ~0ull, bool includeTriggers = false) const;

        [[nodiscard]] std::vector<RaycastHit>
        RaycastAll(const Ray& ray, UInt64 queryMask = ~0ull, bool includeTriggers = false) const;
```

- [ ] **Step 4: Implement raycast queries**

In `PhysicsWorld.cpp`, add includes:

```cpp
#include <algorithm>
```

Add helpers in the anonymous namespace:

```cpp
        [[nodiscard]] bool PassesFilter(const ColliderProxy& proxy, UInt64 queryMask, bool includeTriggers) noexcept
        {
            return (proxy.layer & queryMask) != 0 && (includeTriggers || !proxy.isTrigger);
        }

        [[nodiscard]] std::optional<ShapeRaycastHit> RaycastProxy(const Ray& ray, const ColliderProxy& proxy) noexcept
        {
            return proxy.shape == ColliderShape::Sphere ? RaycastSphere(ray, proxy.sphere)
                                                        : RaycastOrientedBox(ray, proxy.box);
        }

        [[nodiscard]] RaycastHit MakeRaycastHit(const ColliderProxy& proxy, const ShapeRaycastHit& shapeHit) noexcept
        {
            RaycastHit hit;
            hit.gameObjectId = proxy.gameObjectId;
            hit.collider = proxy.collider;
            hit.distance = shapeHit.distance;
            hit.position = shapeHit.position;
            hit.normal = shapeHit.normal;
            hit.layer = proxy.layer;
            hit.isTrigger = proxy.isTrigger;
            return hit;
        }
```

Add methods:

```cpp
    std::optional<RaycastHit>
    PhysicsWorld::RaycastClosest(const Ray& ray, UInt64 queryMask, bool includeTriggers) const
    {
        std::optional<RaycastHit> closest;
        for (const ColliderProxy& proxy : colliderProxies_)
        {
            if (!PassesFilter(proxy, queryMask, includeTriggers))
            {
                continue;
            }

            const std::optional<ShapeRaycastHit> shapeHit = RaycastProxy(ray, proxy);
            if (!shapeHit)
            {
                continue;
            }

            RaycastHit hit = MakeRaycastHit(proxy, *shapeHit);
            if (!closest || hit.distance < closest->distance ||
                (NearlyEqual(hit.distance, closest->distance) && hit.gameObjectId < closest->gameObjectId))
            {
                closest = hit;
            }
        }

        return closest;
    }

    std::vector<RaycastHit>
    PhysicsWorld::RaycastAll(const Ray& ray, UInt64 queryMask, bool includeTriggers) const
    {
        std::vector<RaycastHit> hits;
        for (const ColliderProxy& proxy : colliderProxies_)
        {
            if (!PassesFilter(proxy, queryMask, includeTriggers))
            {
                continue;
            }

            const std::optional<ShapeRaycastHit> shapeHit = RaycastProxy(ray, proxy);
            if (shapeHit)
            {
                hits.push_back(MakeRaycastHit(proxy, *shapeHit));
            }
        }

        std::sort(hits.begin(),
                  hits.end(),
                  [](const RaycastHit& left, const RaycastHit& right)
                  {
                      if (!NearlyEqual(left.distance, right.distance))
                      {
                          return left.distance < right.distance;
                      }
                      return left.gameObjectId < right.gameObjectId;
                  });
        return hits;
    }
```

- [ ] **Step 5: Run test to verify GREEN**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEnginePhysicsTests --output-on-failure
```

Expected: `VEnginePhysicsTests` passes.

- [ ] **Step 6: Commit**

```text
git add Tests/Unit/PhysicsTests.cpp Engine/Runtime/Physics/PhysicsWorld.h Engine/Runtime/Physics/PhysicsWorld.cpp
git commit -m "feat: add physics raycast queries"
```

---

### Task 7: PhysicsWorld Overlap Queries

**Files:**
- Modify: `Tests/Unit/PhysicsTests.cpp`
- Modify: `Engine/Runtime/Physics/PhysicsWorld.h`
- Modify: `Engine/Runtime/Physics/PhysicsWorld.cpp`

- [ ] **Step 1: Add failing overlap tests**

Add tests and call them from `main()`:

```cpp
    bool TestPhysicsWorldOverlapSphereAndBox()
    {
        ve::Scene scene;
        ve::GameObject& sphereObject = scene.CreateGameObject("Sphere");
        sphereObject.AddComponent<ve::TransformComponent>().SetLocalPosition(ve::Vector3(0.0f, 0.0f, 0.0f));
        ve::ColliderComponent& sphereCollider = sphereObject.AddComponent<ve::ColliderComponent>();
        sphereCollider.SetShape(ve::ColliderShape::Sphere);

        ve::GameObject& boxObject = scene.CreateGameObject("Box");
        boxObject.AddComponent<ve::TransformComponent>().SetLocalPosition(ve::Vector3(2.0f, 0.0f, 0.0f));
        boxObject.AddComponent<ve::ColliderComponent>();

        scene.UpdateTransforms();
        ve::PhysicsWorld world;
        world.SyncFromScene(scene);

        const std::vector<ve::OverlapHit> sphereHits =
            world.OverlapSphere(ve::Sphere{ve::Vector3(0.75f, 0.0f, 0.0f), 1.0f});
        ve::OrientedBox overlapBox;
        overlapBox.center = ve::Vector3(2.0f, 0.0f, 0.0f);
        overlapBox.halfExtents = ve::Vector3(0.75f, 0.75f, 0.75f);
        const std::vector<ve::OverlapHit> boxHits = world.OverlapBox(overlapBox);

        bool passed = true;
        passed &= Expect(sphereHits.size() == 2, "OverlapSphere should return sphere and box hits");
        passed &= Expect(sphereHits[0].gameObjectId == sphereObject.GetId(), "OverlapSphere should preserve sync order");
        passed &= Expect(sphereHits[1].gameObjectId == boxObject.GetId(), "OverlapSphere should include box hit");
        passed &= Expect(boxHits.size() == 1 && boxHits[0].gameObjectId == boxObject.GetId(),
                         "OverlapBox should return overlapping box");
        return passed;
    }

    bool TestPhysicsWorldOverlapFiltering()
    {
        ve::Scene scene;
        ve::GameObject& object = scene.CreateGameObject("TriggerLayer");
        object.AddComponent<ve::TransformComponent>();
        ve::ColliderComponent& collider = object.AddComponent<ve::ColliderComponent>();
        collider.SetLayer(1ull << 55);
        collider.SetTrigger(true);

        scene.UpdateTransforms();
        ve::PhysicsWorld world;
        world.SyncFromScene(scene);
        const ve::Sphere query{ve::Vector3::Zero(), 2.0f};

        bool passed = true;
        passed &= Expect(world.OverlapSphere(query).empty(), "OverlapSphere should exclude triggers by default");
        passed &= Expect(world.OverlapSphere(query, 1ull << 55, true).size() == 1,
                         "OverlapSphere should include trigger with matching 64-bit mask");
        passed &= Expect(world.OverlapSphere(query, 1ull << 2, true).empty(),
                         "OverlapSphere should filter non-matching 64-bit mask");
        return passed;
    }
```

- [ ] **Step 2: Run test to verify RED**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
```

Expected: build fails because `PhysicsWorld::OverlapSphere` and `OverlapBox` are not defined.

- [ ] **Step 3: Add overlap API declarations**

In `PhysicsWorld.h`, add:

```cpp
        [[nodiscard]] std::vector<OverlapHit>
        OverlapSphere(const Sphere& sphere, UInt64 queryMask = ~0ull, bool includeTriggers = false) const;

        [[nodiscard]] std::vector<OverlapHit>
        OverlapBox(const OrientedBox& box, UInt64 queryMask = ~0ull, bool includeTriggers = false) const;
```

- [ ] **Step 4: Implement overlap queries**

In `PhysicsWorld.cpp`, add helpers:

```cpp
        [[nodiscard]] bool OverlapsProxy(const Sphere& sphere, const ColliderProxy& proxy) noexcept
        {
            return proxy.shape == ColliderShape::Sphere ? Overlaps(sphere, proxy.sphere) : Overlaps(sphere, proxy.box);
        }

        [[nodiscard]] bool OverlapsProxy(const OrientedBox& box, const ColliderProxy& proxy) noexcept
        {
            return proxy.shape == ColliderShape::Sphere ? Overlaps(proxy.sphere, box) : Overlaps(box, proxy.box);
        }

        [[nodiscard]] OverlapHit MakeOverlapHit(const ColliderProxy& proxy) noexcept
        {
            OverlapHit hit;
            hit.gameObjectId = proxy.gameObjectId;
            hit.collider = proxy.collider;
            hit.layer = proxy.layer;
            hit.isTrigger = proxy.isTrigger;
            return hit;
        }
```

Add methods:

```cpp
    std::vector<OverlapHit>
    PhysicsWorld::OverlapSphere(const Sphere& sphere, UInt64 queryMask, bool includeTriggers) const
    {
        std::vector<OverlapHit> hits;
        for (const ColliderProxy& proxy : colliderProxies_)
        {
            if (PassesFilter(proxy, queryMask, includeTriggers) && OverlapsProxy(sphere, proxy))
            {
                hits.push_back(MakeOverlapHit(proxy));
            }
        }
        return hits;
    }

    std::vector<OverlapHit>
    PhysicsWorld::OverlapBox(const OrientedBox& box, UInt64 queryMask, bool includeTriggers) const
    {
        std::vector<OverlapHit> hits;
        for (const ColliderProxy& proxy : colliderProxies_)
        {
            if (PassesFilter(proxy, queryMask, includeTriggers) && OverlapsProxy(box, proxy))
            {
                hits.push_back(MakeOverlapHit(proxy));
            }
        }
        return hits;
    }
```

- [ ] **Step 5: Run test to verify GREEN**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEnginePhysicsTests --output-on-failure
```

Expected: `VEnginePhysicsTests` passes.

- [ ] **Step 6: Commit**

```text
git add Tests/Unit/PhysicsTests.cpp Engine/Runtime/Physics/PhysicsWorld.h Engine/Runtime/Physics/PhysicsWorld.cpp
git commit -m "feat: add physics overlap queries"
```

---

### Task 8: Full Verification And Docs Cross-Check

**Files:**
- Modify: `Docs/LightweightPhysicsFixedUpdateDesign.md` only if implementation discovers a needed architectural correction.
- Modify: `Docs/DevelopmentPlan.md` only if milestone scope wording must be corrected.

- [ ] **Step 1: Run physics tests**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEnginePhysicsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEnginePhysicsTests --output-on-failure
```

Expected: `VEnginePhysicsTests` passes.

- [ ] **Step 2: Run scene and asset pipeline tests**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineSceneTests
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineAssetPipelineTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R "VEngineSceneTests|VEngineAssetPipelineTests" --output-on-failure
```

Expected: `VEngineSceneTests` and `VEngineAssetPipelineTests` pass.

- [ ] **Step 3: Run full Windows tests**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
```

Expected: all registered Windows tests pass.

- [ ] **Step 4: Self-review the implementation against the spec**

Check:

```text
Docs/superpowers/specs/2026-05-28-lightweight-physics-query-world-design.md
```

Verify:

- Runtime queries read `PhysicsWorld` proxies, not live scene state.
- Transform changes after sync are invisible until the next sync.
- `layer`, `collidesWith`, and query masks are `UInt64`.
- `ColliderComponent` serializes and deserializes all properties.
- Duplicate colliders are not added or deserialized onto one object.
- No `RigidBodyComponent`, fixed-step scheduler, collision response, Editor UI, or C# API slipped into this slice.

- [ ] **Step 5: Commit final verification/docs adjustments if any**

If docs were adjusted:

```text
git add Docs/LightweightPhysicsFixedUpdateDesign.md Docs/DevelopmentPlan.md
git commit -m "docs: clarify physics query world slice"
```

If no docs changed, do not create an empty commit.
