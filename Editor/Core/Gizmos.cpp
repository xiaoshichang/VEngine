#include "Editor/Core/Gizmos.h"

#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/LightComponent.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <algorithm>
#include <cmath>

namespace ve::editor
{
    namespace
    {
        constexpr Float32 IconWorldSize = 0.58f;
        constexpr Float32 DirectionLightLineLength = 1.35f;
        constexpr Float32 DirectionLightArrowLength = 0.24f;
        constexpr Float32 DirectionLightArrowWidth = 0.16f;
        const Vector3 CameraGizmoColor(0.95f, 0.78f, 0.22f);
        const Vector3 LightGizmoColor(1.0f, 0.92f, 0.38f);
        const Vector3 CameraFrustumColor(0.35f, 0.68f, 1.0f);
        const Vector3 DirectionalLightColor(1.0f, 0.86f, 0.24f);

        [[nodiscard]] Vector3 GetTranslation(const Matrix44& matrix) noexcept
        {
            return Vector3(matrix.Get(0, 3), matrix.Get(1, 3), matrix.Get(2, 3));
        }

        [[nodiscard]] Vector3 GetCameraRight(const Matrix44& localToWorld) noexcept
        {
            return localToWorld.TransformDirection(Vector3::UnitX()).Normalized();
        }

        [[nodiscard]] Vector3 GetCameraUp(const Matrix44& localToWorld) noexcept
        {
            return localToWorld.TransformDirection(Vector3::UnitY()).Normalized();
        }

        [[nodiscard]] Vector3 GetCameraForward(const Matrix44& localToWorld) noexcept
        {
            return localToWorld.TransformDirection(Vector3::UnitZ()).Normalized();
        }

        void AddLineVertex(EditorGizmoDrawList& drawList, const Vector3& position, const Vector3& color)
        {
            EditorGizmoVertex vertex = {};
            vertex.position[0] = position.GetX();
            vertex.position[1] = position.GetY();
            vertex.position[2] = position.GetZ();
            vertex.color[0] = color.GetX();
            vertex.color[1] = color.GetY();
            vertex.color[2] = color.GetZ();
            drawList.lines.push_back(vertex);
        }

        void AddLine(EditorGizmoDrawList& drawList, const Vector3& a, const Vector3& b, const Vector3& color)
        {
            if ((b - a).LengthSquared() <= Math::DefaultEpsilon)
            {
                return;
            }

            AddLineVertex(drawList, a, color);
            AddLineVertex(drawList, b, color);
        }

        void AddIconVertex(EditorGizmoDrawList& drawList, const Vector3& position, Float32 u, Float32 v, const Vector3& color)
        {
            EditorGizmoIconVertex vertex = {};
            vertex.position[0] = position.GetX();
            vertex.position[1] = position.GetY();
            vertex.position[2] = position.GetZ();
            vertex.uv[0] = u;
            vertex.uv[1] = v;
            vertex.uv[2] = 0.0f;
            vertex.color[0] = color.GetX();
            vertex.color[1] = color.GetY();
            vertex.color[2] = color.GetZ();
            drawList.icons.push_back(vertex);
        }

        void AddIconQuad(EditorGizmoDrawList& drawList,
                         const Vector3& topLeft,
                         const Vector3& topRight,
                         const Vector3& bottomRight,
                         const Vector3& bottomLeft,
                         const BuiltinGizmoIconUvRect& uv,
                         const Vector3& color)
        {
            AddIconVertex(drawList, topLeft, uv.minU, uv.minV, color);
            AddIconVertex(drawList, bottomRight, uv.maxU, uv.maxV, color);
            AddIconVertex(drawList, topRight, uv.maxU, uv.minV, color);
            AddIconVertex(drawList, topLeft, uv.minU, uv.minV, color);
            AddIconVertex(drawList, bottomLeft, uv.minU, uv.maxV, color);
            AddIconVertex(drawList, bottomRight, uv.maxU, uv.maxV, color);
        }

        [[nodiscard]] bool IsSelectedCamera(const GameObject* selectedGameObject, const CameraComponent& camera) noexcept
        {
            return selectedGameObject != nullptr && selectedGameObject == camera.GetOwner();
        }

        [[nodiscard]] bool IsSelectedLight(const GameObject* selectedGameObject, const LightComponent& light) noexcept
        {
            return selectedGameObject != nullptr && selectedGameObject == light.GetOwner();
        }
    } // namespace

    GizmoComponentVisibility& Gizmos::GetComponentVisibility() noexcept
    {
        return componentVisibility_;
    }

    const GizmoComponentVisibility& Gizmos::GetComponentVisibility() const noexcept
    {
        return componentVisibility_;
    }

    bool Gizmos::HasAnyVisibleComponent() const noexcept
    {
        return componentVisibility_.camera || componentVisibility_.light;
    }

    std::shared_ptr<EditorGizmoDrawList> Gizmos::BuildDrawList(const GizmoBuildDesc& desc) const
    {
        if (desc.scene == nullptr || !HasAnyVisibleComponent())
        {
            return nullptr;
        }

        auto drawList = std::make_shared<EditorGizmoDrawList>();
        drawList->lines.reserve(256);
        drawList->icons.reserve(256);
        for (SizeT rootIndex = 0; rootIndex < desc.scene->GetRootGameObjectCount(); ++rootIndex)
        {
            const GameObject* root = desc.scene->GetRootGameObject(rootIndex);
            if (root != nullptr)
            {
                CollectGameObjectGizmos(*root, desc, *drawList);
            }
        }

        return drawList->lines.empty() && drawList->icons.empty() ? nullptr : drawList;
    }

    void Gizmos::CollectGameObjectGizmos(const GameObject& gameObject, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const
    {
        if (componentVisibility_.camera)
        {
            if (const CameraComponent* camera = gameObject.GetComponent<CameraComponent>(); camera != nullptr && camera->IsEnabled())
            {
                if (IsSelectedCamera(desc.selectedGameObject, *camera))
                {
                    AddCameraFrustum(*camera, drawList);
                }
                else
                {
                    AddCameraIcon(*camera, desc, drawList);
                }
            }
        }

        if (componentVisibility_.light)
        {
            if (const LightComponent* light = gameObject.GetComponent<LightComponent>(); light != nullptr && light->IsEnabled())
            {
                if (IsSelectedLight(desc.selectedGameObject, *light))
                {
                    if (light->GetLightType() == LightType::Directional)
                    {
                        AddDirectionalLightDirection(*light, desc, drawList);
                    }
                }
                else
                {
                    AddLightIcon(*light, desc, drawList);
                }
            }
        }

        const TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
        if (transform == nullptr)
        {
            return;
        }

        for (SizeT childIndex = 0; childIndex < transform->GetChildCount(); ++childIndex)
        {
            const GameObject* child = transform->GetChildGameObject(childIndex);
            if (child != nullptr)
            {
                CollectGameObjectGizmos(*child, desc, drawList);
            }
        }
    }

    void Gizmos::AddIcon(const Matrix44& world, const GizmoBuildDesc& desc, BuiltinGizmoIcon icon, const Vector3& color, EditorGizmoDrawList& drawList) const
    {
        const Vector3 center = GetTranslation(world);
        const Vector3 right = GetCameraRight(desc.sceneViewCameraLocalToWorld) * (IconWorldSize * 0.5f);
        const Vector3 up = GetCameraUp(desc.sceneViewCameraLocalToWorld) * (IconWorldSize * 0.5f);
        const Vector3 topLeft = center - right + up;
        const Vector3 topRight = center + right + up;
        const Vector3 bottomRight = center + right - up;
        const Vector3 bottomLeft = center - right - up;

        AddIconQuad(drawList, topLeft, topRight, bottomRight, bottomLeft, GetBuiltinGizmoIconUvRect(icon), color);
    }

    void Gizmos::AddCameraIcon(const CameraComponent& camera, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const
    {
        const TransformComponent* transform = camera.GetOwner() != nullptr ? camera.GetOwner()->GetComponent<TransformComponent>() : nullptr;
        const Matrix44 world = transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity();
        AddIcon(world, desc, BuiltinGizmoIcon::Camera, CameraGizmoColor, drawList);
    }

    void Gizmos::AddCameraFrustum(const CameraComponent& camera, EditorGizmoDrawList& drawList) const
    {
        const TransformComponent* transform = camera.GetOwner() != nullptr ? camera.GetOwner()->GetComponent<TransformComponent>() : nullptr;
        const Matrix44 world = transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity();
        const Vector3 origin = GetTranslation(world);
        const Vector3 right = GetCameraRight(world);
        const Vector3 up = GetCameraUp(world);
        const Vector3 forward = GetCameraForward(world);
        const Float32 nearClip = std::max(camera.GetNearClipPlane(), 0.001f);
        const Float32 farClip = std::max(camera.GetFarClipPlane(), nearClip + 0.001f);
        const Float32 aspectRatio = std::max(camera.GetAspectRatio(), 0.001f);

        Float32 nearHalfHeight = 0.0f;
        Float32 farHalfHeight = 0.0f;
        if (camera.GetProjectionMode() == CameraComponent::ProjectionMode::Perspective)
        {
            const Float32 halfFieldOfViewTangent = std::tan(std::max(camera.GetVerticalFieldOfViewRadians(), 0.001f) * 0.5f);
            nearHalfHeight = halfFieldOfViewTangent * nearClip;
            farHalfHeight = halfFieldOfViewTangent * farClip;
        }
        else
        {
            nearHalfHeight = std::max(camera.GetOrthographicSize(), 0.001f) * 0.5f;
            farHalfHeight = nearHalfHeight;
        }

        const Float32 nearHalfWidth = nearHalfHeight * aspectRatio;
        const Float32 farHalfWidth = farHalfHeight * aspectRatio;
        const Vector3 nearCenter = origin + (forward * nearClip);
        const Vector3 farCenter = origin + (forward * farClip);
        const Vector3 nearTopLeft = nearCenter + (up * nearHalfHeight) - (right * nearHalfWidth);
        const Vector3 nearTopRight = nearCenter + (up * nearHalfHeight) + (right * nearHalfWidth);
        const Vector3 nearBottomRight = nearCenter - (up * nearHalfHeight) + (right * nearHalfWidth);
        const Vector3 nearBottomLeft = nearCenter - (up * nearHalfHeight) - (right * nearHalfWidth);
        const Vector3 farTopLeft = farCenter + (up * farHalfHeight) - (right * farHalfWidth);
        const Vector3 farTopRight = farCenter + (up * farHalfHeight) + (right * farHalfWidth);
        const Vector3 farBottomRight = farCenter - (up * farHalfHeight) + (right * farHalfWidth);
        const Vector3 farBottomLeft = farCenter - (up * farHalfHeight) - (right * farHalfWidth);

        AddLine(drawList, nearTopLeft, nearTopRight, CameraFrustumColor);
        AddLine(drawList, nearTopRight, nearBottomRight, CameraFrustumColor);
        AddLine(drawList, nearBottomRight, nearBottomLeft, CameraFrustumColor);
        AddLine(drawList, nearBottomLeft, nearTopLeft, CameraFrustumColor);
        AddLine(drawList, farTopLeft, farTopRight, CameraFrustumColor);
        AddLine(drawList, farTopRight, farBottomRight, CameraFrustumColor);
        AddLine(drawList, farBottomRight, farBottomLeft, CameraFrustumColor);
        AddLine(drawList, farBottomLeft, farTopLeft, CameraFrustumColor);
        AddLine(drawList, nearTopLeft, farTopLeft, CameraFrustumColor);
        AddLine(drawList, nearTopRight, farTopRight, CameraFrustumColor);
        AddLine(drawList, nearBottomRight, farBottomRight, CameraFrustumColor);
        AddLine(drawList, nearBottomLeft, farBottomLeft, CameraFrustumColor);
    }

    void Gizmos::AddLightIcon(const LightComponent& light, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const
    {
        const TransformComponent* transform = light.GetOwner() != nullptr ? light.GetOwner()->GetComponent<TransformComponent>() : nullptr;
        const Matrix44 world = transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity();
        AddIcon(world, desc, BuiltinGizmoIcon::Light, LightGizmoColor, drawList);
    }

    void Gizmos::AddDirectionalLightDirection(const LightComponent& light, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const
    {
        const TransformComponent* transform = light.GetOwner() != nullptr ? light.GetOwner()->GetComponent<TransformComponent>() : nullptr;
        const Matrix44 world = transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity();
        const Vector3 origin = GetTranslation(world);
        const Vector3 direction = GetCameraForward(world);
        const Vector3 end = origin + (direction * DirectionLightLineLength);
        const Vector3 sceneViewRight = GetCameraRight(desc.sceneViewCameraLocalToWorld);
        const Vector3 arrowBase = end - (direction * DirectionLightArrowLength);

        AddLine(drawList, origin, end, DirectionalLightColor);
        AddLine(drawList, end, arrowBase + (sceneViewRight * DirectionLightArrowWidth), DirectionalLightColor);
        AddLine(drawList, end, arrowBase - (sceneViewRight * DirectionLightArrowWidth), DirectionalLightColor);
    }
} // namespace ve::editor
