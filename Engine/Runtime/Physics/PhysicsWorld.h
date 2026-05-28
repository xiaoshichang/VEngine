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
        [[nodiscard]] std::optional<RaycastHit>
        RaycastClosest(const Ray& ray, UInt64 queryMask = ~0ull, bool includeTriggers = false) const;
        [[nodiscard]] std::vector<RaycastHit>
        RaycastAll(const Ray& ray, UInt64 queryMask = ~0ull, bool includeTriggers = false) const;

    private:
        std::vector<ColliderProxy> colliderProxies_;
    };
} // namespace ve
