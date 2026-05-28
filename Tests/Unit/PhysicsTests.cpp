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

    bool ExpectNearlyEqual(ve::Float32 actual, ve::Float32 expected, const char* message)
    {
        return Expect(ve::NearlyEqual(actual, expected, 1.0e-4f), message);
    }

    bool ExpectVectorNearlyEqual(const ve::Vector3& actual, const ve::Vector3& expected, const char* message)
    {
        return Expect(actual.IsNearlyEqual(expected, 1.0e-4f), message);
    }

    bool TestRayGetPointUsesNormalizedDirectionAfterMutation()
    {
        ve::Ray ray(ve::Vector3(0.0f, 0.0f, -5.0f), ve::Vector3::UnitZ());
        ray.direction = ve::Vector3(0.0f, 0.0f, 2.0f);

        bool passed = true;
        passed &= ExpectVectorNearlyEqual(ray.GetNormalizedDirection(), ve::Vector3::UnitZ(),
                                          "Ray normalized direction should reflect mutable direction");
        passed &= ExpectVectorNearlyEqual(ray.GetPoint(4.0f), ve::Vector3(0.0f, 0.0f, -1.0f),
                                          "Ray point should use normalized mutable direction");
        return passed;
    }

    bool TestRayGetPointReturnsOriginAfterZeroDirectionMutation()
    {
        const ve::Vector3 origin(1.0f, 2.0f, 3.0f);
        ve::Ray ray(origin, ve::Vector3::UnitY());
        ray.direction = ve::Vector3::Zero();

        bool passed = true;
        passed &= ExpectVectorNearlyEqual(ray.GetNormalizedDirection(), ve::Vector3::Zero(),
                                          "Ray normalized direction should be zero for mutable zero direction");
        passed &= ExpectVectorNearlyEqual(ray.GetPoint(4.0f), origin,
                                          "Ray point should stay at origin for mutable zero direction");
        return passed;
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
            passed &= ExpectNearlyEqual(hit->distance, 4.0f, "Sphere hit distance should be nearest surface");
            passed &= ExpectVectorNearlyEqual(hit->position, ve::Vector3(0.0f, 0.0f, -1.0f),
                                              "Sphere hit position should be on near cap");
            passed &= ExpectVectorNearlyEqual(hit->normal, ve::Vector3(0.0f, 0.0f, -1.0f),
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
            passed &= ExpectNearlyEqual(hit->distance, 4.0f,
                                        "Mutated direction hit distance should remain world-space");
            passed &= ExpectVectorNearlyEqual(hit->position, ve::Vector3(0.0f, 0.0f, -1.0f),
                                              "Mutated direction hit position should use normalized direction");
            passed &= ExpectVectorNearlyEqual(hit->normal, ve::Vector3(0.0f, 0.0f, -1.0f),
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
        const ve::Quaternion rotation = ve::Quaternion::FromAxisAngle(ve::Vector3::UnitY(), ve::ToRadians(45.0f));
        const ve::OrientedBox box =
            ve::MakeOrientedBox(ve::Vector3::Zero(), ve::Vector3(1.0f, 2.0f, 0.5f), rotation);

        const ve::Ray ray(ve::Vector3(0.0f, 0.0f, -5.0f), ve::Vector3::UnitZ());
        const auto hit = ve::RaycastOrientedBox(ray, box);

        bool passed = true;
        passed &= Expect(hit.has_value(), "Ray should hit rotated oriented box");
        if (hit)
        {
            passed &= ExpectNearlyEqual(hit->distance, 4.2928934f, "Rotated box hit distance should hit front face");
            passed &= ExpectVectorNearlyEqual(hit->position, ve::Vector3(0.0f, 0.0f, -0.7071066f),
                                              "Rotated box hit position should be on the front face");
            passed &= ExpectVectorNearlyEqual(hit->normal, ve::Vector3(-0.7071068f, 0.0f, -0.7071068f),
                                              "Rotated box hit normal should point outward from front face");
        }
        return passed;
    }

    bool TestMakeOrientedBoxBuildsOrthonormalAxes()
    {
        const ve::Quaternion rotation =
            ve::Quaternion::FromEulerXYZ(ve::ToRadians(20.0f), ve::ToRadians(-30.0f), ve::ToRadians(10.0f));
        const ve::OrientedBox box =
            ve::MakeOrientedBox(ve::Vector3(1.0f, 2.0f, 3.0f), ve::Vector3(1.0f, 2.0f, 3.0f), rotation);

        bool passed = true;
        passed &= ExpectVectorNearlyEqual(box.center, ve::Vector3(1.0f, 2.0f, 3.0f),
                                          "Factory should preserve oriented box center");
        passed &= ExpectVectorNearlyEqual(box.halfExtents, ve::Vector3(1.0f, 2.0f, 3.0f),
                                          "Factory should preserve oriented box half extents");
        passed &= ExpectVectorNearlyEqual(box.axes[0], rotation.RotateVector(ve::Vector3::UnitX()),
                                          "Factory should rotate local X axis");
        passed &= ExpectVectorNearlyEqual(box.axes[1], rotation.RotateVector(ve::Vector3::UnitY()),
                                          "Factory should rotate local Y axis");
        passed &= ExpectVectorNearlyEqual(box.axes[2], rotation.RotateVector(ve::Vector3::UnitZ()),
                                          "Factory should rotate local Z axis");
        passed &= ExpectNearlyEqual(box.axes[0].Length(), 1.0f, "Factory X axis should be normalized");
        passed &= ExpectNearlyEqual(box.axes[1].Length(), 1.0f, "Factory Y axis should be normalized");
        passed &= ExpectNearlyEqual(box.axes[2].Length(), 1.0f, "Factory Z axis should be normalized");
        passed &= ExpectNearlyEqual(ve::Vector3::Dot(box.axes[0], box.axes[1]), 0.0f,
                                    "Factory X and Y axes should be orthogonal");
        passed &= ExpectNearlyEqual(ve::Vector3::Dot(box.axes[0], box.axes[2]), 0.0f,
                                    "Factory X and Z axes should be orthogonal");
        passed &= ExpectNearlyEqual(ve::Vector3::Dot(box.axes[1], box.axes[2]), 0.0f,
                                    "Factory Y and Z axes should be orthogonal");
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
            passed &= ExpectNearlyEqual(hit->distance, 4.0f,
                                        "Mutated oriented box direction hit distance should remain world-space");
            passed &= ExpectVectorNearlyEqual(hit->position, ve::Vector3(0.0f, 0.0f, -1.0f),
                                              "Mutated oriented box direction hit position should use normalized "
                                              "direction");
            passed &= ExpectVectorNearlyEqual(hit->normal, ve::Vector3(0.0f, 0.0f, -1.0f),
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

    bool TestSphereOverlaps()
    {
        const ve::Sphere a{ve::Vector3::Zero(), 1.0f};
        const ve::Sphere b{ve::Vector3(1.5f, 0.0f, 0.0f), 1.0f};

        bool passed = true;
        passed &= Expect(ve::Overlaps(a, b), "Spheres should overlap when centers are closer than radii sum");
        passed &= Expect(ve::Overlaps(a, ve::Sphere{ve::Vector3(2.0f, 0.0f, 0.0f), 1.0f}),
                         "Spheres should overlap when touching exactly at one point");
        passed &= Expect(!ve::Overlaps(a, ve::Sphere{ve::Vector3(2.01f, 0.0f, 0.0f), 1.0f}),
                         "Spheres should miss when separated beyond their radii sum");
        return passed;
    }

    bool TestSphereAndBoxOverlaps()
    {
        const ve::Sphere sphere{ve::Vector3(1.5f, 0.0f, 0.0f), 1.0f};
        ve::OrientedBox box;
        box.center = ve::Vector3(3.0f, 0.0f, 0.0f);
        box.halfExtents = ve::Vector3(1.0f, 1.0f, 1.0f);

        bool passed = true;
        passed &= Expect(ve::Overlaps(sphere, box), "Sphere should overlap oriented box");
        passed &= Expect(ve::Overlaps(ve::Sphere{ve::Vector3(1.5f, 0.0f, 0.0f), 0.5f}, box),
                         "Sphere should overlap oriented box when touching exactly at one point");
        passed &= Expect(!ve::Overlaps(ve::Sphere{ve::Vector3(1.49f, 0.0f, 0.0f), 0.5f}, box),
                         "Sphere should miss oriented box when separated from closest face");
        return passed;
    }

    bool TestBuildSphereAabb()
    {
        const ve::Aabb bounds = ve::BuildAabb(ve::Sphere{ve::Vector3(1.0f, 2.0f, 3.0f), 2.5f});

        bool passed = true;
        passed &= ExpectVectorNearlyEqual(bounds.minimum, ve::Vector3(-1.5f, -0.5f, 0.5f),
                                          "Sphere AABB minimum should be center minus radius");
        passed &= ExpectVectorNearlyEqual(bounds.maximum, ve::Vector3(3.5f, 4.5f, 5.5f),
                                          "Sphere AABB maximum should be center plus radius");
        return passed;
    }

    bool TestBuildRotatedOrientedBoxAabb()
    {
        const ve::Quaternion rotation = ve::Quaternion::FromAxisAngle(ve::Vector3::UnitZ(), ve::ToRadians(90.0f));
        const ve::OrientedBox box =
            ve::MakeOrientedBox(ve::Vector3(4.0f, 5.0f, 6.0f), ve::Vector3(1.0f, 2.0f, 3.0f), rotation);
        const ve::Aabb bounds = ve::BuildAabb(box);

        bool passed = true;
        passed &= ExpectVectorNearlyEqual(bounds.minimum, ve::Vector3(2.0f, 4.0f, 3.0f),
                                          "Rotated oriented box AABB minimum should include rotated extents");
        passed &= ExpectVectorNearlyEqual(bounds.maximum, ve::Vector3(6.0f, 6.0f, 9.0f),
                                          "Rotated oriented box AABB maximum should include rotated extents");
        return passed;
    }

    bool TestOrientedBoxSatAndAabb()
    {
        ve::OrientedBox first;
        first.center = ve::Vector3::Zero();
        first.halfExtents = ve::Vector3(1.0f, 1.0f, 1.0f);

        const ve::Quaternion rotation = ve::Quaternion::FromAxisAngle(ve::Vector3::UnitZ(), ve::ToRadians(20.0f));
        const ve::OrientedBox second =
            ve::MakeOrientedBox(ve::Vector3(1.25f, 0.0f, 0.0f), ve::Vector3(1.0f, 1.0f, 1.0f), rotation);

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

    bool TestOrientedBoxCrossProductAxisSeparates()
    {
        const ve::OrientedBox first{ve::Vector3::Zero(),
                                   ve::Vector3(1.0f, 1.0f, 1.0f),
                                   {ve::Vector3::UnitX(), ve::Vector3::UnitY(), ve::Vector3::UnitZ()}};
        const ve::Quaternion rotation = ve::Quaternion::FromEulerXYZ(ve::ToRadians(-45.0f),
                                                                     ve::ToRadians(-45.0f),
                                                                     ve::ToRadians(-45.0f));
        const ve::OrientedBox second =
            ve::MakeOrientedBox(ve::Vector3(-2.5f, -2.5f, 0.0f), ve::Vector3(1.0f, 1.0f, 1.0f), rotation);

        return Expect(!ve::Overlaps(first, second),
                      "Skewed boxes should separate on a cross-product edge axis");
    }
} // namespace

int main()
{
    bool passed = true;
    passed &= TestRayGetPointUsesNormalizedDirectionAfterMutation();
    passed &= TestRayGetPointReturnsOriginAfterZeroDirectionMutation();
    passed &= TestRayHitsSphere();
    passed &= TestRayMissesSphere();
    passed &= TestRaycastSphereUsesWorldDistanceAfterDirectionMutation();
    passed &= TestRaycastSphereRejectsMutatedZeroDirection();
    passed &= TestRayHitsRotatedBox();
    passed &= TestMakeOrientedBoxBuildsOrthonormalAxes();
    passed &= TestRaycastOrientedBoxUsesWorldDistanceAfterDirectionMutation();
    passed &= TestRaycastOrientedBoxRejectsMutatedZeroDirection();
    passed &= TestSphereOverlaps();
    passed &= TestSphereAndBoxOverlaps();
    passed &= TestBuildSphereAabb();
    passed &= TestBuildRotatedOrientedBoxAabb();
    passed &= TestOrientedBoxSatAndAabb();
    passed &= TestOrientedBoxCrossProductAxisSeparates();
    return passed ? 0 : 1;
}
