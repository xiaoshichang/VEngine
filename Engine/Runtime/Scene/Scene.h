#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderScene.h"
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

        void Update(Float32 deltaSeconds);

    private:
        void RebuildRTScene();
        void RegisterRenderItemsRecursive(GameObject& gameObject);
        void SubmitRTSceneCommand(std::string debugName, std::function<void()> function) const;

        std::string name_;
        SceneSystem* sceneSystem_ = nullptr;
        std::shared_ptr<RTScene> rtScene_;
        std::vector<std::unique_ptr<GameObject>> rootGameObjects_;
    };
} // namespace ve
