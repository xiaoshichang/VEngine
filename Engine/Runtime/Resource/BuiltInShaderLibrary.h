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

    struct BuiltInShaderResources
    {
        std::shared_ptr<RTShaderResource> virtualShadow;
        std::shared_ptr<RTShaderResource> frameGraphDebugPreview;
        std::shared_ptr<RTShaderResource> shadowCasterDirtyDebug;
        std::shared_ptr<RTShaderResource> virtualShadowRedrawPageDebug;
        std::shared_ptr<RTShaderResource> sceneGrid;
        std::shared_ptr<RTShaderResource> editorGizmoLine;
        std::shared_ptr<RTShaderResource> editorGizmoIcon;
        std::shared_ptr<RTShaderResource> pbrDirect;
        std::shared_ptr<RTShaderResource> hdrToneMapping;
    };

    enum class BuiltInShaderEnvironment
    {
        Player,
        Editor,
    };

    struct BuiltInShaderLibraryInitParam
    {
        const IAssetRecordProvider& assetProvider;
        RenderSystem& renderSystem;
        BuiltInShaderEnvironment environment = BuiltInShaderEnvironment::Player;
        bool includeDebugShaders = false;
    };

    /// ResourceSystem-owned library that keeps builtin ShaderResources alive for the engine lifetime.
    class BuiltInShaderLibrary final : public NonMovable
    {
    public:
        BuiltInShaderLibrary() = default;
        ~BuiltInShaderLibrary();

        [[nodiscard]] Error Initialize(ResourceSystem& resourceSystem, const BuiltInShaderLibraryInitParam& initParam);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] std::shared_ptr<const BuiltInShaderResources> GetResources() const noexcept;

    private:
        AssetRef<ShaderResource> virtualShadow_;
        AssetRef<ShaderResource> frameGraphDebugPreview_;
        AssetRef<ShaderResource> shadowCasterDirtyDebug_;
        AssetRef<ShaderResource> virtualShadowRedrawPageDebug_;
        AssetRef<ShaderResource> sceneGrid_;
        AssetRef<ShaderResource> editorGizmoLine_;
        AssetRef<ShaderResource> editorGizmoIcon_;
        AssetRef<ShaderResource> pbrDirect_;
        AssetRef<ShaderResource> hdrToneMapping_;
        std::shared_ptr<BuiltInShaderResources> resources_;
    };
} // namespace ve
