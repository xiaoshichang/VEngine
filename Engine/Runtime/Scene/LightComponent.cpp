#include "Engine/Runtime/Scene/LightComponent.h"

#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

namespace ve
{
    namespace
    {
        [[nodiscard]] RTLightType ToRTLightType(LightType type) noexcept
        {
            switch (type)
            {
            case LightType::Directional:
                return RTLightType::Directional;
            case LightType::Point:
                return RTLightType::Point;
            }

            return RTLightType::Directional;
        }

        [[nodiscard]] Vector3 GetDirectionFromTransform(const TransformComponent* transform) noexcept
        {
            return transform != nullptr ? transform->GetWorldMatrix().TransformDirection(Vector3::UnitZ()).Normalized()
                                        : Vector3::UnitZ();
        }
    } // namespace

    LightComponent::LightComponent(Scene& scene, GameObject& owner)
        : Component(scene, owner)
        , rtLight_(std::make_shared<RTLight>(BuildLightDesc()))
    {
        TransformComponent* transform = owner.GetComponent<TransformComponent>();
        VE_ASSERT(transform != nullptr);
        transformChangedCallbackId_ = transform->AddTransformChangedCallback([this]() { MarkLightTransformDirty(); });
    }

    LightComponent::~LightComponent()
    {
        UnregisterTransformChangedCallback();
        UnregisterLightFromRenderThread();
    }

    LightType LightComponent::GetLightType() const noexcept
    {
        return type_;
    }

    void LightComponent::SetLightType(LightType type) noexcept
    {
        type_ = type;
        SubmitLightUpdateToRenderThread();
    }

    const Vector3& LightComponent::GetColor() const noexcept
    {
        return color_;
    }

    void LightComponent::SetColor(const Vector3& color) noexcept
    {
        color_ = color;
        SubmitLightUpdateToRenderThread();
    }

    Float32 LightComponent::GetIntensity() const noexcept
    {
        return intensity_;
    }

    void LightComponent::SetIntensity(Float32 intensity) noexcept
    {
        intensity_ = intensity;
        SubmitLightUpdateToRenderThread();
    }

    Float32 LightComponent::GetRange() const noexcept
    {
        return range_;
    }

    void LightComponent::SetRange(Float32 range) noexcept
    {
        range_ = range;
        SubmitLightUpdateToRenderThread();
    }

    Float32 LightComponent::GetInnerConeAngleRadians() const noexcept
    {
        return innerConeAngleRadians_;
    }

    void LightComponent::SetInnerConeAngleRadians(Float32 innerConeAngleRadians) noexcept
    {
        innerConeAngleRadians_ = innerConeAngleRadians;
        SubmitLightUpdateToRenderThread();
    }

    Float32 LightComponent::GetOuterConeAngleRadians() const noexcept
    {
        return outerConeAngleRadians_;
    }

    void LightComponent::SetOuterConeAngleRadians(Float32 outerConeAngleRadians) noexcept
    {
        outerConeAngleRadians_ = outerConeAngleRadians;
        SubmitLightUpdateToRenderThread();
    }

    bool LightComponent::CastShadows() const noexcept
    {
        return castShadows_;
    }

    void LightComponent::SetCastShadows(bool castShadows) noexcept
    {
        castShadows_ = castShadows;
        SubmitLightUpdateToRenderThread();
    }

    std::shared_ptr<RTLight> LightComponent::GetRTLight() noexcept
    {
        return rtLight_;
    }

    std::shared_ptr<const RTLight> LightComponent::GetRTLight() const noexcept
    {
        return rtLight_;
    }

    RTLightDesc LightComponent::BuildLightDesc() const
    {
        const GameObject* owner = GetOwner();
        const TransformComponent* transform = owner != nullptr ? owner->GetComponent<TransformComponent>() : nullptr;

        return RTLightDesc{
            ToRTLightType(type_),
            color_,
            GetDirectionFromTransform(transform),
            intensity_,
            range_,
            innerConeAngleRadians_,
            outerConeAngleRadians_,
            castShadows_,
            transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity(),
        };
    }

    bool LightComponent::IsLightTransformDirty() const noexcept
    {
        return lightTransformDirty_;
    }

    void LightComponent::MarkLightTransformDirty() noexcept
    {
        lightTransformDirty_ = true;
    }

    void LightComponent::ClearLightTransformDirty() noexcept
    {
        lightTransformDirty_ = false;
    }

    void LightComponent::UnregisterTransformChangedCallback() noexcept
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

    void LightComponent::RegisterLightToRenderThread()
    {
        if (!IsEnabled() || renderThreadRegistered_)
        {
            return;
        }

        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->RegisterLight(rtLight_);
        scene->UpdateLight(rtLight_, BuildLightDesc());
        renderThreadRegistered_ = true;
        ClearLightTransformDirty();
    }

    void LightComponent::UnregisterLightFromRenderThread() noexcept
    {
        if (!renderThreadRegistered_)
        {
            return;
        }

        renderThreadRegistered_ = false;
        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->UnregisterLight(rtLight_);
    }

    void LightComponent::SubmitLightUpdateToRenderThread()
    {
        if (!IsEnabled() || !renderThreadRegistered_)
        {
            return;
        }

        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->UpdateLight(rtLight_, BuildLightDesc());
        ClearLightTransformDirty();
    }

    void LightComponent::SubmitLightTransformUpdateToRenderThread()
    {
        SubmitLightUpdateToRenderThread();
        ClearLightTransformDirty();
    }

    void LightComponent::SetEnabled(bool enabled) noexcept
    {
        if (IsEnabled() == enabled)
        {
            return;
        }

        Component::SetEnabled(enabled);
        if (enabled)
        {
            RegisterLightToRenderThread();
        }
        else
        {
            UnregisterLightFromRenderThread();
        }
    }
} // namespace ve
