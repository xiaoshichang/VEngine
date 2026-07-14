#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Physics/PhysicsSystemBackend.h"
#include "Engine/Runtime/Physics/PhysicsTypes.h"

#include <memory>

namespace ve
{
    class Scene;
    struct PhysicsSystemSceneSyncState;

    class PhysicsSystem final : public NonMovable
    {
    public:
        PhysicsSystem();
        ~PhysicsSystem();

        [[nodiscard]] ErrorCode Initialize(const PhysicsSystemInitParam& initParam);
        void Shutdown() noexcept;
        [[nodiscard]] ErrorCode ResetSimulation();

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] PhysicsBackendType GetBackendType() const noexcept;

        [[nodiscard]] ErrorCode StepSimulation(const PhysicsStepDesc& desc);

        [[nodiscard]] Result<PhysicsBodyHandle> CreateBody(const PhysicsBodyDesc& desc);
        [[nodiscard]] ErrorCode DestroyBody(PhysicsBodyHandle body);

        [[nodiscard]] Result<PhysicsBodyTransform> GetBodyTransform(PhysicsBodyHandle body) const;
        [[nodiscard]] ErrorCode SetBodyTransform(PhysicsBodyHandle body, const PhysicsBodyTransform& transform);

        [[nodiscard]] Result<Vector3> GetBodyLinearVelocity(PhysicsBodyHandle body) const;
        [[nodiscard]] ErrorCode SetBodyLinearVelocity(PhysicsBodyHandle body, Vector3 velocity);
        [[nodiscard]] Result<Vector3> GetBodyAngularVelocity(PhysicsBodyHandle body) const;
        [[nodiscard]] ErrorCode SetBodyAngularVelocity(PhysicsBodyHandle body, Vector3 velocity);

        [[nodiscard]] ErrorCode SyncSceneBeforeStep(Scene& scene);
        [[nodiscard]] ErrorCode WriteBackSceneAfterStep(Scene& scene);
        void ClearSceneSyncState(Scene& scene) noexcept;

    private:
        [[nodiscard]] static std::unique_ptr<PhysicsSystemBackend> CreateBackend(PhysicsBackendType backendType);

        std::unique_ptr<PhysicsSystemBackend> backend_;
        std::unique_ptr<PhysicsSystemSceneSyncState> sceneSyncState_;
        PhysicsSystemInitParam initParam_;
        bool initialized_ = false;
    };
} // namespace ve
