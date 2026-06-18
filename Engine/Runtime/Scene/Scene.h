#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Resource/AssetID.h"
#include "Engine/Runtime/Scene/GameObject.h"

#include <functional>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace ve
{
    class SceneSystem;
    class ResourceSystem;

    /// Owns a tree of root GameObjects and updates that hierarchy.
    class Scene final : public NonMovable
    {
    public:
        Scene();
        explicit Scene(std::string name);
        ~Scene();

        [[nodiscard]] const std::string& GetName() const noexcept;
        void SetName(std::string name);

        [[nodiscard]] SizeT GetRootGameObjectCount() const noexcept;
        [[nodiscard]] GameObject* GetRootGameObject(SizeT index) noexcept;
        [[nodiscard]] const GameObject* GetRootGameObject(SizeT index) const noexcept;

        [[nodiscard]] Result<GameObject*> CreateRootGameObject(std::string name = {});
        [[nodiscard]] bool DestroyRootGameObject(GameObject& gameObject) noexcept;
        void Clear() noexcept;

        void SetSceneSystem(SceneSystem* sceneSystem) noexcept;
        [[nodiscard]] std::shared_ptr<RTScene> GetRTScene() noexcept;
        [[nodiscard]] std::shared_ptr<const RTScene> GetRTScene() const noexcept;

        void RegisterRenderItem(std::shared_ptr<RTRenderItem> item);
        void UnregisterRenderItem(std::shared_ptr<RTRenderItem> item) noexcept;
        void UpdateRenderItem(std::shared_ptr<RTRenderItem> item, RTRenderItemDesc desc);
        void RegisterCamera(std::shared_ptr<RTCamera> camera);
        void UnregisterCamera(std::shared_ptr<RTCamera> camera) noexcept;
        void UpdateCamera(std::shared_ptr<RTCamera> camera, RTCameraDesc desc);
        void RegisterLight(std::shared_ptr<RTLight> light);
        void UnregisterLight(std::shared_ptr<RTLight> light) noexcept;
        void UpdateLight(std::shared_ptr<RTLight> light, RTLightDesc desc);
        [[nodiscard]] Result<GameObject*> CreateRootGameObjectWithoutRenderRegistration(std::string name = {});

        void Update(Float32 deltaSeconds);
        void LateUpdate(Float32 deltaSeconds);
        void BeforeRender();
        void RebuildRenderThreadScene();
        void RetainAsset(AssetID id);
        void ClearRetainedAssets(ResourceSystem& resourceSystem) noexcept;
        [[nodiscard]] const std::vector<AssetID>& GetRetainedAssets() const noexcept;

    private:
        void RegisterRenderItemsRecursive(GameObject& gameObject);
        void SyncRenderItemsBeforeRenderRecursive(GameObject& gameObject);
        void SubmitRTSceneCommand(std::string debugName, std::function<void()> function) const;

        std::string name_;
        SceneSystem* sceneSystem_ = nullptr;
        std::shared_ptr<RTScene> rtScene_;
        std::vector<std::unique_ptr<GameObject>> rootGameObjects_;
        std::vector<AssetID> retainedAssets_;
    };
} // namespace ve
