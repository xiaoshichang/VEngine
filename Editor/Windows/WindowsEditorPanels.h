#pragma once

#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Math/Vector4.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Scene/SceneTypes.h"

#include <functional>
#include <string>
#include <vector>

struct ImVec2;

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

        void BeginFrame();
        [[nodiscard]] EditorViewportFrameData ConsumeViewportFrameData();
        void ResetSelection() noexcept;

        void DrawSceneView(EditorProjectService& projectService, EngineRuntime& runtime);
        void DrawGameView(EditorProjectService& projectService, EngineRuntime& runtime);
        void DrawSceneHierarchy(EditorProjectService& projectService,
                                EngineRuntime& runtime,
                                std::string& statusMessage);
        void DrawInspector(EditorProjectService& projectService, EngineRuntime& runtime, std::string& statusMessage);
        void DrawAssetBrowser(EditorProjectService& projectService,
                              EngineRuntime& runtime,
                              std::string& statusMessage,
                              const OpenSceneRequest& openSceneRequest);

    private:
        enum class SelectionKind
        {
            None,
            GameObject,
            Asset,
        };

        struct SceneViewCameraState
        {
            Vector3 position = Vector3(0.0f, 2.0f, -6.0f);
            Float32 yawRadians = 0.0f;
            Float32 pitchRadians = 0.20943952f;
            Float32 moveSpeed = 4.0f;
            Float32 nearPlane = 0.05f;
            Float32 farPlane = 1000.0f;
            Vector4 clearColor = Vector4(0.13f, 0.15f, 0.18f, 1.0f);
        };

        [[nodiscard]] EditorViewportRenderRequest MakeSceneViewRequest(EditorProjectService& projectService,
                                                                       EngineRuntime& runtime,
                                                                       UInt32 width,
                                                                       UInt32 height);
        [[nodiscard]] EditorViewportRenderRequest MakeGameViewRequest(EditorProjectService& projectService,
                                                                      EngineRuntime& runtime,
                                                                      UInt32 width,
                                                                      UInt32 height);
        void DrawViewportImage(UInt64 textureId,
                               const ImVec2& imageSize,
                               const char* overlayText,
                               bool showOverlayText);
        void DrawSceneViewToolbar();
        void HandleSceneViewCameraInput(const ImVec2& imageSize, bool imageHovered);
        void SubmitGameViewInput(EngineRuntime& runtime,
                                 const ImVec2& imageMin,
                                 const ImVec2& imageMax,
                                 UInt32 width,
                                 UInt32 height,
                                 bool imageHovered);
        void SelectGameObject(SceneObjectId gameObjectId) noexcept;
        void DrawGameObjectNode(GameObject& gameObject);

        void SelectAssetRecord(const AssetRecord& record);
        [[nodiscard]] bool IsAssetSelected(const AssetRecord& record) const noexcept;
        [[nodiscard]] const AssetRecord* FindSelectedAssetRecord(const EditorProjectService& projectService) const
            noexcept;
        [[nodiscard]] Path GetAssetSelectionPath(const AssetRecord& record) const;
        void DrawSelectedAssetInspector(EditorProjectService& projectService,
                                        std::string& statusMessage,
                                        const AssetRecord& record);
        [[nodiscard]] bool DrawComponentInspector(EditorProjectService& projectService,
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
        void DrawAddComponentButton(EditorProjectService& projectService,
                                    EngineRuntime& runtime,
                                    std::string& statusMessage,
                                    GameObject& gameObject);

        void DrawAssetTree(EditorProjectService& projectService,
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

        void PrepareSceneMutation(EditorProjectService& projectService, EngineRuntime& runtime);
        void FinishSceneMutation(EditorProjectService& projectService);

        ReflectionRegistry reflectionRegistry_;
        std::vector<EditorViewportRenderRequest> viewportRequests_;
        SceneViewCameraState sceneViewCamera_;
        EditorViewportShaderMode sceneViewShaderMode_ = EditorViewportShaderMode::Shaded;
        SelectionKind selectionKind_ = SelectionKind::None;
        SceneObjectId selectedGameObjectId_ = InvalidSceneObjectId;
        Path selectedAssetPath_;
        UInt64 nextViewportFrameId_ = 1;
        bool sceneViewLookActive_ = false;
    };
} // namespace ve
