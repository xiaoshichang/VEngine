#include "Engine/Runtime/Scripting/ScriptBridge.h"

#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/ScriptComponent.h"
#include "Engine/Runtime/Time/Time.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

namespace ve
{
    namespace
    {
#if defined(_WIN32)
#define VE_SCRIPT_BRIDGE_IMPL_CALLTYPE __stdcall
#else
#define VE_SCRIPT_BRIDGE_IMPL_CALLTYPE
#endif

        [[nodiscard]] std::int32_t ToStatus(ScriptBridgeStatus status) noexcept
        {
            return static_cast<std::int32_t>(status);
        }

        [[nodiscard]] LogSeverity ToNativeSeverity(std::int32_t severity) noexcept
        {
            switch (static_cast<ScriptLogSeverity>(severity))
            {
            case ScriptLogSeverity::Trace:
                return LogSeverity::Trace;
            case ScriptLogSeverity::Debug:
                return LogSeverity::Debug;
            case ScriptLogSeverity::Info:
                return LogSeverity::Info;
            case ScriptLogSeverity::Warn:
                return LogSeverity::Warn;
            case ScriptLogSeverity::Error:
                return LogSeverity::Error;
            case ScriptLogSeverity::Fatal:
                return LogSeverity::Fatal;
            default:
                return LogSeverity::Info;
            }
        }

        [[nodiscard]] std::string ReadUtf8String(const char* text, std::int32_t sizeInBytes)
        {
            if (sizeInBytes <= 0 || text == nullptr)
            {
                return {};
            }

            return std::string(text, static_cast<std::size_t>(sizeInBytes));
        }

        [[nodiscard]] std::int32_t CopyUtf8String(std::string_view text, char* buffer, std::int32_t bufferSizeInBytes)
        {
            const std::int32_t requiredSize = static_cast<std::int32_t>(
                std::min<std::size_t>(text.size(), static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));

            if (buffer != nullptr && bufferSizeInBytes > 0)
            {
                const std::int32_t writableSize = std::min(requiredSize, bufferSizeInBytes - 1);
                if (writableSize > 0)
                {
                    std::memcpy(buffer, text.data(), static_cast<std::size_t>(writableSize));
                }

                buffer[writableSize] = '\0';
            }

            return requiredSize;
        }

        [[nodiscard]] ScriptBridgeRegistry* GetRegistry(void* userData) noexcept
        {
            return static_cast<ScriptBridgeRegistry*>(userData);
        }

        [[nodiscard]] ScriptComponent* ResolveComponent(void* userData, ScriptObjectHandle handle) noexcept
        {
            ScriptBridgeRegistry* registry = GetRegistry(userData);
            return registry != nullptr ? registry->ResolveComponent(handle) : nullptr;
        }

        [[nodiscard]] TransformComponent* ResolveTransform(void* userData, ScriptObjectHandle handle) noexcept
        {
            ScriptComponent* component = ResolveComponent(userData, handle);
            if (component == nullptr)
            {
                return nullptr;
            }

            return component->GetGameObject().GetComponent<TransformComponent>();
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeLog(
            void*, std::int32_t severity, const char* message, std::int32_t messageSizeInBytes)
        {
            if (message == nullptr && messageSizeInBytes > 0)
            {
                return ToStatus(ScriptBridgeStatus::InvalidArgument);
            }

            LogMessage(ToNativeSeverity(severity),
                       "Script",
                       ReadUtf8String(message, messageSizeInBytes),
                       SourceLocation::current());
            return ToStatus(ScriptBridgeStatus::Success);
        }

        double VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetTotalSeconds(void*)
        {
            return Time::GetTotalSeconds();
        }

        float VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetDeltaSeconds(void*)
        {
            return Time::GetDeltaSeconds();
        }

        std::uint64_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetFrameIndex(void*)
        {
            return Time::GetFrameIndex();
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetGameObjectName(
            void* userData, ScriptObjectHandle handle, char* buffer, std::int32_t bufferSizeInBytes)
        {
            ScriptComponent* component = ResolveComponent(userData, handle);
            if (component == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidHandle);
            }

            return CopyUtf8String(component->GetGameObject().GetName(), buffer, bufferSizeInBytes);
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeSetGameObjectName(
            void* userData, ScriptObjectHandle handle, const char* name, std::int32_t nameSizeInBytes)
        {
            if (name == nullptr && nameSizeInBytes > 0)
            {
                return ToStatus(ScriptBridgeStatus::InvalidArgument);
            }

            ScriptComponent* component = ResolveComponent(userData, handle);
            if (component == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidHandle);
            }

            component->GetGameObject().SetName(ReadUtf8String(name, nameSizeInBytes));
            return ToStatus(ScriptBridgeStatus::Success);
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetLocalPosition(
            void* userData, ScriptObjectHandle handle, ScriptVector3* outValue)
        {
            if (outValue == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidArgument);
            }

            TransformComponent* transform = ResolveTransform(userData, handle);
            if (transform == nullptr)
            {
                return ResolveComponent(userData, handle) == nullptr ? ToStatus(ScriptBridgeStatus::InvalidHandle)
                                                                     : ToStatus(ScriptBridgeStatus::MissingComponent);
            }

            const Vector3& value = transform->GetLocalPosition();
            outValue->x = value.GetX();
            outValue->y = value.GetY();
            outValue->z = value.GetZ();
            return ToStatus(ScriptBridgeStatus::Success);
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeSetLocalPosition(
            void* userData, ScriptObjectHandle handle, ScriptVector3 value)
        {
            TransformComponent* transform = ResolveTransform(userData, handle);
            if (transform == nullptr)
            {
                return ResolveComponent(userData, handle) == nullptr ? ToStatus(ScriptBridgeStatus::InvalidHandle)
                                                                     : ToStatus(ScriptBridgeStatus::MissingComponent);
            }

            transform->SetLocalPosition(Vector3(value.x, value.y, value.z));
            return ToStatus(ScriptBridgeStatus::Success);
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetLocalRotation(
            void* userData, ScriptObjectHandle handle, ScriptQuaternion* outValue)
        {
            if (outValue == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidArgument);
            }

            TransformComponent* transform = ResolveTransform(userData, handle);
            if (transform == nullptr)
            {
                return ResolveComponent(userData, handle) == nullptr ? ToStatus(ScriptBridgeStatus::InvalidHandle)
                                                                     : ToStatus(ScriptBridgeStatus::MissingComponent);
            }

            const Quaternion& value = transform->GetLocalRotation();
            outValue->x = value.GetX();
            outValue->y = value.GetY();
            outValue->z = value.GetZ();
            outValue->w = value.GetW();
            return ToStatus(ScriptBridgeStatus::Success);
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeSetLocalRotation(
            void* userData, ScriptObjectHandle handle, ScriptQuaternion value)
        {
            TransformComponent* transform = ResolveTransform(userData, handle);
            if (transform == nullptr)
            {
                return ResolveComponent(userData, handle) == nullptr ? ToStatus(ScriptBridgeStatus::InvalidHandle)
                                                                     : ToStatus(ScriptBridgeStatus::MissingComponent);
            }

            transform->SetLocalRotation(Quaternion(value.x, value.y, value.z, value.w));
            return ToStatus(ScriptBridgeStatus::Success);
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetLocalScale(
            void* userData, ScriptObjectHandle handle, ScriptVector3* outValue)
        {
            if (outValue == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidArgument);
            }

            TransformComponent* transform = ResolveTransform(userData, handle);
            if (transform == nullptr)
            {
                return ResolveComponent(userData, handle) == nullptr ? ToStatus(ScriptBridgeStatus::InvalidHandle)
                                                                     : ToStatus(ScriptBridgeStatus::MissingComponent);
            }

            const Vector3& value = transform->GetLocalScale();
            outValue->x = value.GetX();
            outValue->y = value.GetY();
            outValue->z = value.GetZ();
            return ToStatus(ScriptBridgeStatus::Success);
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeSetLocalScale(
            void* userData, ScriptObjectHandle handle, ScriptVector3 value)
        {
            TransformComponent* transform = ResolveTransform(userData, handle);
            if (transform == nullptr)
            {
                return ResolveComponent(userData, handle) == nullptr ? ToStatus(ScriptBridgeStatus::InvalidHandle)
                                                                     : ToStatus(ScriptBridgeStatus::MissingComponent);
            }

            transform->SetLocalScale(Vector3(value.x, value.y, value.z));
            return ToStatus(ScriptBridgeStatus::Success);
        }

#undef VE_SCRIPT_BRIDGE_IMPL_CALLTYPE
    } // namespace

    ScriptObjectHandle ScriptBridgeRegistry::RegisterComponent(ScriptComponent& component)
    {
        if (freeListHead_ != UINT32_MAX)
        {
            const std::uint32_t slotIndex = freeListHead_;
            Slot& slot = slots_[slotIndex];
            freeListHead_ = slot.nextFreeSlot;
            slot.component = &component;
            slot.nextFreeSlot = UINT32_MAX;
            return PackHandle(slotIndex, slot.generation);
        }

        const std::uint32_t slotIndex = static_cast<std::uint32_t>(slots_.size());
        Slot slot;
        slot.component = &component;
        slot.nextFreeSlot = UINT32_MAX;
        slots_.push_back(slot);
        return PackHandle(slotIndex, slot.generation);
    }

    void ScriptBridgeRegistry::UnregisterComponent(ScriptObjectHandle handle, const ScriptComponent& component) noexcept
    {
        std::uint32_t slotIndex = 0;
        std::uint32_t generation = 0;
        if (!UnpackHandle(handle, slotIndex, generation) || slotIndex >= slots_.size())
        {
            return;
        }

        Slot& slot = slots_[slotIndex];
        if (slot.component != &component || slot.generation != generation)
        {
            return;
        }

        slot.component = nullptr;
        ++slot.generation;
        if (slot.generation == 0)
        {
            slot.generation = 1;
        }

        slot.nextFreeSlot = freeListHead_;
        freeListHead_ = slotIndex;
    }

    ScriptComponent* ScriptBridgeRegistry::ResolveComponent(ScriptObjectHandle handle) noexcept
    {
        std::uint32_t slotIndex = 0;
        std::uint32_t generation = 0;
        if (!UnpackHandle(handle, slotIndex, generation) || slotIndex >= slots_.size())
        {
            return nullptr;
        }

        Slot& slot = slots_[slotIndex];
        return slot.generation == generation ? slot.component : nullptr;
    }

    const ScriptComponent* ScriptBridgeRegistry::ResolveComponent(ScriptObjectHandle handle) const noexcept
    {
        std::uint32_t slotIndex = 0;
        std::uint32_t generation = 0;
        if (!UnpackHandle(handle, slotIndex, generation) || slotIndex >= slots_.size())
        {
            return nullptr;
        }

        const Slot& slot = slots_[slotIndex];
        return slot.generation == generation ? slot.component : nullptr;
    }

    void ScriptBridgeRegistry::Clear() noexcept
    {
        slots_.clear();
        freeListHead_ = UINT32_MAX;
    }

    ScriptObjectHandle ScriptBridgeRegistry::PackHandle(std::uint32_t slotIndex, std::uint32_t generation) noexcept
    {
        return (static_cast<ScriptObjectHandle>(generation) << 32) | static_cast<ScriptObjectHandle>(slotIndex + 1);
    }

    bool ScriptBridgeRegistry::UnpackHandle(ScriptObjectHandle handle,
                                            std::uint32_t& outSlotIndex,
                                            std::uint32_t& outGeneration) noexcept
    {
        if (handle == InvalidScriptObjectHandle)
        {
            return false;
        }

        const std::uint32_t slotIndexPlusOne = static_cast<std::uint32_t>(handle & 0xFFFFFFFFull);
        if (slotIndexPlusOne == 0)
        {
            return false;
        }

        outSlotIndex = slotIndexPlusOne - 1;
        outGeneration = static_cast<std::uint32_t>(handle >> 32);
        return outGeneration != 0;
    }

    ScriptBridgeApi CreateScriptBridgeApi(ScriptBridgeRegistry& registry) noexcept
    {
        ScriptBridgeApi api;
        api.version = 1;
        api.size = sizeof(ScriptBridgeApi);
        api.userData = &registry;
        api.log = &BridgeLog;
        api.getTotalSeconds = &BridgeGetTotalSeconds;
        api.getDeltaSeconds = &BridgeGetDeltaSeconds;
        api.getFrameIndex = &BridgeGetFrameIndex;
        api.getGameObjectName = &BridgeGetGameObjectName;
        api.setGameObjectName = &BridgeSetGameObjectName;
        api.getLocalPosition = &BridgeGetLocalPosition;
        api.setLocalPosition = &BridgeSetLocalPosition;
        api.getLocalRotation = &BridgeGetLocalRotation;
        api.setLocalRotation = &BridgeSetLocalRotation;
        api.getLocalScale = &BridgeGetLocalScale;
        api.setLocalScale = &BridgeSetLocalScale;
        return api;
    }
} // namespace ve
