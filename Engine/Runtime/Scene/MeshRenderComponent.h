#pragma once

#include "Engine/Runtime/Scene/Component.h"

#include <string>

namespace ve
{
    /// Renderable static mesh attachment used by the first-stage forward renderer.
    class MeshRenderComponent final : public Component
    {
    public:
        [[nodiscard]] const std::string& GetMeshAssetPath() const noexcept;
        void SetMeshAssetPath(std::string meshAssetPath);

        [[nodiscard]] const std::string& GetMaterialAssetPath() const noexcept;
        void SetMaterialAssetPath(std::string materialAssetPath);

        [[nodiscard]] bool IsVisible() const noexcept;
        void SetVisible(bool visible) noexcept;

    private:
        std::string meshAssetPath_;
        std::string materialAssetPath_;
        bool visible_ = true;
    };
} // namespace ve
