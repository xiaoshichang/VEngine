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
} // namespace

int main()
{
    bool passed = true;
    passed &= TestRayHitsSphere();
    passed &= TestRayMissesSphere();
    passed &= TestRaycastSphereUsesWorldDistanceAfterDirectionMutation();
    passed &= TestRaycastSphereRejectsMutatedZeroDirection();
    return passed ? 0 : 1;
}
