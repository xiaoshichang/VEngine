#include "Engine/Runtime/Scene/CameraComponent.h"

#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

namespace ve
{
    namespace
    {
        [[nodiscard]] RTCameraProjectionMode ToRTCameraProjectionMode(CameraComponent::ProjectionMode mode) noexcept
        {
            switch (mode)
            {
            case CameraComponent::ProjectionMode::Perspective:
                return RTCameraProjectionMode::Perspective;
            case CameraComponent::ProjectionMode::Orthographic:
                return RTCameraProjectionMode::Orthographic;
            }

            return RTCameraProjectionMode::Perspective;
        }
    } // namespace

    CameraComponent::CameraComponent(Scene& scene, GameObject& owner)
        : Component(scene, owner)
        , rtCamera_(std::make_shared<RTCamera>(BuildCameraInitParam()))
    {
        TransformComponent* transform = owner.GetComponent<TransformComponent>();
        VE_ASSERT(transform != nullptr);
        transformChangedCallbackId_ = transform->AddTransformChangedCallback([this]() { MarkCameraDirty(); });
    }

    CameraComponent::~CameraComponent()
    {
        UnregisterTransformChangedCallback();
        UnregisterCameraFromRenderThread();
    }

    bool CameraComponent::IsPrimary() const noexcept
    {
        return primary_;
    }

    void CameraComponent::SetPrimary(bool primary) noexcept
    {
        primary_ = primary;
        MarkCameraDirty();
    }

    CameraComponent::ProjectionMode CameraComponent::GetProjectionMode() const noexcept
    {
        return projectionMode_;
    }

    void CameraComponent::SetProjectionMode(ProjectionMode mode) noexcept
    {
        projectionMode_ = mode;
        MarkCameraDirty();
    }

    Float32 CameraComponent::GetVerticalFieldOfViewRadians() const noexcept
    {
        return verticalFieldOfViewRadians_;
    }

    void CameraComponent::SetVerticalFieldOfViewRadians(Float32 fieldOfViewRadians) noexcept
    {
        verticalFieldOfViewRadians_ = fieldOfViewRadians;
        MarkCameraDirty();
    }

    Float32 CameraComponent::GetOrthographicSize() const noexcept
    {
        return orthographicSize_;
    }

    void CameraComponent::SetOrthographicSize(Float32 orthographicSize) noexcept
    {
        orthographicSize_ = orthographicSize;
        MarkCameraDirty();
    }

    Float32 CameraComponent::GetAspectRatio() const noexcept
    {
        return aspectRatio_;
    }

    void CameraComponent::SetAspectRatio(Float32 aspectRatio) noexcept
    {
        aspectRatio_ = aspectRatio;
        MarkCameraDirty();
    }

    Float32 CameraComponent::GetNearClipPlane() const noexcept
    {
        return nearClipPlane_;
    }

    void CameraComponent::SetNearClipPlane(Float32 nearClipPlane) noexcept
    {
        nearClipPlane_ = nearClipPlane;
        MarkCameraDirty();
    }

    Float32 CameraComponent::GetFarClipPlane() const noexcept
    {
        return farClipPlane_;
    }

    void CameraComponent::SetFarClipPlane(Float32 farClipPlane) noexcept
    {
        farClipPlane_ = farClipPlane;
        MarkCameraDirty();
    }

    const rhi::RhiColor& CameraComponent::GetClearColor() const noexcept
    {
        return clearColor_;
    }

    void CameraComponent::SetClearColor(const rhi::RhiColor& clearColor) noexcept
    {
        clearColor_ = clearColor;
        MarkCameraDirty();
    }

    std::shared_ptr<RTCamera> CameraComponent::GetRTCamera() noexcept
    {
        return rtCamera_;
    }

    std::shared_ptr<const RTCamera> CameraComponent::GetRTCamera() const noexcept
    {
        return rtCamera_;
    }

    RTCameraInitParam CameraComponent::BuildCameraInitParam() const
    {
        const GameObject* owner = GetOwner();
        const TransformComponent* transform = owner != nullptr ? owner->GetComponent<TransformComponent>() : nullptr;

        return RTCameraInitParam{
            primary_,
            ToRTCameraProjectionMode(projectionMode_),
            verticalFieldOfViewRadians_,
            orthographicSize_,
            aspectRatio_,
            nearClipPlane_,
            farClipPlane_,
            clearColor_,
            transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity(),
        };
    }

    RTCameraUpdateParam CameraComponent::BuildCameraUpdateParam() const
    {
        const GameObject* owner = GetOwner();
        const TransformComponent* transform = owner != nullptr ? owner->GetComponent<TransformComponent>() : nullptr;

        return RTCameraUpdateParam{
            RTCameraDirtyFlags::All,
            primary_,
            ToRTCameraProjectionMode(projectionMode_),
            verticalFieldOfViewRadians_,
            orthographicSize_,
            aspectRatio_,
            nearClipPlane_,
            farClipPlane_,
            clearColor_,
            transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity(),
        };
    }

    bool CameraComponent::IsCameraDirty() const noexcept
    {
        return cameraDirty_;
    }

    void CameraComponent::MarkCameraDirty() noexcept
    {
        cameraDirty_ = true;
    }

    void CameraComponent::ClearCameraDirty() noexcept
    {
        cameraDirty_ = false;
    }

    void CameraComponent::UnregisterTransformChangedCallback() noexcept
    {
        if (transformChangedCallbackId_ == 0)
        {
            return;
        }

        if (GameObject* owner = GetOwner(); owner != nullptr)
        {
            TransformComponent* transform = owner->GetComponent<TransformComponent>();
            VE_ASSERT(transform != nullptr);
            transform->RemoveTransformChangedCallback(transformChangedCallbackId_);
        }

        transformChangedCallbackId_ = 0;
    }

    void CameraComponent::RegisterCameraToRenderThread()
    {
        if (!IsEnabled() || renderThreadRegistered_)
        {
            return;
        }

        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->RegisterCamera(rtCamera_);
        scene->UpdateCamera(rtCamera_, BuildCameraUpdateParam());
        renderThreadRegistered_ = true;
        ClearCameraDirty();
    }

    void CameraComponent::UnregisterCameraFromRenderThread() noexcept
    {
        if (!renderThreadRegistered_)
        {
            return;
        }

        renderThreadRegistered_ = false;
        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->UnregisterCamera(rtCamera_);
    }

    void CameraComponent::SubmitCameraUpdateToRenderThread()
    {
        if (!IsCameraDirty() || !IsEnabled() || !renderThreadRegistered_)
        {
            return;
        }

        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->UpdateCamera(rtCamera_, BuildCameraUpdateParam());
        ClearCameraDirty();
    }

    void CameraComponent::SubmitCameraTransformUpdateToRenderThread()
    {
        SubmitCameraUpdateToRenderThread();
    }

    void CameraComponent::SetEnabled(bool enabled) noexcept
    {
        if (IsEnabled() == enabled)
        {
            return;
        }

        Component::SetEnabled(enabled);
        if (enabled)
        {
            RegisterCameraToRenderThread();
        }
        else
        {
            UnregisterCameraFromRenderThread();
        }
    }
} // namespace ve
