#include "Editor/Core/Editor.h"
#include "Editor/Core/EditorAssetDatabase.h"
#include "Editor/Panels/InspectorPanel/InspectorPanel.h"
#include "Editor/Panels/InspectorPanel/InspectorPanelInternal.h"
#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Resource/MaterialProperty.h"

#include <array>
#include <boost/json.hpp>
#include <exception>
#include <imgui.h>
#include <string>
#include <string_view>
#include <vector>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] std::string ReadString(const boost::json::object& object, boost::json::string_view key, std::string fallback = {})
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_string())
            {
                return std::string(value->as_string());
            }

            return fallback;
        }

        [[nodiscard]] Path ResolveProjectRelativeReference(std::string_view reference, std::string_view fallbackExtension)
        {
            if (reference.empty())
            {
                return Path();
            }

            std::string path(reference);
            if (!path.starts_with("Assets/"))
            {
                path = "Assets/" + path;
            }

            if (Path(path).GetExtension().empty() && !fallbackExtension.empty())
            {
                path += fallbackExtension;
            }

            return Path(path);
        }

        [[nodiscard]] Result<boost::json::object> ReadJsonObjectFile(const Path& path)
        {
            Result<std::string> text = FileSystem::ReadTextFile(path);
            if (!text)
            {
                return Result<boost::json::object>::Failure(text.GetError());
            }

            Result<boost::json::value> json = JsonUtils::Parse(text.GetValue());
            if (!json || !json.GetValue().is_object())
            {
                return Result<boost::json::object>::Failure(Error(ErrorCode::InvalidArgument, "JSON root must be an object."));
            }

            return Result<boost::json::object>::Success(json.GetValue().as_object());
        }

        [[nodiscard]] MaterialResource* FindLoadedMaterialResource(ResourceSystem& resourceSystem, const AssetID& id)
        {
            ResourceObject* resource = resourceSystem.FindLoadedResource(id);
            if (resource == nullptr || resource->GetType() != ResourceType::Material)
            {
                return nullptr;
            }

            return dynamic_cast<MaterialResource*>(resource);
        }

        [[nodiscard]] Result<AssetID> ParseAssetIDText(const std::string& text)
        {
            if (text.empty())
            {
                return Result<AssetID>::Success(AssetID());
            }

            const size_t separator = text.find(':');
            const std::string guidText = separator == std::string::npos ? text : text.substr(0, separator);
            UInt64 subID = 0;
            if (separator != std::string::npos && separator + 1 < text.size())
            {
                try
                {
                    subID = static_cast<UInt64>(std::stoull(text.substr(separator + 1)));
                }
                catch (const std::exception&)
                {
                    return Result<AssetID>::Failure(Error(ErrorCode::InvalidArgument, "Invalid AssetID subID."));
                }
            }

            Result<Guid> guid = Guid::Parse(guidText);
            if (!guid)
            {
                return Result<AssetID>::Failure(guid.GetError());
            }

            return Result<AssetID>::Success(AssetID(guid.GetValue(), subID));
        }
    } // namespace

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

        if (asset.type == EditorAssetType::Material)
        {
            RenderMaterialAsset(asset);
        }
    }

    void InspectorPanel::RenderMaterialAsset(const EditorAssetRecord& asset)
    {
        if (editor_ == nullptr)
        {
            return;
        }

        if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        if (editor_->IsPlaying())
        {
            ImGui::TextDisabled("Stop Play mode to edit material assets.");
            return;
        }

        const EditorAssetDatabase& assetDatabase = editor_->GetAssetDatabase();
        const Path materialPhysicalPath = assetDatabase.GetProjectRoot() / asset.path;
        Result<boost::json::object> materialJson = ReadJsonObjectFile(materialPhysicalPath);
        if (!materialJson)
        {
            ImGui::TextDisabled("Failed to read material.");
            return;
        }

        const Path shaderProjectPath = ResolveProjectRelativeReference(ReadString(materialJson.GetValue(), "shader"), ".veshader");
        const EditorAssetRecord* shaderAsset = assetDatabase.FindAsset(shaderProjectPath);
        if (shaderAsset == nullptr)
        {
            ImGui::TextDisabled("Material shader was not found.");
            return;
        }

        Result<boost::json::object> shaderJson = ReadJsonObjectFile(assetDatabase.GetProjectRoot() / shaderAsset->asset.runtimePath);
        if (!shaderJson)
        {
            ImGui::TextDisabled("Failed to read imported shader.");
            return;
        }

        Result<ShaderMaterialLayout> layout = ReadShaderMaterialLayoutJson(shaderJson.GetValue());
        if (!layout)
        {
            ImGui::TextDisabled("Shader material layout is invalid.");
            return;
        }

        const boost::json::value* propertiesJson = materialJson.GetValue().if_contains("properties");
        if (propertiesJson == nullptr || !propertiesJson->is_object())
        {
            ImGui::TextDisabled("Material properties are missing.");
            return;
        }

        Result<std::vector<MaterialPropertyValue>> values = ResolveMaterialPropertyValues(layout.GetValue(), propertiesJson->as_object());
        if (!values)
        {
            ImGui::TextDisabled("Material properties are invalid.");
            return;
        }

        bool changed = false;
        bool textureChanged = false;
        std::vector<MaterialPropertyValue> editableValues = values.MoveValue();

        for (SizeT index = 0; index < layout.GetValue().properties.size() && index < editableValues.size(); ++index)
        {
            const ShaderMaterialPropertyDesc& property = layout.GetValue().properties[index];
            MaterialPropertyValue& value = editableValues[index];
            ImGui::PushID(property.name.c_str());

            if (value.type == MaterialPropertyType::Color)
            {
                std::array<float, 4> color = ToFloat4(value.vectorValue);
                if (RenderFieldColorEdit4(property.displayName.c_str(), color.data()))
                {
                    value.vectorValue = FromFloat4Vector(color);
                    changed = true;
                }
            }
            else if (value.type == MaterialPropertyType::Float4)
            {
                std::array<float, 4> vector = ToFloat4(value.vectorValue);
                if (RenderFieldDragFloat4(property.displayName.c_str(), vector.data(), FineDragSpeed))
                {
                    value.vectorValue = FromFloat4Vector(vector);
                    changed = true;
                }
            }
            else if (value.type == MaterialPropertyType::Float)
            {
                float floatValue = value.floatValue;
                if (RenderFieldDragFloat(property.displayName.c_str(), &floatValue, FineDragSpeed))
                {
                    value.floatValue = floatValue;
                    changed = true;
                }
            }
            else if (value.type == MaterialPropertyType::Texture2D)
            {
                std::array<char, TextBufferSize> idBuffer = ToTextBuffer(value.assetValue.IsEmpty() ? "" : value.assetValue.ToString());
                if (RenderFieldInputText(property.displayName.c_str(), idBuffer.data(), idBuffer.size(), ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    Result<AssetID> parsedID = ParseAssetIDText(idBuffer.data());
                    if (parsedID)
                    {
                        value.assetValue = parsedID.GetValue();
                        changed = true;
                        textureChanged = true;
                    }
                }

                const std::string assetPath = ResolveAssetPathFromID(assetDatabase, value.assetValue);
                ImGui::TextDisabled("%s", assetPath.empty() ? "No texture" : assetPath.c_str());
                if (!value.assetValue.IsEmpty() && ImGui::Button("Clear"))
                {
                    value.assetValue = AssetID();
                    changed = true;
                    textureChanged = true;
                }
            }

            ImGui::PopID();
        }

        if (!changed)
        {
            return;
        }

        materialJson.GetValue()["properties"] = WriteMaterialPropertyValuesJson(layout.GetValue(), editableValues);
        const ErrorCode writeResult = FileSystem::WriteTextFile(materialPhysicalPath, JsonUtils::SerializePretty(materialJson.GetValue()));
        if (writeResult != ErrorCode::None)
        {
            ImGui::TextDisabled("Failed to save material.");
            return;
        }

        MaterialResource* loadedMaterial = FindLoadedMaterialResource(editor_->GetRuntime().GetResourceSystem(), asset.asset.id);
        if (loadedMaterial != nullptr)
        {
            for (SizeT index = 0; index < layout.GetValue().properties.size() && index < editableValues.size(); ++index)
            {
                const ErrorCode setResult = loadedMaterial->SetPropertyValue(layout.GetValue().properties[index].name, editableValues[index]);
                (void)setResult;
            }
            loadedMaterial->SyncRenderResource(editor_->GetRenderSystem());
        }

        if (textureChanged)
        {
            const ErrorCode refreshResult = editor_->GetAssetDatabase().Refresh();
            (void)refreshResult;
        }
    }
} // namespace ve::editor
