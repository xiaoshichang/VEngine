#include "Editor/Windows/WindowsEditorPanels.h"

#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/Component.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/RenderComponents.h"
#include "Engine/Runtime/Scripting/ScriptComponent.h"

#include <boost/json.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <regex>
#include <string>
#include <typeinfo>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ve
{
    namespace
    {
        enum class AssetArtifactStatus
        {
            Authored,
            Ready,
            Missing,
            Unknown,
        };

        struct ScriptTypeCandidate
        {
            std::string displayName;
            std::string typeName;
            std::string assemblyName;
            Path sourcePath;
        };

        [[nodiscard]] const char* ToDisplayString(AssetArtifactStatus status) noexcept
        {
            switch (status)
            {
            case AssetArtifactStatus::Authored:
                return "Authored";
            case AssetArtifactStatus::Ready:
                return "Ready";
            case AssetArtifactStatus::Missing:
                return "Missing";
            case AssetArtifactStatus::Unknown:
            default:
                return "Unknown";
            }
        }

        [[nodiscard]] bool IsNativeAuthoredAsset(AssetType type) noexcept
        {
            return type == AssetType::Scene || type == AssetType::Material;
        }

        [[nodiscard]] bool IsObjSource(const Path& path)
        {
            return path.GetExtension() == ".obj" || path.GetExtension() == ".OBJ";
        }

        [[nodiscard]] Path GetImportSourcePath(const AssetRecord& record)
        {
            return record.source.IsEmpty() ? record.path : record.source;
        }

        [[nodiscard]] bool CanImportAssetRecord(const AssetRecord& record)
        {
            return record.assetType == AssetType::SourceModel && IsObjSource(GetImportSourcePath(record));
        }

        [[nodiscard]] std::string FormatGuidForUi(const AssetGuid& guid)
        {
            return guid.IsValid() ? guid.ToString() : std::string("(unimported)");
        }

        [[nodiscard]] AssetArtifactStatus GetArtifactStatus(const AssetDatabase& assetDatabase,
                                                            const AssetRecord& record)
        {
            if (IsNativeAuthoredAsset(record.assetType))
            {
                return AssetArtifactStatus::Authored;
            }

            if (record.assetType != AssetType::SourceModel || !record.guid.IsValid() || record.artifacts.empty())
            {
                return AssetArtifactStatus::Unknown;
            }

            for (const AssetArtifact& artifact : record.artifacts)
            {
                if (artifact.path.IsEmpty() || !FileSystem::IsFile(assetDatabase.ResolveProjectPath(artifact.path)))
                {
                    return AssetArtifactStatus::Missing;
                }
            }

            return AssetArtifactStatus::Ready;
        }

        [[nodiscard]] std::string GetImporterName(const AssetDatabase& assetDatabase, const AssetRecord& record)
        {
            if (IsNativeAuthoredAsset(record.assetType))
            {
                return "Native";
            }

            if (!record.metadataPath.IsEmpty() &&
                FileSystem::IsFile(assetDatabase.ResolveProjectPath(record.metadataPath)))
            {
                Result<SourceAssetMetadata> metadata = assetDatabase.LoadSourceMetadata(record.metadataPath);
                if (metadata && !metadata.GetValue().importer.empty())
                {
                    return metadata.GetValue().importer;
                }
            }

            return CanImportAssetRecord(record) ? "ObjModel" : "Unknown";
        }

        void DrawDisabledTextWrapped(const std::string& value)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped("%s", value.c_str());
            ImGui::PopStyleColor();
        }

        void DrawAssetInspectorField(const char* label, const std::string& value)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            DrawDisabledTextWrapped(value);
        }

        void DrawPropertyLabel(const std::string& label)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label.c_str());
        }

        [[nodiscard]] Float32 ReadJsonFloat(const boost::json::value& jsonValue,
                                            Float32 fallback = 0.0f) noexcept
        {
            if (jsonValue.is_double())
            {
                return static_cast<Float32>(jsonValue.as_double());
            }

            if (jsonValue.is_int64())
            {
                return static_cast<Float32>(jsonValue.as_int64());
            }

            if (jsonValue.is_uint64())
            {
                return static_cast<Float32>(jsonValue.as_uint64());
            }

            return fallback;
        }

        [[nodiscard]] UInt64 ReadJsonUInt64(const boost::json::value& jsonValue, UInt64 fallback = 0) noexcept
        {
            if (jsonValue.is_uint64())
            {
                return jsonValue.as_uint64();
            }

            if (jsonValue.is_int64() && jsonValue.as_int64() >= 0)
            {
                return static_cast<UInt64>(jsonValue.as_int64());
            }

            return fallback;
        }

        [[nodiscard]] bool ReadJsonFloatArray(const boost::json::value& jsonValue,
                                              Float32* values,
                                              SizeT valueCount) noexcept
        {
            if (!jsonValue.is_array() || jsonValue.as_array().size() != valueCount)
            {
                return false;
            }

            const boost::json::array& arrayValue = jsonValue.as_array();
            for (SizeT index = 0; index < valueCount; ++index)
            {
                values[index] = ReadJsonFloat(arrayValue[index]);
            }

            return true;
        }

        [[nodiscard]] boost::json::array MakeJsonFloatArray(const Float32* values, SizeT valueCount)
        {
            boost::json::array arrayValue;
            arrayValue.reserve(valueCount);
            for (SizeT index = 0; index < valueCount; ++index)
            {
                arrayValue.push_back(static_cast<double>(values[index]));
            }

            return arrayValue;
        }

        [[nodiscard]] const ReflectedTypeInfo*
        FindReflectedTypeForComponent(const ReflectionRegistry& reflectionRegistry, const Component& component)
        {
            for (const ReflectedTypeInfo& typeInfo : reflectionRegistry.GetTypes())
            {
                std::unique_ptr<Component> probe = typeInfo.componentFactory ? typeInfo.componentFactory() : nullptr;
                if (probe != nullptr && typeid(*probe) == typeid(component))
                {
                    return &typeInfo;
                }
            }

            return nullptr;
        }

        [[nodiscard]] bool HasReflectedComponentType(const ReflectionRegistry& reflectionRegistry,
                                                     const GameObject& gameObject,
                                                     const std::string& typeName)
        {
            for (const std::unique_ptr<Component>& component : gameObject.GetComponents())
            {
                const ReflectedTypeInfo* existingType = FindReflectedTypeForComponent(reflectionRegistry, *component);
                if (existingType != nullptr && existingType->name == typeName)
                {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] bool IsCSharpSourceFile(const Path& path)
        {
            const std::string extension = path.GetExtension();
            return extension == ".cs" || extension == ".CS";
        }

        [[nodiscard]] bool IsIgnoredScriptDirectory(const std::string& name)
        {
            return name == "bin" || name == "obj" || name == ".git" || name == ".vs";
        }

        void CollectCSharpSourceFiles(const Path& directory, std::vector<Path>& sourceFiles)
        {
            Result<std::vector<FileSystem::DirectoryEntry>> entries = FileSystem::ListDirectory(directory);
            if (!entries)
            {
                return;
            }

            for (const FileSystem::DirectoryEntry& entry : entries.GetValue())
            {
                if (entry.type == FileSystem::DirectoryEntryType::Directory)
                {
                    if (!IsIgnoredScriptDirectory(entry.name))
                    {
                        CollectCSharpSourceFiles(entry.path, sourceFiles);
                    }
                    continue;
                }

                if (entry.type == FileSystem::DirectoryEntryType::File && IsCSharpSourceFile(entry.path))
                {
                    sourceFiles.push_back(entry.path);
                }
            }
        }

        [[nodiscard]] std::string ExtractCSharpNamespace(const std::string& source)
        {
            static const std::regex fileScopedNamespace(
                R"(\bnamespace\s+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*;)");
            static const std::regex blockScopedNamespace(
                R"(\bnamespace\s+([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*)\s*\{)");

            std::smatch match;
            if (std::regex_search(source, match, fileScopedNamespace) && match.size() >= 2)
            {
                return match[1].str();
            }

            if (std::regex_search(source, match, blockScopedNamespace) && match.size() >= 2)
            {
                return match[1].str();
            }

            return {};
        }

        [[nodiscard]] std::vector<std::string> ExtractScriptClassNames(const std::string& source)
        {
            static const std::regex classDeclaration(
                R"(\bclass\s+([A-Za-z_][A-Za-z0-9_]*)\s*:\s*([^\{\;\r\n]+))");

            std::vector<std::string> classNames;
            for (std::sregex_iterator iter(source.begin(), source.end(), classDeclaration), end; iter != end; ++iter)
            {
                const std::smatch& match = *iter;
                if (match.size() < 3)
                {
                    continue;
                }

                const std::string baseList = match[2].str();
                if (baseList.find("ScriptBehaviour") != std::string::npos)
                {
                    classNames.push_back(match[1].str());
                }
            }

            return classNames;
        }

        [[nodiscard]] std::vector<ScriptTypeCandidate>
        DiscoverProjectScriptTypes(const EditorProjectService& projectService)
        {
            std::vector<ScriptTypeCandidate> scriptTypes;
            if (!projectService.HasOpenProject())
            {
                return scriptTypes;
            }

            const WindowsScriptProjectConfig& scripts = projectService.GetDescriptor().scripting.windows;
            if (!scripts.IsConfigured())
            {
                return scriptTypes;
            }

            const Path sourceRoot = GetWindowsScriptSourceDirectory(projectService.GetProjectRoot());
            std::vector<Path> sourceFiles;
            CollectCSharpSourceFiles(sourceRoot, sourceFiles);

            std::unordered_set<std::string> seenTypes;
            for (const Path& sourcePath : sourceFiles)
            {
                Result<std::string> sourceText = FileSystem::ReadTextFile(sourcePath);
                if (!sourceText)
                {
                    continue;
                }

                const std::string namespaceName = ExtractCSharpNamespace(sourceText.GetValue());
                for (const std::string& className : ExtractScriptClassNames(sourceText.GetValue()))
                {
                    const std::string typeName = namespaceName.empty() ? className : namespaceName + "." + className;
                    if (!seenTypes.insert(typeName).second)
                    {
                        continue;
                    }

                    ScriptTypeCandidate candidate;
                    candidate.displayName = typeName;
                    candidate.typeName = typeName;
                    candidate.assemblyName = scripts.assemblyName;
                    candidate.sourcePath = sourcePath;
                    scriptTypes.push_back(std::move(candidate));
                }
            }

            std::sort(scriptTypes.begin(),
                      scriptTypes.end(),
                      [](const ScriptTypeCandidate& left, const ScriptTypeCandidate& right)
                      { return left.typeName < right.typeName; });
            return scriptTypes;
        }

        [[nodiscard]] std::string MakeResourceDisplayText(const char* resourceKind,
                                                          ResourceId resourceId,
                                                          const std::string& resourceName)
        {
            if (resourceId == InvalidResourceId)
            {
                return std::string("None (") + resourceKind + ")";
            }

            const std::string name = resourceName.empty() ? std::string("(unnamed)") : resourceName;
            return name + " (" + resourceKind + " #" + std::to_string(resourceId) + ")";
        }

        [[nodiscard]] std::string MakeAuthoredReferenceDisplayText(const EditorAuthoredAssetReference& reference)
        {
            if (!reference.path.IsEmpty())
            {
                return reference.path.GetString();
            }

            if (reference.guid.IsValid())
            {
                return reference.guid.ToString();
            }

            return "(unassigned)";
        }

        void DrawAuthoredReferenceTooltip(const EditorAuthoredAssetReference* authoredReference,
                                          const std::string& runtimeText)
        {
            if (authoredReference == nullptr)
            {
                ImGui::SetTooltip("%s", runtimeText.c_str());
                return;
            }

            ImGui::BeginTooltip();
            ImGui::TextUnformatted("Authored reference");
            ImGui::Separator();
            ImGui::Text("Path: %s",
                        authoredReference->path.IsEmpty() ? "(none)" : authoredReference->path.GetString().c_str());
            ImGui::Text("GUID: %s",
                        authoredReference->guid.IsValid() ? authoredReference->guid.ToString().c_str() : "(none)");
            ImGui::Text("Runtime: %s", runtimeText.c_str());
            ImGui::EndTooltip();
        }

        [[nodiscard]] bool DrawMeshRendererResourceProperty(EditorProjectService& projectService,
                                                            EngineRuntime& runtime,
                                                            std::string& statusMessage,
                                                            SceneObjectId selectedGameObjectId,
                                                            SizeT componentIndex,
                                                            Component& component,
                                                            const ReflectedPropertyInfo& property,
                                                            const boost::json::value& currentValue)
        {
            if (dynamic_cast<MeshRendererComponent*>(&component) == nullptr ||
                (property.name != "mesh" && property.name != "material"))
            {
                return false;
            }

            const ResourceId resourceId = ReadJsonUInt64(currentValue);
            const bool isMesh = property.name == "mesh";
            const char* resourceKind = isMesh ? "Mesh" : "Material";
            const EditorMeshRendererAssetSlot assetSlot =
                isMesh ? EditorMeshRendererAssetSlot::Mesh : EditorMeshRendererAssetSlot::Material;
            std::string resourceName;
            if (isMesh)
            {
                const MeshResource* mesh =
                    runtime.GetResourceManager().FindMesh(ResourceHandle<MeshResource>(resourceId));
                resourceName = mesh != nullptr ? mesh->name : std::string("(missing)");
            }
            else
            {
                const MaterialResource* material =
                    runtime.GetResourceManager().FindMaterial(ResourceHandle<MaterialResource>(resourceId));
                resourceName = material != nullptr ? material->name : std::string("(missing)");
            }

            const EditorAuthoredAssetReference* authoredReference =
                projectService.FindMeshRendererAssetReference(selectedGameObjectId, componentIndex, assetSlot);
            const std::string runtimeText = MakeResourceDisplayText(resourceKind, resourceId, resourceName);
            const std::string displayText = authoredReference != nullptr
                                                ? MakeAuthoredReferenceDisplayText(*authoredReference)
                                                : runtimeText;
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(property.name.c_str());
            ImGui::SameLine();

            constexpr const char* selectButtonLabel = "Select...";
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float buttonWidth = ImGui::CalcTextSize(selectButtonLabel).x +
                                      ImGui::GetStyle().FramePadding.x * 2.0f;
            const float fieldWidth = std::max(80.0f, ImGui::GetContentRegionAvail().x - buttonWidth - spacing);

            std::array<char, 512> resourceBuffer = {};
            std::snprintf(resourceBuffer.data(), resourceBuffer.size(), "%s", displayText.c_str());
            ImGui::SetNextItemWidth(fieldWidth);
            ImGui::BeginDisabled();
            ImGui::InputText("##Resource", resourceBuffer.data(), resourceBuffer.size(), ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                DrawAuthoredReferenceTooltip(authoredReference, runtimeText);
            }

            ImGui::SameLine();
            if (ImGui::Button(selectButtonLabel))
            {
                statusMessage = std::string(resourceKind) + " selector is not implemented yet.";
            }

            return true;
        }

        void DrawReadOnlyStringProperty(const std::string& label, const std::string& value)
        {
            DrawPropertyLabel(label);

            std::array<char, 512> textBuffer = {};
            std::snprintf(textBuffer.data(), textBuffer.size(), "%s", value.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::BeginDisabled();
            ImGui::InputText("##Value", textBuffer.data(), textBuffer.size(), ImGuiInputTextFlags_ReadOnly);
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && textBuffer[0] != '\0')
            {
                ImGui::SetTooltip("%s", textBuffer.data());
            }
        }
    } // namespace

    void WindowsEditorPanels::DrawSelectedAssetInspector(EditorProjectService& projectService,
                                                         std::string& statusMessage,
                                                         const AssetRecord& record)
    {
        (void)statusMessage;

        const AssetDatabase& assetDatabase = projectService.GetAssetDatabase();
        const Path displayPath = GetAssetSelectionPath(record);

        ImGui::TextUnformatted("Asset");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", ToString(record.assetType));
        ImGui::Separator();

        constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                               ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("AssetInspectorFields", 2, tableFlags))
        {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 88.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            DrawAssetInspectorField("Path", displayPath.GetString());
            DrawAssetInspectorField("Type", ToString(record.assetType));
            DrawAssetInspectorField("GUID", FormatGuidForUi(record.guid));
            DrawAssetInspectorField("Importer", GetImporterName(assetDatabase, record));
            DrawAssetInspectorField("Artifact", ToDisplayString(GetArtifactStatus(assetDatabase, record)));

            if (!record.source.IsEmpty() && record.source != displayPath)
            {
                DrawAssetInspectorField("Source", record.source.GetString());
            }

            if (!record.metadataPath.IsEmpty())
            {
                DrawAssetInspectorField("Metadata", record.metadataPath.GetString());
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Generated Artifacts", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (record.artifacts.empty())
            {
                ImGui::TextDisabled("None");
            }
            else if (ImGui::BeginTable("AssetInspectorArtifacts", 3, tableFlags))
            {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableHeadersRow();

                for (const AssetArtifact& artifact : record.artifacts)
                {
                    const bool artifactReady =
                        !artifact.path.IsEmpty() && FileSystem::IsFile(assetDatabase.ResolveProjectPath(artifact.path));

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", artifact.type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    DrawDisabledTextWrapped(artifact.path.GetString());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextDisabled("%s", artifactReady ? "Ready" : "Missing");
                }

                ImGui::EndTable();
            }
        }

        if (!record.dependencies.empty() &&
            ImGui::CollapsingHeader("Dependencies", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("AssetInspectorDependencies", 3, tableFlags))
            {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("GUID", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const AssetDependency& dependency : record.dependencies)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", dependency.type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    DrawDisabledTextWrapped(dependency.path.GetString());
                    ImGui::TableSetColumnIndex(2);
                    DrawDisabledTextWrapped(FormatGuidForUi(dependency.guid));
                }

                ImGui::EndTable();
            }
        }
    }

    void WindowsEditorPanels::DrawInspector(EditorProjectService& projectService,
                                            EngineRuntime& runtime,
                                            std::string& statusMessage)
    {
        if (const AssetRecord* selectedAsset = FindSelectedAssetRecord(projectService))
        {
            DrawSelectedAssetInspector(projectService, statusMessage, *selectedAsset);
            return;
        }

        GameObject* selected = projectService.GetActiveScene().FindGameObject(selectedGameObjectId_);
        if (selected == nullptr)
        {
            ImGui::TextUnformatted("Inspector");
            ImGui::Separator();
            ImGui::TextDisabled("No GameObject selected");
            return;
        }

        ImGui::TextUnformatted("Inspector");
        ImGui::Separator();

        std::array<char, 256> nameBuffer = {};
        std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", selected->GetName().c_str());
        ImGui::TextUnformatted("Name");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##GameObjectName", nameBuffer.data(), nameBuffer.size()))
        {
            PrepareSceneMutation(projectService, runtime);
            selected->SetName(nameBuffer.data());
            FinishSceneMutation(projectService);
            statusMessage = "Edited GameObject name.";
        }

        bool active = selected->IsActiveSelf();
        if (ImGui::Checkbox("Active", &active))
        {
            PrepareSceneMutation(projectService, runtime);
            selected->SetActive(active);
            FinishSceneMutation(projectService);
            statusMessage = "Edited GameObject active state.";
        }

        ImGui::Separator();
        const std::vector<std::unique_ptr<Component>>& components = selected->GetComponents();
        for (SizeT componentIndex = 0; componentIndex < components.size(); ++componentIndex)
        {
            Component& component = *components[componentIndex];
            if (DrawComponentInspector(projectService, runtime, statusMessage, componentIndex, component))
            {
                break;
            }
        }

        DrawAddComponentButton(projectService, runtime, statusMessage, *selected);
    }

    bool WindowsEditorPanels::DrawComponentInspector(EditorProjectService& projectService,
                                                     EngineRuntime& runtime,
                                                     std::string& statusMessage,
                                                     SizeT componentIndex,
                                                     Component& component)
    {
        const ReflectedTypeInfo* typeInfo = FindReflectedTypeForComponent(reflectionRegistry_, component);
        const char* header = typeInfo != nullptr ? typeInfo->name.c_str() : "Component";
        ImGui::PushID(&component);

        const bool isScriptComponent = dynamic_cast<ScriptComponent*>(&component) != nullptr;
        const bool opened = ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen);
        bool removed = false;
        if (isScriptComponent && ImGui::BeginPopupContextItem("ScriptComponentContextMenu"))
        {
            if (ImGui::MenuItem("Remove Component"))
            {
                GameObject& owner = component.GetGameObject();
                PrepareSceneMutation(projectService, runtime);
                removed = owner.RemoveComponent(component);
                FinishSceneMutation(projectService);
                statusMessage = removed ? "Removed ScriptComponent." : "Failed to remove ScriptComponent.";
            }

            ImGui::EndPopup();
        }

        if (removed || !opened)
        {
            ImGui::PopID();
            return removed;
        }

        bool enabled = component.IsEnabled();
        if (ImGui::Checkbox("Enabled", &enabled))
        {
            PrepareSceneMutation(projectService, runtime);
            component.SetEnabled(enabled);
            FinishSceneMutation(projectService);
            statusMessage = "Edited component enabled state.";
        }

        if (typeInfo == nullptr)
        {
            ImGui::TextDisabled("No reflected properties");
            ImGui::PopID();
            return false;
        }

        for (const ReflectedPropertyInfo& property : typeInfo->properties)
        {
            if (property.editorVisible && property.serialize && property.deserialize)
            {
                DrawReflectedProperty(projectService, runtime, statusMessage, componentIndex, component, property);
            }
        }

        ImGui::PopID();
        return false;
    }

    void WindowsEditorPanels::DrawAddComponentButton(EditorProjectService& projectService,
                                                     EngineRuntime& runtime,
                                                     std::string& statusMessage,
                                                     GameObject& gameObject)
    {
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
        {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (!ImGui::BeginPopup("AddComponentPopup"))
        {
            return;
        }

        ImGui::TextDisabled("Components");
        for (const ReflectedTypeInfo& typeInfo : reflectionRegistry_.GetTypes())
        {
            if (!typeInfo.componentFactory || typeInfo.name == "ScriptComponent")
            {
                continue;
            }

            const bool isTransform = typeInfo.name == "TransformComponent";
            const bool alreadyHasType = isTransform &&
                                        HasReflectedComponentType(reflectionRegistry_, gameObject, typeInfo.name);
            if (ImGui::MenuItem(typeInfo.name.c_str(), nullptr, false, !alreadyHasType))
            {
                std::unique_ptr<Component> component = reflectionRegistry_.CreateComponent(typeInfo.name);
                if (component != nullptr)
                {
                    PrepareSceneMutation(projectService, runtime);
                    (void)gameObject.AddComponent(std::move(component));
                    FinishSceneMutation(projectService);
                    statusMessage = "Added " + typeInfo.name + ".";
                }
                else
                {
                    statusMessage = "Failed to create " + typeInfo.name + ".";
                }

                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::Separator();
        const std::vector<ScriptTypeCandidate> scriptTypes = DiscoverProjectScriptTypes(projectService);
        if (ImGui::BeginMenu("Scripts", projectService.HasWindowsScripts() && !scriptTypes.empty()))
        {
            for (const ScriptTypeCandidate& scriptType : scriptTypes)
            {
                if (ImGui::MenuItem(scriptType.displayName.c_str()))
                {
                    PrepareSceneMutation(projectService, runtime);
                    auto component = std::make_unique<ScriptComponent>();
                    ScriptComponent& scriptComponent =
                        static_cast<ScriptComponent&>(gameObject.AddComponent(std::move(component)));
                    scriptComponent.SetAssemblyName(scriptType.assemblyName);
                    scriptComponent.SetScriptTypeName(scriptType.typeName);
                    FinishSceneMutation(projectService);
                    statusMessage = "Added script " + scriptType.typeName + ".";
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", scriptType.sourcePath.GetString().c_str());
                }
            }

            ImGui::EndMenu();
        }

        if (!projectService.HasWindowsScripts())
        {
            ImGui::TextDisabled("No Windows scripts configured");
        }
        else if (scriptTypes.empty())
        {
            ImGui::TextDisabled("No ScriptBehaviour classes found");
        }

        ImGui::EndPopup();
    }

    void WindowsEditorPanels::DrawReflectedProperty(EditorProjectService& projectService,
                                                    EngineRuntime& runtime,
                                                    std::string& statusMessage,
                                                    SizeT componentIndex,
                                                    Component& component,
                                                    const ReflectedPropertyInfo& property)
    {
        boost::json::value currentValue = property.serialize(component);
        boost::json::value nextValue;
        bool changed = false;

        ImGui::PushID(property.name.c_str());
        switch (property.type)
        {
        case ReflectedPropertyType::Bool:
        {
            bool value = currentValue.is_bool() ? currentValue.as_bool() : false;
            if (ImGui::Checkbox(property.name.c_str(), &value))
            {
                nextValue = value;
                changed = true;
            }
            break;
        }
        case ReflectedPropertyType::Float32:
        {
            float value = ReadJsonFloat(currentValue);
            DrawPropertyLabel(property.name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##Value", &value, 0.01f))
            {
                nextValue = static_cast<double>(value);
                changed = true;
            }
            break;
        }
        case ReflectedPropertyType::String:
        {
            if (dynamic_cast<ScriptComponent*>(&component) != nullptr &&
                (property.name == "scriptTypeName" || property.name == "assemblyName"))
            {
                const std::string value =
                    currentValue.is_string() ? std::string(currentValue.as_string()) : std::string();
                DrawReadOnlyStringProperty(property.name, value);
                break;
            }

            std::array<char, 256> textBuffer = {};
            if (currentValue.is_string())
            {
                std::snprintf(textBuffer.data(),
                              textBuffer.size(),
                              "%s",
                              std::string(currentValue.as_string()).c_str());
            }

            DrawPropertyLabel(property.name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##Value", textBuffer.data(), textBuffer.size()))
            {
                nextValue = std::string(textBuffer.data());
                changed = true;
            }
            if (ImGui::IsItemHovered() && textBuffer[0] != '\0')
            {
                ImGui::SetTooltip("%s", textBuffer.data());
            }
            break;
        }
        case ReflectedPropertyType::Vector3:
        {
            Float32 values[3] = {};
            (void)ReadJsonFloatArray(currentValue, values, 3);
            DrawPropertyLabel(property.name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat3("##Value", values, 0.01f))
            {
                nextValue = MakeJsonFloatArray(values, 3);
                changed = true;
            }
            break;
        }
        case ReflectedPropertyType::Vector4:
        case ReflectedPropertyType::Quaternion:
        {
            Float32 values[4] = {};
            (void)ReadJsonFloatArray(currentValue, values, 4);
            DrawPropertyLabel(property.name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat4("##Value", values, 0.01f))
            {
                nextValue = MakeJsonFloatArray(values, 4);
                changed = true;
            }
            break;
        }
        case ReflectedPropertyType::ResourceId:
        {
            if (DrawMeshRendererResourceProperty(projectService,
                                                 runtime,
                                                 statusMessage,
                                                 selectedGameObjectId_,
                                                 componentIndex,
                                                 component,
                                                 property,
                                                 currentValue))
            {
                break;
            }

            UInt64 value = ReadJsonUInt64(currentValue);
            DrawPropertyLabel(property.name);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputScalar("##Value", ImGuiDataType_U64, &value))
            {
                nextValue = static_cast<std::uint64_t>(value);
                changed = true;
            }
            break;
        }
        case ReflectedPropertyType::Enum:
        {
            const std::string currentText =
                currentValue.is_string() ? std::string(currentValue.as_string()) : std::string();
            const ReflectedEnumInfo* enumInfo = reflectionRegistry_.FindEnum(property.enumName);
            DrawPropertyLabel(property.name);
            ImGui::SetNextItemWidth(-1.0f);
            if (enumInfo != nullptr && ImGui::BeginCombo("##Value", currentText.c_str()))
            {
                for (const ReflectedEnumValue& enumValue : enumInfo->values)
                {
                    const bool selected = enumValue.name == currentText;
                    if (ImGui::Selectable(enumValue.name.c_str(), selected))
                    {
                        nextValue = enumValue.name;
                        changed = true;
                    }

                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }

                ImGui::EndCombo();
            }
            break;
        }
        }

        if (changed)
        {
            PrepareSceneMutation(projectService, runtime);
            property.deserialize(component, nextValue);
            FinishSceneMutation(projectService);
            statusMessage = "Edited " + property.name + ".";
        }

        ImGui::PopID();
    }
} // namespace ve
