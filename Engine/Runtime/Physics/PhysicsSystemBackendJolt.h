#pragma once

#include "Engine/Runtime/Physics/PhysicsSystemBackend.h"

#include <memory>

namespace ve
{
    struct PhysicsSystemBackendJoltImpl;

    class PhysicsSystemBackendJolt final : public PhysicsSystemBackend
    {
    public:
        PhysicsSystemBackendJolt();
        ~PhysicsSystemBackendJolt() override;

        [[nodiscard]] ErrorCode Initialize(const PhysicsSystemInitParam& initParam) override;
        void Shutdown() noexcept override;
        [[nodiscard]] bool IsInitialized() const noexcept override;
        [[nodiscard]] PhysicsBackendType GetBackendType() const noexcept override;

        [[nodiscard]] ErrorCode StepSimulation(const PhysicsStepDesc& desc) override;

        [[nodiscard]] Result<PhysicsBodyHandle> CreateBody(const PhysicsBodyDesc& desc) override;
        [[nodiscard]] ErrorCode DestroyBody(PhysicsBodyHandle body) override;

        [[nodiscard]] Result<PhysicsBodyTransform> GetBodyTransform(PhysicsBodyHandle body) const override;
        [[nodiscard]] ErrorCode SetBodyTransform(PhysicsBodyHandle body, const PhysicsBodyTransform& transform) override;

        [[nodiscard]] Result<Vector3> GetBodyLinearVelocity(PhysicsBodyHandle body) const override;
        [[nodiscard]] ErrorCode SetBodyLinearVelocity(PhysicsBodyHandle body, Vector3 velocity) override;

    private:
        std::unique_ptr<PhysicsSystemBackendJoltImpl> impl_;
    };
} // namespace ve
