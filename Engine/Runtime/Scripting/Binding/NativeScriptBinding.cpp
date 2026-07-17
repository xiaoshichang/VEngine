#include "Engine/Runtime/Scripting/Binding/NativeScriptBinding.h"

#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/LightComponent.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/ScriptableComponent.h"

#include <filesystem>
#include <string>

#if VE_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#if VE_PLATFORM_WINDOWS
#define VE_HOSTFXR_TEXT(value) L##value
#else
#define VE_HOSTFXR_TEXT(value) value
#endif

namespace ve
{
    namespace
    {
#if VE_PLATFORM_WINDOWS || VE_PLATFORM_MACOS
#if VE_PLATFORM_WINDOWS
        using CharT = wchar_t;
#else
        using CharT = char;
#endif
        using LoadAssemblyAndGetFunctionPointerFn = int (*)(const CharT* assemblyPath,
                                                            const CharT* typeName,
                                                            const CharT* methodName,
                                                            const CharT* delegateTypeName,
                                                            void* reserved,
                                                            void** delegate);

        [[nodiscard]] const CharT* GetUnmanagedCallersOnlyMethod() noexcept
        {
            return reinterpret_cast<const CharT*>(-1);
        }

#if VE_PLATFORM_WINDOWS
        [[nodiscard]] std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int requiredSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (requiredSize <= 0)
            {
                return {};
            }

            std::wstring result(static_cast<size_t>(requiredSize), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), requiredSize);
            return result;
        }

        [[nodiscard]] std::filesystem::path ToNativePath(const Path& path)
        {
            return std::filesystem::path(Utf8ToWide(path.GetString()));
        }
#else
        [[nodiscard]] std::filesystem::path ToNativePath(const Path& path)
        {
            return std::filesystem::path(path.GetString());
        }
#endif

        [[nodiscard]] std::basic_string<CharT> ToHostFxrString(std::string_view text)
        {
#if VE_PLATFORM_WINDOWS
            return Utf8ToWide(text);
#else
            return std::string(text);
#endif
        }

        [[nodiscard]] std::basic_string<CharT> ToHostFxrPathString(const std::filesystem::path& path)
        {
#if VE_PLATFORM_WINDOWS
            return path.wstring();
#else
            return path.string();
#endif
        }

        template<typename TDelegate>
        [[nodiscard]] ErrorCode LoadEntryPoint(LoadAssemblyAndGetFunctionPointerFn loadFunction,
                                               const std::basic_string<CharT>& assemblyPath,
                                               const std::basic_string<CharT>& typeName,
                                               const CharT* methodName,
                                               TDelegate& output)
        {
            void* function = nullptr;
            const int result = loadFunction(assemblyPath.c_str(), typeName.c_str(), methodName, GetUnmanagedCallersOnlyMethod(), nullptr, &function);
            if (result != 0 || function == nullptr)
            {
                return ErrorCode::InvalidState;
            }

            output = reinterpret_cast<TDelegate>(function);
            return ErrorCode::None;
        }
#endif

        template<typename TComponent>
        [[nodiscard]] TComponent* GetBoundComponent(void* nativeComponent)
        {
            auto* scriptComponent = static_cast<ScriptableComponent*>(nativeComponent);
            GameObject* owner = scriptComponent != nullptr ? scriptComponent->GetOwner() : nullptr;
            return owner != nullptr ? owner->GetComponent<TComponent>() : nullptr;
        }

        void NativeGetTransformLocalPosition(void* nativeComponent, Float32* x, Float32* y, Float32* z)
        {
            if (x != nullptr)
            {
                *x = 0.0f;
            }
            if (y != nullptr)
            {
                *y = 0.0f;
            }
            if (z != nullptr)
            {
                *z = 0.0f;
            }

            TransformComponent* transform = GetBoundComponent<TransformComponent>(nativeComponent);
            if (transform == nullptr)
            {
                return;
            }

            const Vector3& position = transform->GetLocalPosition();
            if (x != nullptr)
            {
                *x = position.GetX();
            }
            if (y != nullptr)
            {
                *y = position.GetY();
            }
            if (z != nullptr)
            {
                *z = position.GetZ();
            }
        }

        void NativeSetTransformLocalPosition(void* nativeComponent, Float32 x, Float32 y, Float32 z)
        {
            TransformComponent* transform = GetBoundComponent<TransformComponent>(nativeComponent);
            if (transform == nullptr)
            {
                return;
            }

            transform->SetLocalPosition(Vector3(x, y, z));
        }

        Int32 NativeHasCamera(void* nativeComponent)
        {
            return GetBoundComponent<CameraComponent>(nativeComponent) != nullptr ? 1 : 0;
        }

        Int32 NativeGetCameraProjectionMode(void* nativeComponent)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            return camera != nullptr ? static_cast<Int32>(camera->GetProjectionMode()) : 0;
        }

        void NativeSetCameraProjectionMode(void* nativeComponent, Int32 projectionMode)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            if (camera == nullptr)
            {
                return;
            }

            camera->SetProjectionMode(projectionMode == static_cast<Int32>(CameraComponent::ProjectionMode::Orthographic)
                                          ? CameraComponent::ProjectionMode::Orthographic
                                          : CameraComponent::ProjectionMode::Perspective);
        }

        Float32 NativeGetCameraVerticalFieldOfViewRadians(void* nativeComponent)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            return camera != nullptr ? camera->GetVerticalFieldOfViewRadians() : 0.0f;
        }

        void NativeSetCameraVerticalFieldOfViewRadians(void* nativeComponent, Float32 fieldOfViewRadians)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            if (camera != nullptr)
            {
                camera->SetVerticalFieldOfViewRadians(fieldOfViewRadians);
            }
        }

        Float32 NativeGetCameraOrthographicSize(void* nativeComponent)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            return camera != nullptr ? camera->GetOrthographicSize() : 0.0f;
        }

        void NativeSetCameraOrthographicSize(void* nativeComponent, Float32 orthographicSize)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            if (camera != nullptr)
            {
                camera->SetOrthographicSize(orthographicSize);
            }
        }

        Float32 NativeGetCameraAspectRatio(void* nativeComponent)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            return camera != nullptr ? camera->GetAspectRatio() : 0.0f;
        }

        void NativeSetCameraAspectRatio(void* nativeComponent, Float32 aspectRatio)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            if (camera != nullptr)
            {
                camera->SetAspectRatio(aspectRatio);
            }
        }

        Int32 NativeIsCameraAspectRatioAutomatic(void* nativeComponent)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            return camera != nullptr && camera->IsAspectRatioAutomatic() ? 1 : 0;
        }

        void NativeResetCameraAspectRatio(void* nativeComponent)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            if (camera != nullptr)
            {
                camera->ResetAspectRatio();
            }
        }

        Float32 NativeGetCameraNearClipPlane(void* nativeComponent)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            return camera != nullptr ? camera->GetNearClipPlane() : 0.0f;
        }

        void NativeSetCameraNearClipPlane(void* nativeComponent, Float32 nearClipPlane)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            if (camera != nullptr)
            {
                camera->SetNearClipPlane(nearClipPlane);
            }
        }

        Float32 NativeGetCameraFarClipPlane(void* nativeComponent)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            return camera != nullptr ? camera->GetFarClipPlane() : 0.0f;
        }

        void NativeSetCameraFarClipPlane(void* nativeComponent, Float32 farClipPlane)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            if (camera != nullptr)
            {
                camera->SetFarClipPlane(farClipPlane);
            }
        }

        void NativeGetCameraClearColor(void* nativeComponent, Float32* r, Float32* g, Float32* b, Float32* a)
        {
            if (r != nullptr)
            {
                *r = 0.0f;
            }
            if (g != nullptr)
            {
                *g = 0.0f;
            }
            if (b != nullptr)
            {
                *b = 0.0f;
            }
            if (a != nullptr)
            {
                *a = 1.0f;
            }

            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            if (camera == nullptr)
            {
                return;
            }

            const rhi::RhiColor& color = camera->GetClearColor();
            if (r != nullptr)
            {
                *r = color.r;
            }
            if (g != nullptr)
            {
                *g = color.g;
            }
            if (b != nullptr)
            {
                *b = color.b;
            }
            if (a != nullptr)
            {
                *a = color.a;
            }
        }

        void NativeSetCameraClearColor(void* nativeComponent, Float32 r, Float32 g, Float32 b, Float32 a)
        {
            CameraComponent* camera = GetBoundComponent<CameraComponent>(nativeComponent);
            if (camera != nullptr)
            {
                camera->SetClearColor(rhi::RhiColor{r, g, b, a});
            }
        }

        Int32 NativeHasLight(void* nativeComponent)
        {
            return GetBoundComponent<LightComponent>(nativeComponent) != nullptr ? 1 : 0;
        }

        Int32 NativeGetLightType(void* nativeComponent)
        {
            LightComponent* light = GetBoundComponent<LightComponent>(nativeComponent);
            return light != nullptr ? static_cast<Int32>(light->GetLightType()) : 0;
        }

        void NativeSetLightType(void* nativeComponent, Int32 lightType)
        {
            LightComponent* light = GetBoundComponent<LightComponent>(nativeComponent);
            if (light == nullptr)
            {
                return;
            }

            light->SetLightType(lightType == static_cast<Int32>(LightType::Point) ? LightType::Point : LightType::Directional);
        }

        void NativeGetLightColor(void* nativeComponent, Float32* r, Float32* g, Float32* b)
        {
            if (r != nullptr)
            {
                *r = 0.0f;
            }
            if (g != nullptr)
            {
                *g = 0.0f;
            }
            if (b != nullptr)
            {
                *b = 0.0f;
            }

            LightComponent* light = GetBoundComponent<LightComponent>(nativeComponent);
            if (light == nullptr)
            {
                return;
            }

            const Vector3& color = light->GetColor();
            if (r != nullptr)
            {
                *r = color.GetX();
            }
            if (g != nullptr)
            {
                *g = color.GetY();
            }
            if (b != nullptr)
            {
                *b = color.GetZ();
            }
        }

        void NativeSetLightColor(void* nativeComponent, Float32 r, Float32 g, Float32 b)
        {
            LightComponent* light = GetBoundComponent<LightComponent>(nativeComponent);
            if (light != nullptr)
            {
                light->SetColor(Vector3(r, g, b));
            }
        }

        Float32 NativeGetLightIntensity(void* nativeComponent)
        {
            LightComponent* light = GetBoundComponent<LightComponent>(nativeComponent);
            return light != nullptr ? light->GetIntensity() : 0.0f;
        }

        void NativeSetLightIntensity(void* nativeComponent, Float32 intensity)
        {
            LightComponent* light = GetBoundComponent<LightComponent>(nativeComponent);
            if (light != nullptr)
            {
                light->SetIntensity(intensity);
            }
        }

        Float32 NativeGetLightRange(void* nativeComponent)
        {
            LightComponent* light = GetBoundComponent<LightComponent>(nativeComponent);
            return light != nullptr ? light->GetRange() : 0.0f;
        }

        void NativeSetLightRange(void* nativeComponent, Float32 range)
        {
            LightComponent* light = GetBoundComponent<LightComponent>(nativeComponent);
            if (light != nullptr)
            {
                light->SetRange(range);
            }
        }

        Int32 NativeGetLightCastShadows(void* nativeComponent)
        {
            LightComponent* light = GetBoundComponent<LightComponent>(nativeComponent);
            return light != nullptr && light->CastShadows() ? 1 : 0;
        }

        void NativeSetLightCastShadows(void* nativeComponent, Int32 castShadows)
        {
            LightComponent* light = GetBoundComponent<LightComponent>(nativeComponent);
            if (light != nullptr)
            {
                light->SetCastShadows(castShadows != 0);
            }
        }

        void NativeLogInfo(const char* text)
        {
            VE_LOG_INFO_CATEGORY("Script", "{}", text != nullptr ? text : "");
        }
    } // namespace

    ErrorCode LoadManagedEntryPoints(const ScriptingAssemblyLoadDesc& desc,
                                     const ManagedScriptBindingInitParam& initParam,
                                     ManagedScriptEntryPoints& entryPoints)
    {
#if !(VE_PLATFORM_WINDOWS || VE_PLATFORM_MACOS)
        static_cast<void>(desc);
        static_cast<void>(initParam);
        static_cast<void>(entryPoints);
        return ErrorCode::Unsupported;
#else
        if (initParam.loadAssemblyAndGetFunctionPointer == nullptr || initParam.runtimeConfigPath.empty())
        {
            return ErrorCode::InvalidState;
        }

        const std::filesystem::path assemblyPath = ToNativePath(desc.assemblyPath);
        std::error_code error;
        if (!std::filesystem::exists(assemblyPath, error))
        {
            return ErrorCode::NotFound;
        }

        const std::basic_string<CharT> assemblyPathText = ToHostFxrPathString(assemblyPath);
        const std::basic_string<CharT> bridgeTypeName = ToHostFxrString(desc.bridgeTypeName);
        if (bridgeTypeName.empty())
        {
            return ErrorCode::InvalidArgument;
        }

        auto loadFunction = reinterpret_cast<LoadAssemblyAndGetFunctionPointerFn>(initParam.loadAssemblyAndGetFunctionPointer);
        ManagedScriptEntryPoints loadedEntryPoints;

        ErrorCode result =
            LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("RegisterNativeApiExport"), loadedEntryPoints.registerNativeApi);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("LoadProjectAssemblyExport"),
                                loadedEntryPoints.loadProjectAssembly);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("UnloadProjectAssemblyExport"),
                                loadedEntryPoints.unloadProjectAssembly);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("GetScriptTypesJsonExport"),
                                loadedEntryPoints.getScriptTypesJson);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("FreeStringExport"), loadedEntryPoints.freeString);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("CreateScriptExport"), loadedEntryPoints.create);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("DestroyScriptExport"), loadedEntryPoints.destroy);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("OnCreateExport"), loadedEntryPoints.createEvent);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("GetScriptFieldsJsonExport"),
                                loadedEntryPoints.getFieldsJson);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("SetScriptFieldsJsonExport"),
                                loadedEntryPoints.setFieldsJson);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("SetScriptFieldJsonExport"),
                                loadedEntryPoints.setFieldJson);
        if (result != ErrorCode::None)
        {
            return result;
        }

        static_cast<void>(LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("OnUpdateExport"), loadedEntryPoints.update));
        static_cast<void>(LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("OnLateUpdateExport"),
                                         loadedEntryPoints.lateUpdate));
        static_cast<void>(LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("OnEnableExport"), loadedEntryPoints.enable));
        static_cast<void>(LoadEntryPoint(loadFunction, assemblyPathText, bridgeTypeName, VE_HOSTFXR_TEXT("OnDisableExport"),
                                         loadedEntryPoints.disable));

        entryPoints = loadedEntryPoints;
        return ErrorCode::None;
#endif
    }

    void RegisterNativeScriptApi(const ManagedScriptEntryPoints& entryPoints)
    {
        if (entryPoints.registerNativeApi == nullptr)
        {
            return;
        }

        const NativeScriptApi nativeApi{
            reinterpret_cast<void*>(&NativeGetTransformLocalPosition),
            reinterpret_cast<void*>(&NativeSetTransformLocalPosition),
            reinterpret_cast<void*>(&NativeHasCamera),
            reinterpret_cast<void*>(&NativeGetCameraProjectionMode),
            reinterpret_cast<void*>(&NativeSetCameraProjectionMode),
            reinterpret_cast<void*>(&NativeGetCameraVerticalFieldOfViewRadians),
            reinterpret_cast<void*>(&NativeSetCameraVerticalFieldOfViewRadians),
            reinterpret_cast<void*>(&NativeGetCameraOrthographicSize),
            reinterpret_cast<void*>(&NativeSetCameraOrthographicSize),
            reinterpret_cast<void*>(&NativeGetCameraAspectRatio),
            reinterpret_cast<void*>(&NativeSetCameraAspectRatio),
            reinterpret_cast<void*>(&NativeIsCameraAspectRatioAutomatic),
            reinterpret_cast<void*>(&NativeResetCameraAspectRatio),
            reinterpret_cast<void*>(&NativeGetCameraNearClipPlane),
            reinterpret_cast<void*>(&NativeSetCameraNearClipPlane),
            reinterpret_cast<void*>(&NativeGetCameraFarClipPlane),
            reinterpret_cast<void*>(&NativeSetCameraFarClipPlane),
            reinterpret_cast<void*>(&NativeGetCameraClearColor),
            reinterpret_cast<void*>(&NativeSetCameraClearColor),
            reinterpret_cast<void*>(&NativeHasLight),
            reinterpret_cast<void*>(&NativeGetLightType),
            reinterpret_cast<void*>(&NativeSetLightType),
            reinterpret_cast<void*>(&NativeGetLightColor),
            reinterpret_cast<void*>(&NativeSetLightColor),
            reinterpret_cast<void*>(&NativeGetLightIntensity),
            reinterpret_cast<void*>(&NativeSetLightIntensity),
            reinterpret_cast<void*>(&NativeGetLightRange),
            reinterpret_cast<void*>(&NativeSetLightRange),
            reinterpret_cast<void*>(&NativeGetLightCastShadows),
            reinterpret_cast<void*>(&NativeSetLightCastShadows),
            reinterpret_cast<void*>(&NativeLogInfo),
        };

        entryPoints.registerNativeApi(&nativeApi);
    }
} // namespace ve
