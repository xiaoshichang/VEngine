#include "Engine/Runtime/Scene/LightComponent.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <exception>
#include <limits>

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
            return transform != nullptr ? transform->GetWorldMatrix().TransformDirection(Vector3::UnitZ()).Normalized() : Vector3::UnitZ();
        }

        void IncrementShadowRevision(UInt64& shadowRevision) noexcept
        {
            if (shadowRevision == std::numeric_limits<UInt64>::max())
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Light shadow revision exhausted.");
                std::terminate();
            }
            ++shadowRevision;
        }
    } // namespace

    LightComponent::LightComponent(Scene& scene, GameObject& owner)
        : Component(scene, owner)
        , rtLight_(std::make_shared<RTLight>(BuildLightInitParam()))
    {
        TransformComponent* transform = owner.GetComponent<TransformComponent>();
        VE_ASSERT(transform != nullptr);
        transformChangedCallbackId_ = transform->AddTransformChangedCallback([this]() { MarkLightShadowDirty(); });
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
        MarkLightDirty();
    }

    const Vector3& LightComponent::GetColor() const noexcept
    {
        return color_;
    }

    void LightComponent::SetColor(const Vector3& color) noexcept
    {
        color_ = color;
        MarkLightDirty();
    }

    Float32 LightComponent::GetIntensity() const noexcept
    {
        return intensity_;
    }

    void LightComponent::SetIntensity(Float32 intensity) noexcept
    {
        intensity_ = intensity;
        MarkLightDirty();
    }

    Float32 LightComponent::GetRange() const noexcept
    {
        return range_;
    }

    void LightComponent::SetRange(Float32 range) noexcept
    {
        range_ = range;
        MarkLightDirty();
    }

    Float32 LightComponent::GetInnerConeAngleRadians() const noexcept
    {
        return innerConeAngleRadians_;
    }

    void LightComponent::SetInnerConeAngleRadians(Float32 innerConeAngleRadians) noexcept
    {
        innerConeAngleRadians_ = innerConeAngleRadians;
        MarkLightDirty();
    }

    Float32 LightComponent::GetOuterConeAngleRadians() const noexcept
    {
        return outerConeAngleRadians_;
    }

    void LightComponent::SetOuterConeAngleRadians(Float32 outerConeAngleRadians) noexcept
    {
        outerConeAngleRadians_ = outerConeAngleRadians;
        MarkLightDirty();
    }

    bool LightComponent::CastShadows() const noexcept
    {
        return castShadows_;
    }

    void LightComponent::SetCastShadows(bool castShadows) noexcept
    {
        if (castShadows_ == castShadows)
        {
            return;
        }
        castShadows_ = castShadows;
        MarkLightShadowDirty();
    }

    Float32 LightComponent::GetShadowDistance() const noexcept
    {
        return shadowDistance_;
    }

    void LightComponent::SetShadowDistance(Float32 shadowDistance) noexcept
    {
        if (shadowDistance_ == shadowDistance)
        {
            return;
        }
        shadowDistance_ = shadowDistance;
        MarkLightShadowDirty();
    }

    Float32 LightComponent::GetDepthBias() const noexcept
    {
        return depthBias_;
    }

    void LightComponent::SetDepthBias(Float32 depthBias) noexcept
    {
        if (depthBias_ == depthBias)
        {
            return;
        }
        depthBias_ = depthBias;
        MarkLightShadowDirty();
    }

    Float32 LightComponent::GetNormalBias() const noexcept
    {
        return normalBias_;
    }

    void LightComponent::SetNormalBias(Float32 normalBias) noexcept
    {
        if (normalBias_ == normalBias)
        {
            return;
        }
        normalBias_ = normalBias;
        MarkLightShadowDirty();
    }

    UInt64 LightComponent::GetShadowRevision() const noexcept
    {
        return shadowRevision_;
    }

    std::shared_ptr<RTLight> LightComponent::GetRTLight() noexcept
    {
        return rtLight_;
    }

    std::shared_ptr<const RTLight> LightComponent::GetRTLight() const noexcept
    {
        return rtLight_;
    }

    RTLightInitParam LightComponent::BuildLightInitParam() const
    {
        const GameObject* owner = GetOwner();
        const TransformComponent* transform = owner != nullptr ? owner->GetComponent<TransformComponent>() : nullptr;

        return RTLightInitParam{
            ToRTLightType(type_),
            color_,
            GetDirectionFromTransform(transform),
            intensity_,
            range_,
            innerConeAngleRadians_,
            outerConeAngleRadians_,
            castShadows_,
            shadowDistance_,
            depthBias_,
            normalBias_,
            shadowRevision_,
            transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity(),
        };
    }

    RTLightUpdateParam LightComponent::BuildLightUpdateParam() const
    {
        const GameObject* owner = GetOwner();
        const TransformComponent* transform = owner != nullptr ? owner->GetComponent<TransformComponent>() : nullptr;

        return RTLightUpdateParam{
            RTLightDirtyFlags::All,
            ToRTLightType(type_),
            color_,
            GetDirectionFromTransform(transform),
            intensity_,
            range_,
            innerConeAngleRadians_,
            outerConeAngleRadians_,
            castShadows_,
            shadowDistance_,
            depthBias_,
            normalBias_,
            shadowRevision_,
            transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity(),
        };
    }

    bool LightComponent::IsLightDirty() const noexcept
    {
        return lightDirty_;
    }

    void LightComponent::MarkLightDirty() noexcept
    {
        lightDirty_ = true;
    }

    void LightComponent::MarkLightShadowDirty() noexcept
    {
        IncrementShadowRevision(shadowRevision_);
        MarkLightDirty();
    }

    void LightComponent::ClearLightDirty() noexcept
    {
        lightDirty_ = false;
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
        scene->UpdateLight(rtLight_, BuildLightUpdateParam());
        renderThreadRegistered_ = true;
        ClearLightDirty();
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
        if (!IsLightDirty() || !IsEnabled() || !renderThreadRegistered_)
        {
            return;
        }

        Scene* scene = GetScene();
        VE_ASSERT(scene != nullptr);
        scene->UpdateLight(rtLight_, BuildLightUpdateParam());
        ClearLightDirty();
    }

    void LightComponent::SubmitLightTransformUpdateToRenderThread()
    {
        SubmitLightUpdateToRenderThread();
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
