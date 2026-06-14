#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Scene/Component.h"

#include <memory>

namespace ve
{
    /// Supported light kinds for the first-stage Scene model.
    enum class LightType
    {
        Directional,
        Point,
    };

    /// Light data consumed by future render-world extraction.
    class LightComponent final : public Component
    {
    public:
        LightComponent(Scene& scene, GameObject& owner);
        ~LightComponent() override;

        [[nodiscard]] LightType GetLightType() const noexcept;
        void SetLightType(LightType type) noexcept;

        [[nodiscard]] const Vector3& GetColor() const noexcept;
        void SetColor(const Vector3& color) noexcept;

        [[nodiscard]] const Vector3& GetDirection() const noexcept;
        void SetDirection(const Vector3& direction) noexcept;

        [[nodiscard]] Float32 GetIntensity() const noexcept;
        void SetIntensity(Float32 intensity) noexcept;

        [[nodiscard]] Float32 GetRange() const noexcept;
        void SetRange(Float32 range) noexcept;

        [[nodiscard]] Float32 GetInnerConeAngleRadians() const noexcept;
        void SetInnerConeAngleRadians(Float32 innerConeAngleRadians) noexcept;

        [[nodiscard]] Float32 GetOuterConeAngleRadians() const noexcept;
        void SetOuterConeAngleRadians(Float32 outerConeAngleRadians) noexcept;

        [[nodiscard]] bool CastShadows() const noexcept;
        void SetCastShadows(bool castShadows) noexcept;

        [[nodiscard]] std::shared_ptr<RTLight> GetRTLight() noexcept;
        [[nodiscard]] std::shared_ptr<const RTLight> GetRTLight() const noexcept;

        void SetEnabled(bool enabled) noexcept override;

    private:
        friend class GameObject;
        friend class Scene;

        [[nodiscard]] RTLightDesc BuildLightDesc() const;
        [[nodiscard]] bool IsLightTransformDirty() const noexcept;
        void MarkLightTransformDirty() noexcept;
        void ClearLightTransformDirty() noexcept;
        void UnregisterTransformChangedCallback() noexcept;
        void RegisterLightToRenderThread();
        void UnregisterLightFromRenderThread() noexcept;
        void SubmitLightUpdateToRenderThread();
        void SubmitLightTransformUpdateToRenderThread();

        LightType type_ = LightType::Directional;
        Vector3 color_ = Vector3::One();
        Vector3 direction_ = Vector3::UnitZ();
        Float32 intensity_ = 1.0f;
        Float32 range_ = 0.0f;
        Float32 innerConeAngleRadians_ = 0.0f;
        Float32 outerConeAngleRadians_ = 0.0f;
        bool castShadows_ = false;
        bool lightTransformDirty_ = true;
        bool renderThreadRegistered_ = false;
        UInt64 transformChangedCallbackId_ = 0;
        std::shared_ptr<RTLight> rtLight_;
    };
} // namespace ve
