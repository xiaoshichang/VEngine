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
        constexpr Float32 IconRadius = 0.18f;
        constexpr Float32 IconLineThickness = 0.025f;
        constexpr Float32 FrustumLineThickness = 0.018f;
        constexpr UInt32 CircleSegmentCount = 24;
        const Vector3 CameraGizmoColor(0.95f, 0.78f, 0.22f);
        const Vector3 LightGizmoColor(1.0f, 0.92f, 0.38f);
        const Vector3 CameraFrustumColor(0.35f, 0.68f, 1.0f);

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

        void AddVertex(EditorGizmoDrawList& drawList, const Vector3& position, const Vector3& color)
        {
            EditorGizmoVertex vertex = {};
            vertex.position[0] = position.GetX();
            vertex.position[1] = position.GetY();
            vertex.position[2] = position.GetZ();
            vertex.color[0] = color.GetX();
            vertex.color[1] = color.GetY();
            vertex.color[2] = color.GetZ();
            drawList.vertices.push_back(vertex);
        }

        void AddTriangle(EditorGizmoDrawList& drawList, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& color)
        {
            AddVertex(drawList, a, color);
            AddVertex(drawList, b, color);
            AddVertex(drawList, c, color);
        }

        void AddQuad(EditorGizmoDrawList& drawList, const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d, const Vector3& color)
        {
            AddTriangle(drawList, a, b, c, color);
            AddTriangle(drawList, a, c, d, color);
            AddTriangle(drawList, c, b, a, color);
            AddTriangle(drawList, d, c, a, color);
        }

        void AddCameraFacingLine(EditorGizmoDrawList& drawList,
                                 const Vector3& a,
                                 const Vector3& b,
                                 const Vector3& color,
                                 const Vector3& sceneViewCameraPosition,
                                 Float32 thickness)
        {
            const Vector3 line = b - a;
            if (line.LengthSquared() <= Math::DefaultEpsilon)
            {
                return;
            }

            const Vector3 lineDirection = line.Normalized();
            Vector3 viewDirection = (sceneViewCameraPosition - ((a + b) * 0.5f)).Normalized();
            if (viewDirection.LengthSquared() <= Math::DefaultEpsilon)
            {
                viewDirection = Vector3::UnitY();
            }

            Vector3 side = Vector3::Cross(lineDirection, viewDirection).Normalized();
            if (side.LengthSquared() <= Math::DefaultEpsilon)
            {
                side = Vector3::Cross(lineDirection, Vector3::UnitY()).Normalized();
            }
            if (side.LengthSquared() <= Math::DefaultEpsilon)
            {
                side = Vector3::Cross(lineDirection, Vector3::UnitX()).Normalized();
            }

            const Vector3 offset = side * (thickness * 0.5f);
            AddQuad(drawList, a - offset, a + offset, b + offset, b - offset, color);
        }

        void AddBillboardLine(EditorGizmoDrawList& drawList,
                              const Vector3& center,
                              const Vector3& localA,
                              const Vector3& localB,
                              const Vector3& color,
                              const Vector3& right,
                              const Vector3& up)
        {
            const Vector3 a = center + (right * localA.GetX()) + (up * localA.GetY());
            const Vector3 b = center + (right * localB.GetX()) + (up * localB.GetY());
            const Vector3 side = ((right * (localB.GetY() - localA.GetY())) - (up * (localB.GetX() - localA.GetX()))).Normalized();
            const Vector3 offset = side * (IconLineThickness * 0.5f);
            AddQuad(drawList, a - offset, a + offset, b + offset, b - offset, color);
        }

        void AddBillboardCircle(EditorGizmoDrawList& drawList, const Vector3& center, const Vector3& color, const Vector3& right, const Vector3& up)
        {
            for (UInt32 segmentIndex = 0; segmentIndex < CircleSegmentCount; ++segmentIndex)
            {
                const Float32 a = (static_cast<Float32>(segmentIndex) / static_cast<Float32>(CircleSegmentCount)) * Math::TwoPi;
                const Float32 b = (static_cast<Float32>(segmentIndex + 1) / static_cast<Float32>(CircleSegmentCount)) * Math::TwoPi;
                AddBillboardLine(drawList,
                                 center,
                                 Vector3(Cos(a) * IconRadius, Sin(a) * IconRadius, 0.0f),
                                 Vector3(Cos(b) * IconRadius, Sin(b) * IconRadius, 0.0f),
                                 color,
                                 right,
                                 up);
            }
        }

        [[nodiscard]] bool IsSelectedCamera(const GameObject* selectedGameObject, const CameraComponent& camera) noexcept
        {
            return selectedGameObject != nullptr && selectedGameObject == camera.GetOwner();
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
        drawList->vertices.reserve(1024);
        for (SizeT rootIndex = 0; rootIndex < desc.scene->GetRootGameObjectCount(); ++rootIndex)
        {
            const GameObject* root = desc.scene->GetRootGameObject(rootIndex);
            if (root != nullptr)
            {
                CollectGameObjectGizmos(*root, desc, *drawList);
            }
        }

        return drawList->vertices.empty() ? nullptr : drawList;
    }

    void Gizmos::CollectGameObjectGizmos(const GameObject& gameObject, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const
    {
        if (componentVisibility_.camera)
        {
            if (const CameraComponent* camera = gameObject.GetComponent<CameraComponent>(); camera != nullptr && camera->IsEnabled())
            {
                AddCameraIcon(*camera, desc, drawList);
                if (IsSelectedCamera(desc.selectedGameObject, *camera))
                {
                    AddCameraFrustum(*camera, desc, drawList);
                }
            }
        }

        if (componentVisibility_.light)
        {
            if (const LightComponent* light = gameObject.GetComponent<LightComponent>(); light != nullptr && light->IsEnabled())
            {
                AddLightIcon(*light, desc, drawList);
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

    void Gizmos::AddCameraIcon(const CameraComponent& camera, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const
    {
        const TransformComponent* transform = camera.GetOwner() != nullptr ? camera.GetOwner()->GetComponent<TransformComponent>() : nullptr;
        const Matrix44 world = transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity();
        const Vector3 center = GetTranslation(world);
        const Vector3 right = GetCameraRight(desc.sceneViewCameraLocalToWorld);
        const Vector3 up = GetCameraUp(desc.sceneViewCameraLocalToWorld);

        AddBillboardLine(drawList, center, Vector3(-0.24f, -0.13f, 0.0f), Vector3(0.10f, -0.13f, 0.0f), CameraGizmoColor, right, up);
        AddBillboardLine(drawList, center, Vector3(0.10f, -0.13f, 0.0f), Vector3(0.10f, 0.13f, 0.0f), CameraGizmoColor, right, up);
        AddBillboardLine(drawList, center, Vector3(0.10f, 0.13f, 0.0f), Vector3(-0.24f, 0.13f, 0.0f), CameraGizmoColor, right, up);
        AddBillboardLine(drawList, center, Vector3(-0.24f, 0.13f, 0.0f), Vector3(-0.24f, -0.13f, 0.0f), CameraGizmoColor, right, up);
        AddBillboardLine(drawList, center, Vector3(0.10f, 0.08f, 0.0f), Vector3(0.28f, 0.20f, 0.0f), CameraGizmoColor, right, up);
        AddBillboardLine(drawList, center, Vector3(0.10f, -0.08f, 0.0f), Vector3(0.28f, -0.20f, 0.0f), CameraGizmoColor, right, up);
        AddBillboardLine(drawList, center, Vector3(0.28f, 0.20f, 0.0f), Vector3(0.28f, -0.20f, 0.0f), CameraGizmoColor, right, up);
    }

    void Gizmos::AddCameraFrustum(const CameraComponent& camera, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const
    {
        const TransformComponent* transform = camera.GetOwner() != nullptr ? camera.GetOwner()->GetComponent<TransformComponent>() : nullptr;
        const Matrix44 world = transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity();
        const Vector3 origin = GetTranslation(world);
        const Vector3 right = GetCameraRight(world);
        const Vector3 up = GetCameraUp(world);
        const Vector3 forward = GetCameraForward(world);
        const Vector3 sceneViewCameraPosition = GetTranslation(desc.sceneViewCameraLocalToWorld);
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

        AddCameraFacingLine(drawList, nearTopLeft, nearTopRight, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
        AddCameraFacingLine(drawList, nearTopRight, nearBottomRight, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
        AddCameraFacingLine(drawList, nearBottomRight, nearBottomLeft, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
        AddCameraFacingLine(drawList, nearBottomLeft, nearTopLeft, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
        AddCameraFacingLine(drawList, farTopLeft, farTopRight, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
        AddCameraFacingLine(drawList, farTopRight, farBottomRight, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
        AddCameraFacingLine(drawList, farBottomRight, farBottomLeft, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
        AddCameraFacingLine(drawList, farBottomLeft, farTopLeft, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
        AddCameraFacingLine(drawList, nearTopLeft, farTopLeft, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
        AddCameraFacingLine(drawList, nearTopRight, farTopRight, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
        AddCameraFacingLine(drawList, nearBottomRight, farBottomRight, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
        AddCameraFacingLine(drawList, nearBottomLeft, farBottomLeft, CameraFrustumColor, sceneViewCameraPosition, FrustumLineThickness);
    }

    void Gizmos::AddLightIcon(const LightComponent& light, const GizmoBuildDesc& desc, EditorGizmoDrawList& drawList) const
    {
        const TransformComponent* transform = light.GetOwner() != nullptr ? light.GetOwner()->GetComponent<TransformComponent>() : nullptr;
        const Matrix44 world = transform != nullptr ? transform->GetWorldMatrix() : Matrix44::Identity();
        const Vector3 center = GetTranslation(world);
        const Vector3 right = GetCameraRight(desc.sceneViewCameraLocalToWorld);
        const Vector3 up = GetCameraUp(desc.sceneViewCameraLocalToWorld);

        AddBillboardCircle(drawList, center, LightGizmoColor, right, up);
        AddBillboardLine(drawList, center, Vector3(-0.28f, 0.0f, 0.0f), Vector3(0.28f, 0.0f, 0.0f), LightGizmoColor, right, up);
        AddBillboardLine(drawList, center, Vector3(0.0f, -0.28f, 0.0f), Vector3(0.0f, 0.28f, 0.0f), LightGizmoColor, right, up);
    }
} // namespace ve::editor
