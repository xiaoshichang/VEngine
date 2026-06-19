#include "Editor/Panels/InspectorPanel.h"

#include "Editor/Core/Editor.h"
#include "Editor/Core/EditorAssetDatabase.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/Component.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/LightComponent.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <algorithm>
#include <array>
#include <imgui.h>
#include <string>

namespace ve::editor
{
    namespace
    {
        constexpr SizeT TextBufferSize = 512;
        constexpr float TransformDragSpeed = 0.05f;
        constexpr float RotationDragSpeed = 0.01f;
        constexpr float BoundsDragSpeed = 0.05f;
        constexpr float FineDragSpeed = 0.01f;
        constexpr float MediumDragSpeed = 0.1f;
        constexpr float LargeDragSpeed = 1.0f;

        [[nodiscard]] std::array<char, TextBufferSize> ToTextBuffer(const std::string& value)
        {
            std::array<char, TextBufferSize> buffer{};
            const size_t copySize = (std::min)(value.size(), buffer.size() - 1);
            value.copy(buffer.data(), copySize);
            buffer[copySize] = '\0';
            return buffer;
        }

        [[nodiscard]] std::array<float, 3> ToFloat3(const Vector3& value) noexcept
        {
            return {value.GetX(), value.GetY(), value.GetZ()};
        }

        [[nodiscard]] Vector3 FromFloat3(const std::array<float, 3>& value) noexcept
        {
            return Vector3(value[0], value[1], value[2]);
        }

        [[nodiscard]] std::array<float, 4> ToFloat4(const Quaternion& value) noexcept
        {
            return {value.GetX(), value.GetY(), value.GetZ(), value.GetW()};
        }

        [[nodiscard]] std::array<float, 4> ToFloat4(const rhi::RhiColor& value) noexcept
        {
            return {value.r, value.g, value.b, value.a};
        }

        [[nodiscard]] Quaternion FromFloat4(const std::array<float, 4>& value) noexcept
        {
            return Quaternion(value[0], value[1], value[2], value[3]).Normalized();
        }

        [[nodiscard]] rhi::RhiColor ToRhiColor(const std::array<float, 4>& value) noexcept
        {
            return rhi::RhiColor{value[0], value[1], value[2], value[3]};
        }

        bool RenderEnabledCheckbox(Component& component)
        {
            bool enabled = component.IsEnabled();
            if (ImGui::Checkbox("Enabled", &enabled))
            {
                component.SetEnabled(enabled);
                return true;
            }

            return false;
        }

        [[nodiscard]] std::string ResolveAssetPathFromID(const EditorAssetDatabase& assetDatabase, const AssetID& id)
        {
            if (id.IsEmpty())
            {
                return {};
            }

            const EditorAssetRecord* asset = assetDatabase.FindAssetByID(id);
            if (asset != nullptr)
            {
                return asset->path.GetString();
            }

            return "Missing asset: " + id.ToString();
        }

        void RenderAssetReferenceField(const char* label, const char* pathInputId, const char* buttonId, const std::string& assetPath)
        {
            std::array<char, TextBufferSize> pathBuffer = ToTextBuffer(assetPath);
            const ImGuiStyle& style = ImGui::GetStyle();
            const float buttonWidth = ImGui::CalcTextSize("...").x + style.FramePadding.x * 2.0f;

            ImGui::TextUnformatted(label);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-buttonWidth - style.ItemSpacing.x);
            ImGui::InputText(pathInputId, pathBuffer.data(), pathBuffer.size(), ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            if (ImGui::Button(buttonId))
            {
            }
        }
    } // namespace

    void InspectorPanel::Render(Editor& editor, const ImVec2& position, const ImVec2& size)
    {
        editor_ = &editor;
        BasePanel::Render(position, size);
        editor_ = nullptr;
    }

    const char* InspectorPanel::GetName() const noexcept
    {
        return "Inspector";
    }

    void InspectorPanel::RenderContent()
    {
        if (editor_ == nullptr)
        {
            return;
        }

        switch (editor_->GetSelectionType())
        {
        case EditorSelectionType::GameObject:
        {
            GameObject* selectedGameObject = editor_->GetSelectedGameObject();
            if (selectedGameObject != nullptr)
            {
                RenderGameObject(*selectedGameObject);
            }
            return;
        }
        case EditorSelectionType::Asset:
        {
            const EditorAssetRecord* asset = editor_->GetAssetDatabase().FindAsset(editor_->GetSelectedAssetPath());
            if (asset != nullptr)
            {
                RenderAsset(*asset);
            }
            else
            {
                ImGui::TextDisabled("Selected asset was not found.");
            }
            return;
        }
        case EditorSelectionType::None:
            break;
        }

        ImGui::TextDisabled("Nothing selected.");
    }

    void InspectorPanel::RenderGameObject(GameObject& gameObject)
    {
        std::array<char, TextBufferSize> nameBuffer = ToTextBuffer(gameObject.GetName());
        if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size()))
        {
            gameObject.SetName(nameBuffer.data());
        }

        ImGui::TextDisabled("%zu component(s)", static_cast<size_t>(gameObject.GetComponentCount()));
        ImGui::Separator();

        if (TransformComponent* transform = gameObject.GetComponent<TransformComponent>(); transform != nullptr)
        {
            RenderTransformComponent(*transform);
        }

        if (MeshRenderComponent* mesh = gameObject.GetComponent<MeshRenderComponent>(); mesh != nullptr)
        {
            RenderMeshRenderComponent(*mesh);
        }

        if (CameraComponent* camera = gameObject.GetComponent<CameraComponent>(); camera != nullptr)
        {
            RenderCameraComponent(*camera);
        }

        if (LightComponent* light = gameObject.GetComponent<LightComponent>(); light != nullptr)
        {
            RenderLightComponent(*light);
        }
    }

    void InspectorPanel::RenderTransformComponent(TransformComponent& transform)
    {
        ImGui::PushID(&transform);
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            RenderEnabledCheckbox(transform);

            std::array<float, 3> position = ToFloat3(transform.GetLocalPosition());
            if (ImGui::DragFloat3("Position", position.data(), TransformDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                transform.SetLocalPosition(FromFloat3(position));
            }

            std::array<float, 4> rotation = ToFloat4(transform.GetLocalRotation());
            if (ImGui::DragFloat4("Rotation XYZW", rotation.data(), RotationDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                transform.SetLocalRotation(FromFloat4(rotation));
            }

            std::array<float, 3> scale = ToFloat3(transform.GetLocalScale());
            if (ImGui::DragFloat3("Scale", scale.data(), TransformDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                transform.SetLocalScale(FromFloat3(scale));
            }
        }
        ImGui::PopID();
    }

    void InspectorPanel::RenderMeshRenderComponent(MeshRenderComponent& mesh)
    {
        ImGui::PushID(&mesh);
        if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
        {
            RenderEnabledCheckbox(mesh);

            const EditorAssetDatabase* assetDatabase = editor_ != nullptr ? &editor_->GetAssetDatabase() : nullptr;
            const std::string meshAssetPath = assetDatabase != nullptr ? ResolveAssetPathFromID(*assetDatabase, mesh.GetMeshAssetID()) : "";
            RenderAssetReferenceField("Mesh", "##MeshReferencePath", "##MeshReference", meshAssetPath);

            const std::string materialAssetPath = assetDatabase != nullptr ? ResolveAssetPathFromID(*assetDatabase, mesh.GetMaterialAssetID()) : "";
            RenderAssetReferenceField("Material", "##MaterialReferencePath", "##MaterialReference", materialAssetPath);

            std::array<float, 3> boundsCenter = ToFloat3(mesh.GetBoundsCenter());
            if (ImGui::DragFloat3("Bounds Center", boundsCenter.data(), BoundsDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                mesh.SetBoundsCenter(FromFloat3(boundsCenter));
            }

            std::array<float, 3> boundsExtents = ToFloat3(mesh.GetBoundsExtents());
            if (ImGui::DragFloat3("Bounds Extents", boundsExtents.data(), BoundsDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                mesh.SetBoundsExtents(FromFloat3(boundsExtents));
            }
        }
        ImGui::PopID();
    }

    void InspectorPanel::RenderCameraComponent(CameraComponent& camera)
    {
        ImGui::PushID(&camera);
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            RenderEnabledCheckbox(camera);

            bool primary = camera.IsPrimary();
            if (ImGui::Checkbox("Primary", &primary))
            {
                camera.SetPrimary(primary);
            }

            int projectionMode = camera.GetProjectionMode() == CameraComponent::ProjectionMode::Perspective ? 0 : 1;
            const char* projectionModes[] = {"Perspective", "Orthographic"};
            if (ImGui::Combo("Projection", &projectionMode, projectionModes, IM_ARRAYSIZE(projectionModes)))
            {
                camera.SetProjectionMode(projectionMode == 0 ? CameraComponent::ProjectionMode::Perspective : CameraComponent::ProjectionMode::Orthographic);
            }

            float verticalFieldOfView = camera.GetVerticalFieldOfViewRadians();
            if (ImGui::DragFloat("FOV Radians", &verticalFieldOfView, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                camera.SetVerticalFieldOfViewRadians(verticalFieldOfView);
            }

            float orthographicSize = camera.GetOrthographicSize();
            if (ImGui::DragFloat("Ortho Size", &orthographicSize, MediumDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                camera.SetOrthographicSize(orthographicSize);
            }

            float aspectRatio = camera.GetAspectRatio();
            if (ImGui::DragFloat("Aspect", &aspectRatio, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                camera.SetAspectRatio(aspectRatio);
            }

            float nearClipPlane = camera.GetNearClipPlane();
            if (ImGui::DragFloat("Near", &nearClipPlane, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                camera.SetNearClipPlane(nearClipPlane);
            }

            float farClipPlane = camera.GetFarClipPlane();
            if (ImGui::DragFloat("Far", &farClipPlane, LargeDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                camera.SetFarClipPlane(farClipPlane);
            }

            std::array<float, 4> clearColor = ToFloat4(camera.GetClearColor());
            if (ImGui::ColorEdit4("Clear Color", clearColor.data()))
            {
                camera.SetClearColor(ToRhiColor(clearColor));
            }
        }
        ImGui::PopID();
    }

    void InspectorPanel::RenderLightComponent(LightComponent& light)
    {
        ImGui::PushID(&light);
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen))
        {
            RenderEnabledCheckbox(light);

            int lightType = light.GetLightType() == LightType::Directional ? 0 : 1;
            const char* lightTypes[] = {"Directional", "Point"};
            if (ImGui::Combo("Type", &lightType, lightTypes, IM_ARRAYSIZE(lightTypes)))
            {
                light.SetLightType(lightType == 0 ? LightType::Directional : LightType::Point);
            }

            std::array<float, 3> color = ToFloat3(light.GetColor());
            if (ImGui::ColorEdit3("Color", color.data()))
            {
                light.SetColor(FromFloat3(color));
            }

            float intensity = light.GetIntensity();
            if (ImGui::DragFloat("Intensity", &intensity, MediumDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                light.SetIntensity(intensity);
            }

            float range = light.GetRange();
            if (ImGui::DragFloat("Range", &range, MediumDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                light.SetRange(range);
            }

            float innerConeAngle = light.GetInnerConeAngleRadians();
            if (ImGui::DragFloat("Inner Cone", &innerConeAngle, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                light.SetInnerConeAngleRadians(innerConeAngle);
            }

            float outerConeAngle = light.GetOuterConeAngleRadians();
            if (ImGui::DragFloat("Outer Cone", &outerConeAngle, FineDragSpeed, 0.0f, 0.0f, "%.3f"))
            {
                light.SetOuterConeAngleRadians(outerConeAngle);
            }

            bool castShadows = light.CastShadows();
            if (ImGui::Checkbox("Cast Shadows", &castShadows))
            {
                light.SetCastShadows(castShadows);
            }
        }
        ImGui::PopID();
    }

    void InspectorPanel::RenderAsset(const EditorAssetRecord& asset)
    {
        ImGui::TextUnformatted(asset.path.GetFilename().c_str());
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Asset", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("Type: %s", EditorAssetDatabase::ToString(asset.type));
            ImGui::TextWrapped("AssetID: %s", asset.asset.id.ToString().c_str());
            ImGui::TextWrapped("Path: %s", asset.path.GetString().c_str());
            ImGui::TextWrapped("Meta: %s", asset.metaPath.GetString().c_str());
            ImGui::TextWrapped("Physical Path: %s", (editor_->GetAssetDatabase().GetProjectRoot() / asset.path).GetString().c_str());
            ImGui::Text("Imported: %s", asset.imported ? "Yes" : "No");

            if (!asset.importedPath.IsEmpty())
            {
                ImGui::TextWrapped("Imported Path: %s", asset.importedPath.GetString().c_str());
            }
        }
    }
} // namespace ve::editor
