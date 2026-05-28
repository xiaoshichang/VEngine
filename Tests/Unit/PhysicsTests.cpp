#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Quaternion.h"
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

    bool TestRaycastSphereUsesWorldDistanceAfterDirectionMutation()
    {
        ve::Ray ray(ve::Vector3(0.0f, 0.0f, -5.0f), ve::Vector3::UnitZ());
        ray.direction = ve::Vector3(0.0f, 0.0f, 2.0f);
        const ve::Sphere sphere{ve::Vector3::Zero(), 1.0f};
        const auto hit = ve::RaycastSphere(ray, sphere);

        bool passed = true;
        passed &= Expect(hit.has_value(), "Ray with mutated non-unit direction should hit sphere");
        if (hit)
        {
            passed &= Expect(ve::NearlyEqual(hit->distance, 4.0f),
                             "Mutated direction hit distance should remain world-space");
            passed &= Expect(hit->position.IsNearlyEqual(ve::Vector3(0.0f, 0.0f, -1.0f)),
                             "Mutated direction hit position should use normalized direction");
            passed &= Expect(hit->normal.IsNearlyEqual(ve::Vector3(0.0f, 0.0f, -1.0f)),
                             "Mutated direction hit normal should point outward");
        }

        return passed;
    }

    bool TestRaycastSphereRejectsMutatedZeroDirection()
    {
        ve::Ray ray(ve::Vector3(0.0f, 0.0f, -5.0f), ve::Vector3::UnitZ());
        ray.direction = ve::Vector3::Zero();
        const ve::Sphere sphere{ve::Vector3::Zero(), 1.0f};
        return Expect(!ve::RaycastSphere(ray, sphere).has_value(),
                      "Ray with mutated zero direction should not produce a sphere hit");
    }

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

    bool TestRaycastOrientedBoxUsesWorldDistanceAfterDirectionMutation()
    {
        ve::Ray ray(ve::Vector3(0.0f, 0.0f, -5.0f), ve::Vector3::UnitZ());
        ray.direction = ve::Vector3(0.0f, 0.0f, 2.0f);
        ve::OrientedBox box;
        box.center = ve::Vector3::Zero();
        box.halfExtents = ve::Vector3(1.0f, 1.0f, 1.0f);
        const auto hit = ve::RaycastOrientedBox(ray, box);

        bool passed = true;
        passed &= Expect(hit.has_value(), "Ray with mutated non-unit direction should hit oriented box");
        if (hit)
        {
            passed &= Expect(ve::NearlyEqual(hit->distance, 4.0f),
                             "Mutated oriented box direction hit distance should remain world-space");
            passed &= Expect(hit->position.IsNearlyEqual(ve::Vector3(0.0f, 0.0f, -1.0f)),
                             "Mutated oriented box direction hit position should use normalized direction");
            passed &= Expect(hit->normal.IsNearlyEqual(ve::Vector3(0.0f, 0.0f, -1.0f)),
                             "Mutated oriented box direction hit normal should point outward");
        }

        return passed;
    }

    bool TestRaycastOrientedBoxRejectsMutatedZeroDirection()
    {
        ve::Ray ray(ve::Vector3(0.0f, 0.0f, -5.0f), ve::Vector3::UnitZ());
        ray.direction = ve::Vector3::Zero();
        ve::OrientedBox box;
        box.center = ve::Vector3::Zero();
        box.halfExtents = ve::Vector3(1.0f, 1.0f, 1.0f);
        return Expect(!ve::RaycastOrientedBox(ray, box).has_value(),
                      "Ray with mutated zero direction should not produce an oriented box hit");
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
} // namespace

int main()
{
    bool passed = true;
    passed &= TestRayHitsSphere();
    passed &= TestRayMissesSphere();
    passed &= TestRaycastSphereUsesWorldDistanceAfterDirectionMutation();
    passed &= TestRaycastSphereRejectsMutatedZeroDirection();
    passed &= TestRayHitsRotatedBox();
    passed &= TestRaycastOrientedBoxUsesWorldDistanceAfterDirectionMutation();
    passed &= TestRaycastOrientedBoxRejectsMutatedZeroDirection();
    passed &= TestSphereAndBoxOverlaps();
    passed &= TestOrientedBoxSatAndAabb();
    return passed ? 0 : 1;
}
