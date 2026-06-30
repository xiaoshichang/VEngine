#include "Engine/Runtime/Physics/PhysicsSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Physics/PhysicsSystemBackendJolt.h"

namespace ve
{
    PhysicsSystem::PhysicsSystem() = default;

    PhysicsSystem::~PhysicsSystem()
    {
        Shutdown();
    }

    ErrorCode PhysicsSystem::Initialize(const PhysicsSystemInitParam& initParam)
    {
        if (initialized_)
        {
            return ErrorCode::InvalidState;
        }

        backend_ = CreateBackend(initParam.backendType);
        if (backend_ == nullptr)
        {
            return ErrorCode::Unsupported;
        }

        const ErrorCode result = backend_->Initialize(initParam);
        if (result != ErrorCode::None)
        {
            backend_.reset();
            return result;
        }

        initialized_ = true;
        return ErrorCode::None;
    }

    void PhysicsSystem::Shutdown() noexcept
    {
        if (backend_ != nullptr)
        {
            backend_->Shutdown();
            backend_.reset();
        }

        initialized_ = false;
    }

    bool PhysicsSystem::IsInitialized() const noexcept
    {
        return initialized_ && backend_ != nullptr && backend_->IsInitialized();
    }

    PhysicsBackendType PhysicsSystem::GetBackendType() const noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::GetBackendType requires an initialized physics system.");
        return backend_->GetBackendType();
    }

    ErrorCode PhysicsSystem::StepSimulation(const PhysicsStepDesc& desc)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::StepSimulation requires an initialized physics system.");
        return backend_->StepSimulation(desc);
    }

    Result<PhysicsBodyHandle> PhysicsSystem::CreateBody(const PhysicsBodyDesc& desc)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::CreateBody requires an initialized physics system.");
        return backend_->CreateBody(desc);
    }

    ErrorCode PhysicsSystem::DestroyBody(PhysicsBodyHandle body)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::DestroyBody requires an initialized physics system.");
        return backend_->DestroyBody(body);
    }

    Result<PhysicsBodyTransform> PhysicsSystem::GetBodyTransform(PhysicsBodyHandle body) const
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::GetBodyTransform requires an initialized physics system.");
        return backend_->GetBodyTransform(body);
    }

    ErrorCode PhysicsSystem::SetBodyTransform(PhysicsBodyHandle body, const PhysicsBodyTransform& transform)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::SetBodyTransform requires an initialized physics system.");
        return backend_->SetBodyTransform(body, transform);
    }

    Result<Vector3> PhysicsSystem::GetBodyLinearVelocity(PhysicsBodyHandle body) const
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::GetBodyLinearVelocity requires an initialized physics system.");
        return backend_->GetBodyLinearVelocity(body);
    }

    ErrorCode PhysicsSystem::SetBodyLinearVelocity(PhysicsBodyHandle body, Vector3 velocity)
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "PhysicsSystem::SetBodyLinearVelocity requires an initialized physics system.");
        return backend_->SetBodyLinearVelocity(body, velocity);
    }

    std::unique_ptr<PhysicsSystemBackend> PhysicsSystem::CreateBackend(PhysicsBackendType backendType)
    {
        PhysicsBackendType resolvedBackend = backendType;
        if (resolvedBackend == PhysicsBackendType::Auto)
        {
            resolvedBackend = PhysicsBackendType::Jolt;
        }

        switch (resolvedBackend)
        {
        case PhysicsBackendType::Jolt:
            return std::make_unique<PhysicsSystemBackendJolt>();
        case PhysicsBackendType::Auto:
            return nullptr;
        }

        return nullptr;
    }
} // namespace ve
