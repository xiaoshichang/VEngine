#include "Engine/Runtime/Scripting/ScriptBridge.h"

#include "Engine/Runtime/Input/InputSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Physics/PhysicsWorld.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/RenderComponents.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/ScriptComponent.h"
#include "Engine/Runtime/Time/Time.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
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
            ScriptBridgeContext* context = static_cast<ScriptBridgeContext*>(userData);
            return context != nullptr ? context->registry : nullptr;
        }

        [[nodiscard]] InputSystem* GetInputSystem(void* userData) noexcept
        {
            ScriptBridgeContext* context = static_cast<ScriptBridgeContext*>(userData);
            return context != nullptr ? context->inputSystem : nullptr;
        }

        [[nodiscard]] Scene* GetScene(void* userData) noexcept
        {
            ScriptBridgeContext* context = static_cast<ScriptBridgeContext*>(userData);
            return context != nullptr ? context->scene : nullptr;
        }

        [[nodiscard]] ScriptComponent* ResolveComponent(void* userData, ScriptObjectHandle handle) noexcept
        {
            ScriptBridgeRegistry* registry = GetRegistry(userData);
            return registry != nullptr ? registry->ResolveComponent(handle) : nullptr;
        }

        [[nodiscard]] GameObject* ResolveGameObject(void* userData, ScriptObjectHandle handle) noexcept
        {
            if (ScriptComponent* component = ResolveComponent(userData, handle))
            {
                return &component->GetGameObject();
            }

            Scene* scene = GetScene(userData);
            return scene != nullptr ? scene->FindGameObject(static_cast<SceneObjectId>(handle)) : nullptr;
        }

        [[nodiscard]] TransformComponent* ResolveTransform(void* userData, ScriptObjectHandle handle) noexcept
        {
            GameObject* gameObject = ResolveGameObject(userData, handle);
            if (gameObject == nullptr)
            {
                return nullptr;
            }

            return gameObject->GetComponent<TransformComponent>();
        }

        [[nodiscard]] ScriptVector2 ToScriptVector2(const Vector2& value) noexcept
        {
            return ScriptVector2{value.GetX(), value.GetY()};
        }

        [[nodiscard]] ScriptVector3 ToScriptVector3(const Vector3& value) noexcept
        {
            return ScriptVector3{value.GetX(), value.GetY(), value.GetZ()};
        }

        [[nodiscard]] Vector3 ToVector3(const ScriptVector3& value) noexcept
        {
            return Vector3(value.x, value.y, value.z);
        }

        [[nodiscard]] Ray ToRay(const ScriptRay& ray) noexcept
        {
            return Ray(ToVector3(ray.origin), ToVector3(ray.direction));
        }

        [[nodiscard]] bool VisitActiveGameObject(GameObject& gameObject,
                                                  const std::function<bool(GameObject&)>& visitor)
        {
            if (!gameObject.IsActiveInHierarchy())
            {
                return false;
            }

            if (visitor(gameObject))
            {
                return true;
            }

            for (GameObject* child : gameObject.GetChildren())
            {
                if (child != nullptr && VisitActiveGameObject(*child, visitor))
                {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]] CameraComponent* FindMainCamera(Scene& scene, GameObject** outGameObject = nullptr)
        {
            CameraComponent* result = nullptr;
            for (GameObject* root : scene.GetRootGameObjects())
            {
                if (root == nullptr)
                {
                    continue;
                }

                if (VisitActiveGameObject(
                    *root,
                    [&result, outGameObject](GameObject& gameObject)
                    {
                        if (CameraComponent* camera = gameObject.GetComponent<CameraComponent>())
                        {
                            result = camera;
                            if (outGameObject != nullptr)
                            {
                                *outGameObject = &gameObject;
                            }
                            return true;
                        }

                        return false;
                    }))
                {
                    return result;
                }
            }

            return nullptr;
        }

        [[nodiscard]] Ray BuildCameraScreenRay(CameraComponent& camera,
                                               const Vector2& screenPoint,
                                               UInt32 viewportWidth,
                                               UInt32 viewportHeight) noexcept
        {
            const TransformComponent* transform = camera.GetGameObject().GetComponent<TransformComponent>();
            const Vector3 cameraPosition = transform != nullptr ? transform->GetWorldPosition() : Vector3::Zero();
            const Quaternion cameraRotation = transform != nullptr ? transform->GetLocalRotation()
                                                                   : Quaternion::Identity();
            const Float32 width = std::max(static_cast<Float32>(viewportWidth), 1.0f);
            const Float32 height = std::max(static_cast<Float32>(viewportHeight), 1.0f);
            const Float32 normalizedX = (screenPoint.GetX() / width) * 2.0f - 1.0f;
            const Float32 normalizedY = 1.0f - (screenPoint.GetY() / height) * 2.0f;
            const Float32 aspectRatio = width / height;

            if (camera.GetProjectionMode() == CameraProjectionMode::Orthographic)
            {
                const Float32 viewHeight = camera.GetOrthographicSize();
                const Float32 viewWidth = viewHeight * aspectRatio;
                const Vector3 right = cameraRotation.RotateVector(Vector3::UnitX()).Normalized();
                const Vector3 up = cameraRotation.RotateVector(Vector3::UnitY()).Normalized();
                const Vector3 forward = cameraRotation.RotateVector(Vector3::UnitZ()).Normalized();
                const Vector3 origin = cameraPosition + (right * normalizedX * viewWidth * 0.5f) +
                                       (up * normalizedY * viewHeight * 0.5f);
                return Ray(origin, forward);
            }

            const Float32 tanHalfFov = std::tan(camera.GetFieldOfViewRadians() * 0.5f);
            const Vector3 viewDirection(normalizedX * aspectRatio * tanHalfFov, normalizedY * tanHalfFov, 1.0f);
            return Ray(cameraPosition, cameraRotation.RotateVector(viewDirection).Normalized());
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
            GameObject* gameObject = ResolveGameObject(userData, handle);
            if (gameObject == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidHandle);
            }

            return CopyUtf8String(gameObject->GetName(), buffer, bufferSizeInBytes);
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeSetGameObjectName(
            void* userData, ScriptObjectHandle handle, const char* name, std::int32_t nameSizeInBytes)
        {
            if (name == nullptr && nameSizeInBytes > 0)
            {
                return ToStatus(ScriptBridgeStatus::InvalidArgument);
            }

            GameObject* gameObject = ResolveGameObject(userData, handle);
            if (gameObject == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidHandle);
            }

            gameObject->SetName(ReadUtf8String(name, nameSizeInBytes));
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

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetKey(void* userData, std::int32_t keyCode)
        {
            InputSystem* inputSystem = GetInputSystem(userData);
            return inputSystem != nullptr && inputSystem->GetKey(static_cast<KeyCode>(keyCode)) ? 1 : 0;
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetKeyDown(void* userData, std::int32_t keyCode)
        {
            InputSystem* inputSystem = GetInputSystem(userData);
            return inputSystem != nullptr && inputSystem->GetKeyDown(static_cast<KeyCode>(keyCode)) ? 1 : 0;
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetKeyUp(void* userData, std::int32_t keyCode)
        {
            InputSystem* inputSystem = GetInputSystem(userData);
            return inputSystem != nullptr && inputSystem->GetKeyUp(static_cast<KeyCode>(keyCode)) ? 1 : 0;
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetMouseButton(void* userData, std::int32_t mouseButton)
        {
            InputSystem* inputSystem = GetInputSystem(userData);
            return inputSystem != nullptr && inputSystem->GetMouseButton(static_cast<MouseButton>(mouseButton)) ? 1
                                                                                                               : 0;
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetMouseButtonDown(void* userData, std::int32_t mouseButton)
        {
            InputSystem* inputSystem = GetInputSystem(userData);
            return inputSystem != nullptr && inputSystem->GetMouseButtonDown(static_cast<MouseButton>(mouseButton))
                       ? 1
                       : 0;
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetMouseButtonUp(void* userData, std::int32_t mouseButton)
        {
            InputSystem* inputSystem = GetInputSystem(userData);
            return inputSystem != nullptr && inputSystem->GetMouseButtonUp(static_cast<MouseButton>(mouseButton)) ? 1
                                                                                                                  : 0;
        }

        ScriptVector2 VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetMousePosition(void* userData)
        {
            InputSystem* inputSystem = GetInputSystem(userData);
            return inputSystem != nullptr ? ToScriptVector2(inputSystem->GetMousePosition()) : ScriptVector2{};
        }

        ScriptVector2 VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetMouseDelta(void* userData)
        {
            InputSystem* inputSystem = GetInputSystem(userData);
            return inputSystem != nullptr ? ToScriptVector2(inputSystem->GetMouseDelta()) : ScriptVector2{};
        }

        float VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetScrollDelta(void* userData)
        {
            InputSystem* inputSystem = GetInputSystem(userData);
            return inputSystem != nullptr ? inputSystem->GetScrollDelta() : 0.0f;
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeGetMainCamera(void* userData, ScriptObjectHandle* outHandle)
        {
            if (outHandle == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidArgument);
            }

            *outHandle = InvalidScriptObjectHandle;
            Scene* scene = GetScene(userData);
            if (scene == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidHandle);
            }

            GameObject* cameraObject = nullptr;
            if (FindMainCamera(*scene, &cameraObject) == nullptr || cameraObject == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::MissingComponent);
            }

            *outHandle = cameraObject->GetId();
            return ToStatus(ScriptBridgeStatus::Success);
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeScreenPointToRay(
            void* userData, ScriptObjectHandle handle, ScriptVector2 screenPoint, ScriptRay* outRay)
        {
            if (outRay == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidArgument);
            }

            GameObject* gameObject = ResolveGameObject(userData, handle);
            if (gameObject == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidHandle);
            }

            CameraComponent* camera = gameObject->GetComponent<CameraComponent>();
            if (camera == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::MissingComponent);
            }

            InputSystem* inputSystem = GetInputSystem(userData);
            const UInt32 viewportWidth = inputSystem != nullptr ? inputSystem->GetViewportWidth() : 1u;
            const UInt32 viewportHeight = inputSystem != nullptr ? inputSystem->GetViewportHeight() : 1u;
            const Ray ray =
                BuildCameraScreenRay(*camera, Vector2(screenPoint.x, screenPoint.y), viewportWidth, viewportHeight);
            outRay->origin = ToScriptVector3(ray.origin);
            outRay->direction = ToScriptVector3(ray.GetNormalizedDirection());
            return ToStatus(ScriptBridgeStatus::Success);
        }

        std::int32_t VE_SCRIPT_BRIDGE_IMPL_CALLTYPE BridgeRaycast(
            void* userData, ScriptRay ray, ScriptRaycastHit* outHit, std::uint64_t queryMask, bool includeTriggers)
        {
            if (outHit == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidArgument);
            }

            *outHit = ScriptRaycastHit{};
            Scene* scene = GetScene(userData);
            if (scene == nullptr)
            {
                return ToStatus(ScriptBridgeStatus::InvalidHandle);
            }

            scene->UpdateTransforms();
            PhysicsWorld world;
            world.SyncFromScene(*scene);
            const std::optional<RaycastHit> hit = world.RaycastClosest(ToRay(ray), queryMask, includeTriggers);
            if (!hit)
            {
                return 0;
            }

            outHit->gameObjectHandle = hit->gameObjectId;
            outHit->distance = hit->distance;
            outHit->position = ToScriptVector3(hit->position);
            outHit->normal = ToScriptVector3(hit->normal);
            return 1;
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

    ScriptBridgeApi CreateScriptBridgeApi(ScriptBridgeContext& context) noexcept
    {
        ScriptBridgeApi api;
        api.version = 2;
        api.size = sizeof(ScriptBridgeApi);
        api.userData = &context;
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
        api.getKey = &BridgeGetKey;
        api.getKeyDown = &BridgeGetKeyDown;
        api.getKeyUp = &BridgeGetKeyUp;
        api.getMouseButton = &BridgeGetMouseButton;
        api.getMouseButtonDown = &BridgeGetMouseButtonDown;
        api.getMouseButtonUp = &BridgeGetMouseButtonUp;
        api.getMousePosition = &BridgeGetMousePosition;
        api.getMouseDelta = &BridgeGetMouseDelta;
        api.getScrollDelta = &BridgeGetScrollDelta;
        api.getMainCamera = &BridgeGetMainCamera;
        api.screenPointToRay = &BridgeScreenPointToRay;
        api.raycast = &BridgeRaycast;
        return api;
    }
} // namespace ve
