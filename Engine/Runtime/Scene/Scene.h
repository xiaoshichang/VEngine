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
    class SceneSerialization;
    class CameraComponent;

    enum class SceneExecutionMode
    {
        Editing,
        Runtime,
    };

    /// Owns a tree of root GameObjects and updates that hierarchy.
    class Scene final : public NonMovable
    {
    public:
        Scene(SceneSystem& sceneSystem, SceneExecutionMode executionMode);
        Scene(SceneSystem& sceneSystem, SceneExecutionMode executionMode, std::string name);
        ~Scene();

        [[nodiscard]] const std::string& GetName() const noexcept;
        void SetName(std::string name);

        [[nodiscard]] SizeT GetRootGameObjectCount() const noexcept;
        [[nodiscard]] GameObject* GetRootGameObject(SizeT index) noexcept;
        [[nodiscard]] const GameObject* GetRootGameObject(SizeT index) const noexcept;
        [[nodiscard]] CameraComponent* GetMainCamera() noexcept;
        [[nodiscard]] const CameraComponent* GetMainCamera() const noexcept;

        [[nodiscard]] bool DestroyRootGameObject(GameObject& gameObject) noexcept;
        void Clear() noexcept;

        [[nodiscard]] SceneSystem* GetSceneSystem() noexcept;
        [[nodiscard]] const SceneSystem* GetSceneSystem() const noexcept;
        [[nodiscard]] std::shared_ptr<RTScene> GetRTScene() noexcept;
        [[nodiscard]] std::shared_ptr<const RTScene> GetRTScene() const noexcept;
        [[nodiscard]] SceneExecutionMode GetExecutionMode() const noexcept;

        void RegisterRenderItem(std::shared_ptr<RTRenderItem> item);
        void UnregisterRenderItem(std::shared_ptr<RTRenderItem> item) noexcept;
        void UpdateRenderItem(std::shared_ptr<RTRenderItem> item, RTRenderItemUpdateParam updateParam);
        void RegisterCamera(std::shared_ptr<RTCamera> camera);
        void UnregisterCamera(std::shared_ptr<RTCamera> camera) noexcept;
        void UpdateCamera(std::shared_ptr<RTCamera> camera, RTCameraUpdateParam updateParam);
        void RegisterLight(std::shared_ptr<RTLight> light);
        void UnregisterLight(std::shared_ptr<RTLight> light) noexcept;
        void UpdateLight(std::shared_ptr<RTLight> light, RTLightUpdateParam updateParam);

        void Update(Float32 deltaSeconds);
        void LateUpdate(Float32 deltaSeconds);
        void BeforeRender();
        void OnLoad();

        [[nodiscard]] Result<GameObject*> CreateGameObject(std::string name = {}, GameObject* parent = nullptr);

    private:
        friend class SceneSystem;
        friend class SceneSerialization;
        friend class Component;
        friend class GameObject;

        [[nodiscard]] bool ShouldDispatchLifecycleCallbacks() const noexcept;
        [[nodiscard]] Result<GameObject*> CreateRootGameObject(std::string name = {});
        void RebuildRenderThreadScene();
        void RegisterRenderItemsRecursive(GameObject& gameObject);
        void SyncRenderItemsBeforeRenderRecursive(GameObject& gameObject);
        void SubmitRTSceneCommand(std::string debugName, std::function<void()> function) const;
        [[nodiscard]] static CameraComponent* FindMainCameraRecursive(GameObject& gameObject) noexcept;
        [[nodiscard]] static const CameraComponent* FindMainCameraRecursive(const GameObject& gameObject) noexcept;

        std::string name_;
        SceneSystem& sceneSystem_;
        SceneExecutionMode executionMode_ = SceneExecutionMode::Runtime;
        bool loaded_ = false;
        std::shared_ptr<RTScene> rtScene_;
        std::vector<std::unique_ptr<GameObject>> rootGameObjects_;
    };
} // namespace ve
