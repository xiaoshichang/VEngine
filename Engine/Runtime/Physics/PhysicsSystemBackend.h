#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Physics/PhysicsTypes.h"

namespace ve
{
    class PhysicsSystemBackend : public NonMovable
    {
    public:
        virtual ~PhysicsSystemBackend() = default;

        [[nodiscard]] virtual ErrorCode Initialize(const PhysicsSystemInitParam& initParam) = 0;
        virtual void Shutdown() noexcept = 0;
        [[nodiscard]] virtual bool IsInitialized() const noexcept = 0;
        [[nodiscard]] virtual PhysicsBackendType GetBackendType() const noexcept = 0;

        [[nodiscard]] virtual ErrorCode StepSimulation(const PhysicsStepDesc& desc) = 0;

        [[nodiscard]] virtual Result<PhysicsBodyHandle> CreateBody(const PhysicsBodyDesc& desc) = 0;
        [[nodiscard]] virtual ErrorCode DestroyBody(PhysicsBodyHandle body) = 0;

        [[nodiscard]] virtual Result<PhysicsBodyTransform> GetBodyTransform(PhysicsBodyHandle body) const = 0;
        [[nodiscard]] virtual ErrorCode SetBodyTransform(PhysicsBodyHandle body, const PhysicsBodyTransform& transform) = 0;

        [[nodiscard]] virtual Result<Vector3> GetBodyLinearVelocity(PhysicsBodyHandle body) const = 0;
        [[nodiscard]] virtual ErrorCode SetBodyLinearVelocity(PhysicsBodyHandle body, Vector3 velocity) = 0;
        [[nodiscard]] virtual Result<Vector3> GetBodyAngularVelocity(PhysicsBodyHandle body) const = 0;
        [[nodiscard]] virtual ErrorCode SetBodyAngularVelocity(PhysicsBodyHandle body, Vector3 velocity) = 0;
    };
} // namespace ve
