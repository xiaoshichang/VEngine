#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"

#include <cstdint>
#include <vector>

namespace ve
{
    class ScriptComponent;

#if defined(_WIN32)
#define VE_SCRIPT_BRIDGE_CALLTYPE __stdcall
#else
#define VE_SCRIPT_BRIDGE_CALLTYPE
#endif

    using ScriptObjectHandle = std::uint64_t;

    inline constexpr ScriptObjectHandle InvalidScriptObjectHandle = 0;

    enum class ScriptBridgeStatus : std::int32_t
    {
        Success = 0,
        InvalidHandle = -1,
        MissingComponent = -2,
        InvalidArgument = -3,
    };

    enum class ScriptLogSeverity : std::int32_t
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Fatal = 5,
    };

    struct ScriptVector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct ScriptQuaternion
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    struct ScriptBridgeApi
    {
        std::int32_t version = 1;
        std::int32_t size = 0;
        void* userData = nullptr;

        std::int32_t(VE_SCRIPT_BRIDGE_CALLTYPE* log)(
            void* userData, std::int32_t severity, const char* message, std::int32_t messageSizeInBytes) = nullptr;
        double(VE_SCRIPT_BRIDGE_CALLTYPE* getTotalSeconds)(void* userData) = nullptr;
        float(VE_SCRIPT_BRIDGE_CALLTYPE* getDeltaSeconds)(void* userData) = nullptr;
        std::uint64_t(VE_SCRIPT_BRIDGE_CALLTYPE* getFrameIndex)(void* userData) = nullptr;

        std::int32_t(VE_SCRIPT_BRIDGE_CALLTYPE* getGameObjectName)(
            void* userData, ScriptObjectHandle handle, char* buffer, std::int32_t bufferSizeInBytes) = nullptr;
        std::int32_t(VE_SCRIPT_BRIDGE_CALLTYPE* setGameObjectName)(
            void* userData, ScriptObjectHandle handle, const char* name, std::int32_t nameSizeInBytes) = nullptr;

        std::int32_t(VE_SCRIPT_BRIDGE_CALLTYPE* getLocalPosition)(
            void* userData, ScriptObjectHandle handle, ScriptVector3* outValue) = nullptr;
        std::int32_t(VE_SCRIPT_BRIDGE_CALLTYPE* setLocalPosition)(
            void* userData, ScriptObjectHandle handle, ScriptVector3 value) = nullptr;
        std::int32_t(VE_SCRIPT_BRIDGE_CALLTYPE* getLocalRotation)(
            void* userData, ScriptObjectHandle handle, ScriptQuaternion* outValue) = nullptr;
        std::int32_t(VE_SCRIPT_BRIDGE_CALLTYPE* setLocalRotation)(
            void* userData, ScriptObjectHandle handle, ScriptQuaternion value) = nullptr;
        std::int32_t(VE_SCRIPT_BRIDGE_CALLTYPE* getLocalScale)(
            void* userData, ScriptObjectHandle handle, ScriptVector3* outValue) = nullptr;
        std::int32_t(VE_SCRIPT_BRIDGE_CALLTYPE* setLocalScale)(
            void* userData, ScriptObjectHandle handle, ScriptVector3 value) = nullptr;
    };

    class ScriptBridgeRegistry : public NonMovable
    {
    public:
        ScriptBridgeRegistry() = default;
        ~ScriptBridgeRegistry() = default;

        [[nodiscard]] ScriptObjectHandle RegisterComponent(ScriptComponent& component);
        void UnregisterComponent(ScriptObjectHandle handle, const ScriptComponent& component) noexcept;
        [[nodiscard]] ScriptComponent* ResolveComponent(ScriptObjectHandle handle) noexcept;
        [[nodiscard]] const ScriptComponent* ResolveComponent(ScriptObjectHandle handle) const noexcept;
        void Clear() noexcept;

    private:
        struct Slot
        {
            ScriptComponent* component = nullptr;
            std::uint32_t generation = 1;
            std::uint32_t nextFreeSlot = 0;
        };

        [[nodiscard]] static ScriptObjectHandle PackHandle(std::uint32_t slotIndex, std::uint32_t generation) noexcept;
        [[nodiscard]] static bool UnpackHandle(ScriptObjectHandle handle,
                                               std::uint32_t& outSlotIndex,
                                               std::uint32_t& outGeneration) noexcept;

        std::vector<Slot> slots_;
        std::uint32_t freeListHead_ = UINT32_MAX;
    };

    [[nodiscard]] ScriptBridgeApi CreateScriptBridgeApi(ScriptBridgeRegistry& registry) noexcept;

#undef VE_SCRIPT_BRIDGE_CALLTYPE
} // namespace ve
