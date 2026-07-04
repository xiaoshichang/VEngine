#include "Engine/Runtime/Scripting/NativeAOTScriptBridgeSymbols.h"

#include "Engine/Runtime/Core/Platform.h"

#if VE_PLATFORM_IOS
#include <dlfcn.h>
#endif

namespace ve
{
    namespace
    {
#if VE_PLATFORM_IOS
        template<typename T>
        [[nodiscard]] T LoadNativeAOTSymbol(const char* name) noexcept
        {
            return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name));
        }
#endif
    } // namespace

    bool HasCompleteNativeAOTScriptBridge(const ManagedScriptEntryPoints& entryPoints) noexcept
    {
        return entryPoints.registerNativeApi != nullptr && entryPoints.loadProjectAssembly != nullptr && entryPoints.unloadProjectAssembly != nullptr &&
               entryPoints.getScriptTypesJson != nullptr && entryPoints.freeString != nullptr && entryPoints.create != nullptr &&
               entryPoints.destroy != nullptr && entryPoints.createEvent != nullptr && entryPoints.update != nullptr &&
               entryPoints.lateUpdate != nullptr && entryPoints.enable != nullptr && entryPoints.disable != nullptr &&
               entryPoints.getFieldsJson != nullptr && entryPoints.setFieldsJson != nullptr && entryPoints.setFieldJson != nullptr;
    }

    ManagedScriptEntryPoints CreateLinkedNativeAOTScriptEntryPoints() noexcept
    {
        ManagedScriptEntryPoints entryPoints;

#if VE_PLATFORM_IOS
        entryPoints.registerNativeApi = LoadNativeAOTSymbol<ManagedScriptEntryPoints::RegisterNativeApiFn>("VEngine_RegisterNativeApi");
        entryPoints.loadProjectAssembly = LoadNativeAOTSymbol<ManagedScriptEntryPoints::LoadProjectAssemblyFn>("VEngine_LoadProjectAssembly");
        entryPoints.unloadProjectAssembly = LoadNativeAOTSymbol<ManagedScriptEntryPoints::UnloadProjectAssemblyFn>("VEngine_UnloadProjectAssembly");
        entryPoints.getScriptTypesJson = LoadNativeAOTSymbol<ManagedScriptEntryPoints::GetScriptTypesJsonFn>("VEngine_GetScriptTypesJson");
        entryPoints.freeString = LoadNativeAOTSymbol<ManagedScriptEntryPoints::FreeStringFn>("VEngine_FreeString");
        entryPoints.create = LoadNativeAOTSymbol<ManagedScriptEntryPoints::CreateFn>("VEngine_CreateScript");
        entryPoints.destroy = LoadNativeAOTSymbol<ManagedScriptEntryPoints::DestroyFn>("VEngine_DestroyScript");
        entryPoints.createEvent = LoadNativeAOTSymbol<ManagedScriptEntryPoints::EventFn>("VEngine_OnCreate");
        entryPoints.update = LoadNativeAOTSymbol<ManagedScriptEntryPoints::UpdateFn>("VEngine_OnUpdate");
        entryPoints.lateUpdate = LoadNativeAOTSymbol<ManagedScriptEntryPoints::UpdateFn>("VEngine_OnLateUpdate");
        entryPoints.enable = LoadNativeAOTSymbol<ManagedScriptEntryPoints::EventFn>("VEngine_OnEnable");
        entryPoints.disable = LoadNativeAOTSymbol<ManagedScriptEntryPoints::EventFn>("VEngine_OnDisable");
        entryPoints.getFieldsJson = LoadNativeAOTSymbol<ManagedScriptEntryPoints::GetFieldsJsonFn>("VEngine_GetScriptFieldsJson");
        entryPoints.setFieldsJson = LoadNativeAOTSymbol<ManagedScriptEntryPoints::SetFieldsJsonFn>("VEngine_SetScriptFieldsJson");
        entryPoints.setFieldJson = LoadNativeAOTSymbol<ManagedScriptEntryPoints::SetFieldJsonFn>("VEngine_SetScriptFieldJson");

        if (!HasCompleteNativeAOTScriptBridge(entryPoints))
        {
            return {};
        }
#endif

        return entryPoints;
    }

    bool HasLinkedNativeAOTScriptBridge() noexcept
    {
        return HasCompleteNativeAOTScriptBridge(CreateLinkedNativeAOTScriptEntryPoints());
    }
} // namespace ve
