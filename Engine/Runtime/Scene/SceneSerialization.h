#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"

#include <string>
#include <string_view>

namespace ve
{
    class Scene;
    class ScriptingSystem;
    class TransformComponent;
}

namespace boost::json
{
    class object;
}

namespace ve
{
    class SceneSerialization
    {
    public:
        [[nodiscard]] static Result<std::string> SaveToString(const Scene& scene);
        [[nodiscard]] static ErrorCode LoadFromString(Scene& scene, std::string_view text, ScriptingSystem& scriptingSystem);

    private:
        [[nodiscard]] static ErrorCode ReadScene(Scene& scene, const boost::json::object& object, ScriptingSystem& scriptingSystem);
        [[nodiscard]] static ErrorCode
        ReadGameObjectRecursive(Scene& scene, TransformComponent* parent, const boost::json::object& object, ScriptingSystem& scriptingSystem);
    };
} // namespace ve
