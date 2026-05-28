#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/SceneTypes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ve
{
    class GameThreadSystem;
    class ScriptContext;

    struct SceneDesc
    {
        GameThreadSystem* gameThreadSystem = nullptr;
        ScriptContext* scriptContext = nullptr;
    };

    class Scene : public NonMovable
    {
    public:
        explicit Scene(const SceneDesc& desc = {});
        ~Scene();

        [[nodiscard]] GameObject& CreateGameObject(std::string name = {});
        [[nodiscard]] GameObject& CreateGameObjectWithId(SceneObjectId id, std::string name = {});
        bool DestroyGameObject(GameObject& gameObject) noexcept;
        void Clear() noexcept;

        [[nodiscard]] GameObject* FindGameObject(SceneObjectId id) noexcept;
        [[nodiscard]] const GameObject* FindGameObject(SceneObjectId id) const noexcept;

        [[nodiscard]] const std::vector<GameObject*>& GetRootGameObjects() const noexcept;
        [[nodiscard]] SizeT GetGameObjectCount() const noexcept;

        void Update();
        void LateUpdate();
        void UpdateTransforms();

        void ValidateMutationAccess() const noexcept;
        [[nodiscard]] ScriptContext* GetScriptContext() noexcept;
        [[nodiscard]] const ScriptContext* GetScriptContext() const noexcept;
        void SetScriptContext(ScriptContext* scriptContext);

    private:
        friend class GameObject;

        [[nodiscard]] SceneObjectId AllocateObjectId() noexcept;
        void RegisterGameObject(GameObject& gameObject);
        void UnregisterGameObject(GameObject& gameObject) noexcept;
        void AddRootGameObject(GameObject& gameObject);
        void RemoveRootGameObject(GameObject& gameObject) noexcept;
        void DestroyGameObjectTree(GameObject& gameObject) noexcept;
        void UpdateGameObject(GameObject& gameObject);
        void LateUpdateGameObject(GameObject& gameObject);
        void UpdateTransformTree(GameObject& gameObject, const Matrix44& parentWorld);

        GameThreadSystem* gameThreadSystem_ = nullptr;
        ScriptContext* scriptContext_ = nullptr;
        SceneObjectId nextObjectId_ = 1;
        std::vector<std::unique_ptr<GameObject>> gameObjects_;
        std::vector<GameObject*> rootGameObjects_;
        std::unordered_map<SceneObjectId, GameObject*> gameObjectLookup_;
    };
} // namespace ve
