#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Resource/AssetRef.h"

#include <memory>

namespace ve
{
    class IAssetRecordProvider;
    class RenderSystem;
    class ResourceSystem;
    class RTShaderResource;
    class ShaderResource;

    struct RenderShaderResources
    {
        std::shared_ptr<RTShaderResource> virtualShadow;
        std::shared_ptr<RTShaderResource> frameGraphDebugPreview;
        std::shared_ptr<RTShaderResource> shadowCasterDirtyDebug;
        std::shared_ptr<RTShaderResource> virtualShadowRedrawPageDebug;
        std::shared_ptr<RTShaderResource> sceneGrid;
        std::shared_ptr<RTShaderResource> editorGizmoLine;
        std::shared_ptr<RTShaderResource> editorGizmoIcon;
    };

    struct RenderShaderResourceLibraryInitParam
    {
        ResourceSystem& resourceSystem;
        const IAssetRecordProvider& assetProvider;
        RenderSystem& renderSystem;
        bool includeEditorShaders = false;
        bool includeDebugShaders = false;
    };

    /// Owns the AssetRefs that keep engine render-pass shaders loaded while frames use their RT resources.
    class RenderShaderResourceLibrary final : public NonMovable
    {
    public:
        RenderShaderResourceLibrary() = default;
        ~RenderShaderResourceLibrary();

        [[nodiscard]] Error Initialize(const RenderShaderResourceLibraryInitParam& initParam);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] std::shared_ptr<const RenderShaderResources> GetResources() const noexcept;

    private:
        AssetRef<ShaderResource> virtualShadow_;
        AssetRef<ShaderResource> frameGraphDebugPreview_;
        AssetRef<ShaderResource> shadowCasterDirtyDebug_;
        AssetRef<ShaderResource> virtualShadowRedrawPageDebug_;
        AssetRef<ShaderResource> sceneGrid_;
        AssetRef<ShaderResource> editorGizmoLine_;
        AssetRef<ShaderResource> editorGizmoIcon_;
        std::shared_ptr<RenderShaderResources> resources_;
    };
} // namespace ve
