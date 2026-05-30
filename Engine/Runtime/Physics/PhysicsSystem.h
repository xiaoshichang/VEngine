#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Physics/PhysicsWorld.h"
#include "Engine/Runtime/Physics/RigidBodyComponent.h"
#include "Engine/Runtime/Scene/SceneTypes.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace ve
{
    class ColliderComponent;
    class Scene;
    class TransformComponent;

    struct PhysicsPose
    {
        Vector3 position = Vector3::Zero();
        Quaternion rotation = Quaternion::Identity();
        Vector3 scale = Vector3::One();
    };

    struct PhysicsStepStats
    {
        UInt32 dynamicBodyCount = 0;
        UInt32 contactCount = 0;
        UInt32 triggerOverlapCount = 0;
    };

    class PhysicsSystem
    {
    public:
        void Clear();
        void SyncFromScene(Scene& scene);
        PhysicsStepStats Step(Float32 fixedDeltaSeconds);
        void WriteBackTransforms();
        void SyncQueriesFromScene(const Scene& scene);

        [[nodiscard]] const PhysicsWorld& GetQueryWorld() const noexcept;
        [[nodiscard]] PhysicsWorld& GetQueryWorld() noexcept;
        [[nodiscard]] const PhysicsStepStats& GetLastStepStats() const noexcept;

        [[nodiscard]] std::optional<PhysicsPose>
        GetPresentationPose(SceneObjectId gameObjectId, Float32 alpha) const noexcept;
        [[nodiscard]] SizeT GetPoseHistoryCount() const noexcept;

    private:
        friend struct PhysicsSystemInternalsAccess;

        struct BodyProxy
        {
            SceneObjectId gameObjectId = InvalidSceneObjectId;
            TransformComponent* transform = nullptr;
            ColliderComponent* collider = nullptr;
            RigidBodyComponent* rigidBody = nullptr;
            RigidBodyType bodyType = RigidBodyType::Static;
            PhysicsInterpolationMode interpolationMode = PhysicsInterpolationMode::None;
            Float32 inverseMass = 0.0f;
            Vector3 inverseInertia = Vector3::Zero();
            PhysicsPose pose;
            ColliderShape shape = ColliderShape::Box;
            Sphere sphere;
            OrientedBox box;
        };

        struct PoseHistory
        {
            PhysicsPose previous;
            PhysicsPose current;
            Vector3 linearVelocity = Vector3::Zero();
            Vector3 angularVelocity = Vector3::Zero();
            PhysicsInterpolationMode interpolationMode = PhysicsInterpolationMode::None;
        };

        std::vector<BodyProxy> bodyProxies_;
        std::unordered_map<SceneObjectId, PoseHistory> poseHistory_;
        PhysicsWorld queryWorld_;
        PhysicsStepStats lastStepStats_;
    };
} // namespace ve
