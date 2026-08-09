#include "Engine/Runtime/Render/RenderShaderResources.h"

#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Resource/AssetRecord.h"
#include "Engine/Runtime/Resource/ResourceObject.h"
#include "Engine/Runtime/Resource/ResourceSystem.h"

#include <string>
#include <utility>

namespace ve
{
    namespace
    {
        constexpr const char* VirtualShadowPath = "Builtin/Shaders/VirtualShadow.veshader.json";
        constexpr const char* FrameGraphDebugPreviewPath = "Builtin/Shaders/FrameGraphDebugPreview.veshader.json";
        constexpr const char* ShadowCasterDirtyDebugPath = "Builtin/Shaders/ShadowCasterDirtyDebug.veshader.json";
        constexpr const char* VirtualShadowRedrawPageDebugPath = "Builtin/Shaders/VirtualShadowRedrawPageDebug.veshader.json";
        constexpr const char* SceneGridPath = "Editor/Shaders/RenderPasses/SceneGrid.veshader.json";
        constexpr const char* EditorGizmoLinePath = "Editor/Shaders/RenderPasses/EditorGizmoLine.veshader.json";
        constexpr const char* EditorGizmoIconPath = "Editor/Shaders/RenderPasses/EditorGizmoIcon.veshader.json";

        [[nodiscard]] Error LoadShader(ResourceSystem& resourceSystem,
                                       const IAssetRecordProvider& assetProvider,
                                       RenderSystem& renderSystem,
                                       const char* runtimePath,
                                       AssetRef<ShaderResource>& assetRef,
                                       std::shared_ptr<RTShaderResource>& renderResource)
        {
            Result<AssetRef<ShaderResource>> result = resourceSystem.Request<ShaderResource>(Path(runtimePath), assetProvider);
            if (!result)
            {
                return Error(result.GetError().GetCode(),
                             "Failed to load render shader resource '" + std::string(runtimePath) + "': " + result.GetError().GetMessage());
            }

            assetRef = result.MoveValue();
            resourceSystem.EnsureRenderResource(assetRef, renderSystem);
            renderResource = assetRef.Get()->GetRTShaderResource();
            if (renderResource == nullptr)
            {
                return Error(ErrorCode::InvalidState, "Render shader resource has no render-thread proxy: " + std::string(runtimePath));
            }

            return Error();
        }
    } // namespace

    RenderShaderResourceLibrary::~RenderShaderResourceLibrary()
    {
        Shutdown();
    }

    Error RenderShaderResourceLibrary::Initialize(const RenderShaderResourceLibraryInitParam& initParam)
    {
        if (IsInitialized())
        {
            return Error(ErrorCode::InvalidState, "RenderShaderResourceLibrary is already initialized.");
        }

        auto resources = std::make_shared<RenderShaderResources>();
        const auto loadShader = [&initParam](const char* runtimePath,
                                             AssetRef<ShaderResource>& assetRef,
                                             std::shared_ptr<RTShaderResource>& renderResource)
        {
            return LoadShader(initParam.resourceSystem, initParam.assetProvider, initParam.renderSystem, runtimePath, assetRef, renderResource);
        };

        Error result = loadShader(VirtualShadowPath, virtualShadow_, resources->virtualShadow);
        if (result.IsOk() && initParam.includeDebugShaders) result = loadShader(FrameGraphDebugPreviewPath, frameGraphDebugPreview_, resources->frameGraphDebugPreview);
        if (result.IsOk() && initParam.includeDebugShaders) result = loadShader(ShadowCasterDirtyDebugPath, shadowCasterDirtyDebug_, resources->shadowCasterDirtyDebug);
        if (result.IsOk() && initParam.includeDebugShaders) result = loadShader(VirtualShadowRedrawPageDebugPath, virtualShadowRedrawPageDebug_, resources->virtualShadowRedrawPageDebug);
        if (result.IsOk() && initParam.includeEditorShaders) result = loadShader(SceneGridPath, sceneGrid_, resources->sceneGrid);
        if (result.IsOk() && initParam.includeEditorShaders) result = loadShader(EditorGizmoLinePath, editorGizmoLine_, resources->editorGizmoLine);
        if (result.IsOk() && initParam.includeEditorShaders) result = loadShader(EditorGizmoIconPath, editorGizmoIcon_, resources->editorGizmoIcon);
        if (!result.IsOk())
        {
            Shutdown();
            return result;
        }

        resources_ = std::move(resources);
        return Error();
    }

    void RenderShaderResourceLibrary::Shutdown() noexcept
    {
        resources_.reset();
        editorGizmoIcon_.Reset();
        editorGizmoLine_.Reset();
        sceneGrid_.Reset();
        virtualShadowRedrawPageDebug_.Reset();
        shadowCasterDirtyDebug_.Reset();
        frameGraphDebugPreview_.Reset();
        virtualShadow_.Reset();
    }

    bool RenderShaderResourceLibrary::IsInitialized() const noexcept
    {
        return resources_ != nullptr;
    }

    std::shared_ptr<const RenderShaderResources> RenderShaderResourceLibrary::GetResources() const noexcept
    {
        return resources_;
    }
} // namespace ve
