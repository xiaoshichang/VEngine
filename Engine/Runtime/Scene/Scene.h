#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Scene/GameObject.h"

#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace ve
{
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

    void Update(Float32 deltaSeconds);

private:
    std::string name_;
    std::vector<std::unique_ptr<GameObject>> rootGameObjects_;
};
}
