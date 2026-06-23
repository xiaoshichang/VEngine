#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Editor/Core/EditorBuiltinResources.h"
#include "Editor/RenderPass/EditorGizmoRenderPass.h"

#include <memory>

namespace ve
{
    class CameraComponent;
    class GameObject;
    class LightComponent;
    class Scene;
}

namespace ve::editor
{
    struct GizmoComponentVisibility
    {
        bool camera = true;
        bool light = true;
    };

    struct GizmoBuildDesc
    {
        const Scene* scene = nullptr;
        const GameObject* selectedGameObject = nullptr;
        Matrix44 sceneViewCameraLocalToWorld = Matrix44::Identity();
    };

    class Gizmos
    {
    public:
        [[nodiscard]] GizmoComponentVisibility& GetComponentVisibility() noexcept;
        [[nodiscard]] const GizmoComponentVisibility& GetComponentVisibility() const noexcept;
        [[nodiscard]] bool HasAnyVisibleComponent() const noexcept;
        [[nodiscard]] std::shared_ptr<EditorGizmoDrawList> BuildDrawList(const GizmoBuildDesc& desc) const;

    private:
        void CollectGameObjectGizmos(const GameObject& gameObject, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const;
        void AddIcon(const Matrix44& world, const GizmoBuildDesc& desc, BuiltinGizmoIcon icon, const Vector3& color, EditorGizmoDrawList& drawList) const;
        void AddCameraIcon(const CameraComponent& camera, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const;
        void AddCameraFrustum(const CameraComponent& camera, EditorGizmoDrawList& drawList) const;
        void AddLightIcon(const LightComponent& light, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const;
        void AddDirectionalLightDirection(const LightComponent& light, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const;

        GizmoComponentVisibility componentVisibility_;
    };
} // namespace ve::editor
