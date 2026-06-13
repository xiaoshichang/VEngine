#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Scene/Component.h"

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
        [[nodiscard]] LightType GetLightType() const noexcept;
        void SetLightType(LightType type) noexcept;

        [[nodiscard]] const Vector3& GetColor() const noexcept;
        void SetColor(const Vector3& color) noexcept;

        [[nodiscard]] Float32 GetIntensity() const noexcept;
        void SetIntensity(Float32 intensity) noexcept;

    private:
        LightType type_ = LightType::Directional;
        Vector3 color_ = Vector3::One();
        Float32 intensity_ = 1.0f;
    };
} // namespace ve
