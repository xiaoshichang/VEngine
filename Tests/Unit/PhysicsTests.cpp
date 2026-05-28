#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Physics/ColliderComponent.h"
#include "Engine/Runtime/Physics/PhysicsGeometry.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/Serialization/SceneSerialization.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <iostream>
#include <string>

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

    bool ExpectOrthonormalAxes(const ve::OrientedBox& box, const char* message)
    {
        bool passed = true;
        passed &= ExpectNearlyEqual(box.axes[0].Length(), 1.0f, message);
        passed &= ExpectNearlyEqual(box.axes[1].Length(), 1.0f, message);
        passed &= ExpectNearlyEqual(box.axes[2].Length(), 1.0f, message);
        passed &= ExpectNearlyEqual(ve::Vector3::Dot(box.axes[0], box.axes[1]), 0.0f, message);
        passed &= ExpectNearlyEqual(ve::Vector3::Dot(box.axes[0], box.axes[2]), 0.0f, message);
        passed &= ExpectNearlyEqual(ve::Vector3::Dot(box.axes[1], box.axes[2]), 0.0f, message);
        return passed;
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

    bool TestColliderDefaultsAndSetters()
    {
        ve::Scene scene;
        ve::GameObject& object = scene.CreateGameObject("Collider");
        ve::ColliderComponent& collider = object.AddComponent<ve::ColliderComponent>();

        bool passed = true;
        passed &= Expect(collider.GetShape() == ve::ColliderShape::Box, "Collider default shape should be box");
        passed &= Expect(collider.GetCenter().IsNearlyEqual(ve::Vector3::Zero()),
                         "Collider default center should be zero");
        passed &= Expect(collider.GetBoxSize().IsNearlyEqual(ve::Vector3::One()),
                         "Collider default box size should be one");
        passed &= Expect(ve::NearlyEqual(collider.GetSphereRadius(), 0.5f),
                         "Collider default sphere radius should be 0.5");
        passed &= Expect(collider.GetLayer() == 1ull, "Collider default layer should be bit 0");
        passed &= Expect(collider.GetCollidesWith() == ~0ull,
                         "Collider default collision mask should include all layers");
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
        passed &= Expect(object.GetComponent<ve::ColliderComponent>() == &first,
                         "Existing collider should remain attached");
        passed &= Expect(first.GetLayer() == (1ull << 3), "Duplicate add should not reset existing collider state");
        return passed;
    }

    bool TestBuildWorldSphereUsesTransformScaleAndCenter()
    {
        ve::Scene scene;
        ve::GameObject& object = scene.CreateGameObject("Sphere");
        ve::TransformComponent& transform = object.AddComponent<ve::TransformComponent>();
        transform.SetLocalPosition(ve::Vector3(10.0f, 20.0f, 30.0f));
        transform.SetLocalRotation(ve::Quaternion::FromAxisAngle(ve::Vector3::UnitZ(), ve::ToRadians(90.0f)));
        transform.SetLocalScale(ve::Vector3(2.0f, 3.0f, 4.0f));

        ve::ColliderComponent& collider = object.AddComponent<ve::ColliderComponent>();
        collider.SetCenter(ve::Vector3(1.0f, 2.0f, 3.0f));
        collider.SetSphereRadius(1.5f);
        scene.UpdateTransforms();

        const ve::Sphere sphere = collider.BuildWorldSphere(transform);

        bool passed = true;
        passed &= ExpectVectorNearlyEqual(sphere.center, ve::Vector3(4.0f, 22.0f, 42.0f),
                                          "World sphere should transform local center by TRS");
        passed &= ExpectNearlyEqual(sphere.radius, 6.0f,
                                    "World sphere radius should use the maximum non-uniform axis scale");
        return passed;
    }

    bool TestBuildWorldBoxUsesTransformScaleRotationAndCenter()
    {
        ve::Scene scene;
        ve::GameObject& object = scene.CreateGameObject("Box");
        ve::TransformComponent& transform = object.AddComponent<ve::TransformComponent>();
        transform.SetLocalPosition(ve::Vector3(5.0f, -1.0f, 2.0f));
        transform.SetLocalRotation(ve::Quaternion::FromAxisAngle(ve::Vector3::UnitZ(), ve::ToRadians(90.0f)));
        transform.SetLocalScale(ve::Vector3(2.0f, 3.0f, 4.0f));

        ve::ColliderComponent& collider = object.AddComponent<ve::ColliderComponent>();
        collider.SetCenter(ve::Vector3(1.0f, 2.0f, 0.5f));
        collider.SetBoxSize(ve::Vector3(2.0f, 4.0f, 6.0f));
        scene.UpdateTransforms();

        const ve::OrientedBox box = collider.BuildWorldBox(transform);
        const ve::Aabb bounds = ve::BuildAabb(box);

        bool passed = true;
        passed &= ExpectVectorNearlyEqual(box.center, ve::Vector3(-1.0f, 1.0f, 4.0f),
                                          "World box should transform local center by TRS");
        passed &= ExpectVectorNearlyEqual(box.halfExtents, ve::Vector3(2.0f, 6.0f, 12.0f),
                                          "World box half extents should include local size and scale");
        passed &= ExpectVectorNearlyEqual(box.axes[0], ve::Vector3(0.0f, 1.0f, 0.0f),
                                          "World box X axis should include rotation without scale");
        passed &= ExpectVectorNearlyEqual(box.axes[1], ve::Vector3(-1.0f, 0.0f, 0.0f),
                                          "World box Y axis should include rotation without scale");
        passed &= ExpectVectorNearlyEqual(box.axes[2], ve::Vector3(0.0f, 0.0f, 1.0f),
                                          "World box Z axis should include rotation without scale");
        passed &= ExpectOrthonormalAxes(box, "World box axes should be orthonormal");
        passed &= ExpectVectorNearlyEqual(bounds.minimum, ve::Vector3(-7.0f, -1.0f, -8.0f),
                                          "World box AABB should consume normalized axes and half extents");
        passed &= ExpectVectorNearlyEqual(bounds.maximum, ve::Vector3(5.0f, 3.0f, 16.0f),
                                          "World box AABB should consume normalized axes and half extents");
        return passed;
    }

    bool TestBuildWorldBoxKeepsAxesOrthonormalWithScaledParent()
    {
        ve::Scene scene;
        ve::GameObject& parent = scene.CreateGameObject("Parent");
        ve::TransformComponent& parentTransform = parent.AddComponent<ve::TransformComponent>();
        parentTransform.SetLocalScale(ve::Vector3(2.0f, 1.0f, 3.0f));

        ve::GameObject& object = scene.CreateGameObject("Child");
        object.SetParent(&parent);
        ve::TransformComponent& transform = object.AddComponent<ve::TransformComponent>();
        transform.SetLocalRotation(ve::Quaternion::FromAxisAngle(ve::Vector3::UnitZ(), ve::ToRadians(45.0f)));

        ve::ColliderComponent& collider = object.AddComponent<ve::ColliderComponent>();
        scene.UpdateTransforms();

        const ve::OrientedBox box = collider.BuildWorldBox(transform);

        bool passed = true;
        passed &= ExpectOrthonormalAxes(box, "World box axes should stay orthonormal with inherited non-uniform scale");
        return passed;
    }

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
        const ve::ColliderComponent* collider =
            object != nullptr ? object->GetComponent<ve::ColliderComponent>() : nullptr;
        passed &= Expect(collider != nullptr && collider->GetShape() == ve::ColliderShape::Box,
                         "First collider should be preserved");
        passed &= Expect(collider != nullptr && collider->GetLayer() == 8ull,
                         "Skipped duplicate should not overwrite first collider properties");
        return passed;
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
    passed &= TestColliderDefaultsAndSetters();
    passed &= TestOneColliderPerGameObject();
    passed &= TestBuildWorldSphereUsesTransformScaleAndCenter();
    passed &= TestBuildWorldBoxUsesTransformScaleRotationAndCenter();
    passed &= TestBuildWorldBoxKeepsAxesOrthonormalWithScaledParent();
    passed &= TestColliderSerializationRoundTrip();
    passed &= TestDuplicateColliderDeserializationSkipsSecondCollider();
    return passed ? 0 : 1;
}
