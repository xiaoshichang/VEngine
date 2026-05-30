#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Physics/ColliderComponent.h"
#include "Engine/Runtime/Physics/PhysicsGeometry.h"
#include "Engine/Runtime/Physics/PhysicsSystem.h"
#include "Engine/Runtime/Physics/PhysicsWorld.h"
#include "Engine/Runtime/Physics/RigidBodyComponent.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/Serialization/SceneSerialization.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <iostream>
#include <string>
#include <vector>

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
        passed &= Expect(closest && closest->gameObjectId == nearObject.GetId(),
                         "Closest raycast should return nearest object");
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
        passed &= Expect(sphereHits[0].gameObjectId == sphereObject.GetId(),
                         "OverlapSphere should preserve sync order");
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

    bool TestRigidBodyDefaultsSettersAndDuplicateRule()
    {
        ve::Scene scene;
        ve::GameObject& object = scene.CreateGameObject("RigidBody");
        ve::RigidBodyComponent& rigidBody = object.AddComponent<ve::RigidBodyComponent>();

        bool passed = true;
        passed &= Expect(rigidBody.GetBodyType() == ve::RigidBodyType::Dynamic,
                         "RigidBody should default to dynamic");
        passed &= Expect(ve::NearlyEqual(rigidBody.GetMass(), 1.0f), "RigidBody mass should default to 1");
        passed &= Expect(rigidBody.UsesGravity(), "RigidBody should use gravity by default");
        passed &= Expect(rigidBody.GetInterpolationMode() == ve::PhysicsInterpolationMode::Interpolate,
                         "RigidBody should default to interpolation");

        rigidBody.SetBodyType(ve::RigidBodyType::Kinematic);
        rigidBody.SetMass(3.0f);
        rigidBody.SetUseGravity(false);
        rigidBody.SetGravityScale(0.25f);
        rigidBody.SetLinearVelocity(ve::Vector3(1.0f, 2.0f, 3.0f));
        rigidBody.SetAngularVelocity(ve::Vector3(4.0f, 5.0f, 6.0f));
        rigidBody.SetLinearDamping(0.5f);
        rigidBody.SetAngularDamping(0.75f);
        rigidBody.SetInterpolationMode(ve::PhysicsInterpolationMode::Extrapolate);
        rigidBody.AddForce(ve::Vector3(7.0f, 8.0f, 9.0f));
        rigidBody.AddTorque(ve::Vector3(10.0f, 11.0f, 12.0f));

        passed &= Expect(rigidBody.GetBodyType() == ve::RigidBodyType::Kinematic,
                         "RigidBody body type setter should persist");
        passed &= Expect(ve::NearlyEqual(rigidBody.GetMass(), 3.0f), "RigidBody mass setter should persist");
        passed &= Expect(!rigidBody.UsesGravity(), "RigidBody gravity toggle should persist");
        passed &= Expect(ve::NearlyEqual(rigidBody.GetGravityScale(), 0.25f),
                         "RigidBody gravity scale should persist");
        passed &= ExpectVectorNearlyEqual(rigidBody.GetLinearVelocity(), ve::Vector3(1.0f, 2.0f, 3.0f),
                                          "RigidBody linear velocity should persist");
        passed &= ExpectVectorNearlyEqual(rigidBody.GetAngularVelocity(), ve::Vector3(4.0f, 5.0f, 6.0f),
                                          "RigidBody angular velocity should persist");
        passed &= Expect(ve::NearlyEqual(rigidBody.GetLinearDamping(), 0.5f),
                         "RigidBody linear damping should persist");
        passed &= Expect(ve::NearlyEqual(rigidBody.GetAngularDamping(), 0.75f),
                         "RigidBody angular damping should persist");
        passed &= Expect(rigidBody.GetInterpolationMode() == ve::PhysicsInterpolationMode::Extrapolate,
                         "RigidBody interpolation mode should persist");
        passed &= ExpectVectorNearlyEqual(rigidBody.GetAccumulatedForce(), ve::Vector3(7.0f, 8.0f, 9.0f),
                                          "RigidBody accumulated force should persist until a step clears it");
        passed &= ExpectVectorNearlyEqual(rigidBody.GetAccumulatedTorque(), ve::Vector3(10.0f, 11.0f, 12.0f),
                                          "RigidBody accumulated torque should persist until a step clears it");

        ve::RigidBodyComponent& duplicate = object.AddComponent<ve::RigidBodyComponent>();
        passed &= Expect(&duplicate == &rigidBody, "Adding a second rigid body should return the existing component");
        passed &= Expect(object.GetComponents().size() == 1, "Object should still own one rigid body component");
        return passed;
    }

    bool TestRigidBodySerializationRoundTripAndDuplicateSkip()
    {
        ve::ReflectionRegistry registry;
        ve::RegisterSceneReflectionTypes(registry);

        ve::Scene source;
        ve::GameObject& object = source.CreateGameObject("SerializedRigidBody");
        object.AddComponent<ve::TransformComponent>();
        ve::RigidBodyComponent& rigidBody = object.AddComponent<ve::RigidBodyComponent>();
        rigidBody.SetBodyType(ve::RigidBodyType::Kinematic);
        rigidBody.SetMass(4.0f);
        rigidBody.SetUseGravity(false);
        rigidBody.SetGravityScale(2.0f);
        rigidBody.SetLinearVelocity(ve::Vector3(1.0f, 2.0f, 3.0f));
        rigidBody.SetAngularVelocity(ve::Vector3(4.0f, 5.0f, 6.0f));
        rigidBody.SetLinearDamping(0.25f);
        rigidBody.SetAngularDamping(0.5f);
        rigidBody.SetInterpolationMode(ve::PhysicsInterpolationMode::None);

        const std::string json = ve::SerializeSceneToJson(source, registry);
        ve::Scene loaded;
        bool passed = true;
        passed &= Expect(ve::DeserializeSceneFromJson(loaded, registry, json) == ve::ErrorCode::None,
                         "RigidBody scene should deserialize");
        ve::GameObject* loadedObject = loaded.FindGameObject(object.GetId());
        const ve::RigidBodyComponent* loadedRigidBody =
            loadedObject != nullptr ? loadedObject->GetComponent<ve::RigidBodyComponent>() : nullptr;
        passed &= Expect(loadedRigidBody != nullptr, "Loaded object should have a rigid body");
        if (loadedRigidBody != nullptr)
        {
            passed &= Expect(loadedRigidBody->GetBodyType() == ve::RigidBodyType::Kinematic,
                             "RigidBody body type should round-trip");
            passed &= Expect(ve::NearlyEqual(loadedRigidBody->GetMass(), 4.0f),
                             "RigidBody mass should round-trip");
            passed &= Expect(!loadedRigidBody->UsesGravity(), "RigidBody gravity toggle should round-trip");
            passed &= Expect(ve::NearlyEqual(loadedRigidBody->GetGravityScale(), 2.0f),
                             "RigidBody gravity scale should round-trip");
            passed &= ExpectVectorNearlyEqual(loadedRigidBody->GetLinearVelocity(), ve::Vector3(1.0f, 2.0f, 3.0f),
                                              "RigidBody linear velocity should round-trip");
            passed &= ExpectVectorNearlyEqual(loadedRigidBody->GetAngularVelocity(), ve::Vector3(4.0f, 5.0f, 6.0f),
                                              "RigidBody angular velocity should round-trip");
            passed &= Expect(ve::NearlyEqual(loadedRigidBody->GetLinearDamping(), 0.25f),
                             "RigidBody linear damping should round-trip");
            passed &= Expect(ve::NearlyEqual(loadedRigidBody->GetAngularDamping(), 0.5f),
                             "RigidBody angular damping should round-trip");
            passed &= Expect(loadedRigidBody->GetInterpolationMode() == ve::PhysicsInterpolationMode::None,
                             "RigidBody interpolation mode should round-trip");
        }

        const std::string duplicateJson = R"({
            "version": 1,
            "scene": {"name": "Scene"},
            "gameObjects": [
                {
                    "id": 200,
                    "name": "DuplicateRigidBody",
                    "active": true,
                    "parent": 0,
                    "components": [
                        {"type": "RigidBodyComponent", "properties": {"bodyType": "Static", "mass": 8.0}},
                        {"type": "RigidBodyComponent", "properties": {"bodyType": "Dynamic", "mass": 1.0}}
                    ]
                }
            ]
        })";
        ve::Scene duplicateScene;
        passed &= Expect(ve::DeserializeSceneFromJson(duplicateScene, registry, duplicateJson) == ve::ErrorCode::None,
                         "Duplicate rigid body scene should still deserialize");
        ve::GameObject* duplicateObject = duplicateScene.FindGameObject(200);
        const ve::RigidBodyComponent* duplicateRigidBody =
            duplicateObject != nullptr ? duplicateObject->GetComponent<ve::RigidBodyComponent>() : nullptr;
        passed &= Expect(duplicateObject != nullptr && duplicateObject->GetComponents().size() == 1,
                         "Duplicate rigid body should be skipped");
        passed &= Expect(duplicateRigidBody != nullptr &&
                             duplicateRigidBody->GetBodyType() == ve::RigidBodyType::Static,
                         "First rigid body should be preserved when duplicate is skipped");
        return passed;
    }

    bool TestPhysicsSystemIntegratesGravityForceAndTorque()
    {
        ve::Scene scene;
        ve::GameObject& object = scene.CreateGameObject("DynamicSphere");
        ve::TransformComponent& transform = object.AddComponent<ve::TransformComponent>();
        ve::ColliderComponent& collider = object.AddComponent<ve::ColliderComponent>();
        collider.SetShape(ve::ColliderShape::Sphere);
        ve::RigidBodyComponent& rigidBody = object.AddComponent<ve::RigidBodyComponent>();
        rigidBody.SetMass(2.0f);
        rigidBody.SetUseGravity(true);
        rigidBody.AddForce(ve::Vector3(4.0f, 0.0f, 0.0f));
        rigidBody.AddTorque(ve::Vector3(0.0f, 0.0f, 2.0f));

        scene.UpdateTransforms();
        ve::PhysicsSystem physics;
        physics.SyncFromScene(scene);
        const ve::PhysicsStepStats stats = physics.Step(0.5f);
        physics.WriteBackTransforms();
        scene.UpdateTransforms();

        bool passed = true;
        passed &= Expect(stats.dynamicBodyCount == 1, "Physics step should count one dynamic body");
        passed &= Expect(rigidBody.GetLinearVelocity().GetX() > 0.9f,
                         "Force should accelerate dynamic body by inverse mass");
        passed &= Expect(rigidBody.GetLinearVelocity().GetY() < -4.8f,
                         "Gravity should accelerate dynamic body downward");
        passed &= Expect(transform.GetLocalPosition().GetX() > 0.45f,
                         "Integrated linear velocity should move Transform on X");
        passed &= Expect(transform.GetLocalPosition().GetY() < -2.4f,
                         "Integrated gravity velocity should move Transform downward");
        passed &= Expect(rigidBody.GetAngularVelocity().GetZ() > 0.0f,
                         "Torque should produce angular velocity");
        passed &= ExpectVectorNearlyEqual(rigidBody.GetAccumulatedForce(), ve::Vector3::Zero(),
                                          "Physics step should clear accumulated force");
        passed &= ExpectVectorNearlyEqual(rigidBody.GetAccumulatedTorque(), ve::Vector3::Zero(),
                                          "Physics step should clear accumulated torque");
        passed &= Expect(physics.GetPoseHistoryCount() == 1, "Physics should retain pose history for the body");
        return passed;
    }

    bool TestPhysicsSystemResolvesDynamicAgainstStaticGround()
    {
        ve::Scene scene;
        ve::GameObject& ground = scene.CreateGameObject("Ground");
        ground.AddComponent<ve::TransformComponent>().SetLocalPosition(ve::Vector3(0.0f, -0.5f, 0.0f));
        ve::ColliderComponent& groundCollider = ground.AddComponent<ve::ColliderComponent>();
        groundCollider.SetBoxSize(ve::Vector3(8.0f, 1.0f, 8.0f));

        ve::GameObject& sphere = scene.CreateGameObject("Sphere");
        ve::TransformComponent& sphereTransform = sphere.AddComponent<ve::TransformComponent>();
        sphereTransform.SetLocalPosition(ve::Vector3(0.0f, 0.4f, 0.0f));
        ve::ColliderComponent& sphereCollider = sphere.AddComponent<ve::ColliderComponent>();
        sphereCollider.SetShape(ve::ColliderShape::Sphere);
        sphereCollider.SetSphereRadius(0.5f);
        ve::RigidBodyComponent& rigidBody = sphere.AddComponent<ve::RigidBodyComponent>();
        rigidBody.SetLinearVelocity(ve::Vector3(0.0f, -1.0f, 0.0f));
        rigidBody.SetUseGravity(false);

        scene.UpdateTransforms();
        ve::PhysicsSystem physics;
        physics.SyncFromScene(scene);
        const ve::PhysicsStepStats stats = physics.Step(0.1f);
        physics.WriteBackTransforms();
        scene.UpdateTransforms();

        bool passed = true;
        passed &= Expect(stats.contactCount >= 1, "Physics step should report a non-trigger contact");
        passed &= Expect(sphereTransform.GetLocalPosition().GetY() >= 0.45f,
                         "Collision response should move sphere out of static ground");
        passed &= Expect(rigidBody.GetLinearVelocity().GetY() >= -0.1f,
                         "Collision impulse should reduce downward velocity");
        return passed;
    }

    bool TestPhysicsPresentationPoseDoesNotMutateTransform()
    {
        ve::Scene scene;
        ve::GameObject& object = scene.CreateGameObject("Interpolated");
        ve::TransformComponent& transform = object.AddComponent<ve::TransformComponent>();
        transform.SetLocalPosition(ve::Vector3(10.0f, 0.0f, 0.0f));
        ve::ColliderComponent& collider = object.AddComponent<ve::ColliderComponent>();
        collider.SetShape(ve::ColliderShape::Sphere);
        ve::RigidBodyComponent& rigidBody = object.AddComponent<ve::RigidBodyComponent>();
        rigidBody.SetUseGravity(false);
        rigidBody.SetLinearVelocity(ve::Vector3(2.0f, 0.0f, 0.0f));
        rigidBody.SetInterpolationMode(ve::PhysicsInterpolationMode::Interpolate);

        scene.UpdateTransforms();
        ve::PhysicsSystem physics;
        physics.SyncFromScene(scene);
        physics.Step(1.0f);
        physics.WriteBackTransforms();
        scene.UpdateTransforms();

        const auto pose = physics.GetPresentationPose(object.GetId(), 0.5f);

        bool passed = true;
        passed &= Expect(pose.has_value(), "Presentation pose should be available for simulated body");
        if (pose)
        {
            passed &= ExpectVectorNearlyEqual(pose->position, ve::Vector3(11.0f, 0.0f, 0.0f),
                                              "Interpolated presentation pose should sit between fixed poses");
        }
        passed &= ExpectVectorNearlyEqual(transform.GetLocalPosition(), ve::Vector3(12.0f, 0.0f, 0.0f),
                                          "Presentation pose query should not mutate TransformComponent");
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
    passed &= TestPhysicsWorldSyncBuildsColliderProxies();
    passed &= TestPhysicsWorldSyncSkipsInactiveDisabledAndMissingTransform();
    passed &= TestPhysicsWorldRaycastClosestAndAll();
    passed &= TestPhysicsWorldRaycastFilteringAndSyncBoundary();
    passed &= TestPhysicsWorldOverlapSphereAndBox();
    passed &= TestPhysicsWorldOverlapFiltering();
    passed &= TestRigidBodyDefaultsSettersAndDuplicateRule();
    passed &= TestRigidBodySerializationRoundTripAndDuplicateSkip();
    passed &= TestPhysicsSystemIntegratesGravityForceAndTorque();
    passed &= TestPhysicsSystemResolvesDynamicAgainstStaticGround();
    passed &= TestPhysicsPresentationPoseDoesNotMutateTransform();
    return passed ? 0 : 1;
}
