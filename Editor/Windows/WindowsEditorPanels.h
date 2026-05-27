#pragma once

#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Scene/SceneTypes.h"

#include <functional>
#include <string>

namespace ve
{
    class Component;
    class EditorProjectService;
    class EngineRuntime;
    class GameObject;
    struct AssetRecord;

    class WindowsEditorPanels
    {
    public:
        using OpenSceneRequest = std::function<void(const Path&)>;

        WindowsEditorPanels();

        void ResetSelection() noexcept;

        void DrawSceneView(const EditorProjectService& projectService);
        void DrawGameView(const EditorProjectService& projectService);
        void DrawSceneHierarchy(EditorProjectService& projectService,
                                EngineRuntime& runtime,
                                std::string& statusMessage);
        void DrawInspector(EditorProjectService& projectService, EngineRuntime& runtime, std::string& statusMessage);
        void DrawAssetBrowser(EditorProjectService& projectService,
                              EngineRuntime& runtime,
                              std::string& statusMessage,
                              const OpenSceneRequest& openSceneRequest);

    private:
        void DrawViewportSurface(const char* label, const EditorProjectService& projectService);
        void DrawGameObjectNode(GameObject& gameObject);

        void DrawComponentInspector(EditorProjectService& projectService,
                                    EngineRuntime& runtime,
                                    std::string& statusMessage,
                                    SizeT componentIndex,
                                    Component& component);
        void DrawReflectedProperty(EditorProjectService& projectService,
                                   EngineRuntime& runtime,
                                   std::string& statusMessage,
                                   SizeT componentIndex,
                                   Component& component,
                                   const ReflectedPropertyInfo& property);

        void DrawAssetTable(EditorProjectService& projectService,
                            EngineRuntime& runtime,
                            std::string& statusMessage,
                            const OpenSceneRequest& openSceneRequest);
        void DrawAssetCommands(EditorProjectService& projectService,
                               EngineRuntime& runtime,
                               std::string& statusMessage,
                               const AssetRecord& record,
                               const OpenSceneRequest& openSceneRequest);
        void ImportAsset(EditorProjectService& projectService,
                         std::string& statusMessage,
                         const AssetRecord& record,
                         bool force);

        void PrepareSceneMutation(EngineRuntime& runtime);
        void FinishSceneMutation(EditorProjectService& projectService);

        ReflectionRegistry reflectionRegistry_;
        SceneObjectId selectedGameObjectId_ = InvalidSceneObjectId;
    };
} // namespace ve
