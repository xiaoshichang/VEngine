#include "Engine/Runtime/Scripting/NativeAOTScriptBridgeSymbols.h"

#include "Engine/Runtime/Core/Platform.h"

#if VE_PLATFORM_IOS
#define VE_NATIVEAOT_WEAK_IMPORT __attribute__((weak_import))

extern "C"
{
    void VEngine_RegisterNativeApi(const ve::NativeScriptApi* nativeApi) VE_NATIVEAOT_WEAK_IMPORT;
    int VEngine_LoadProjectAssembly(const char* assemblyPath) VE_NATIVEAOT_WEAK_IMPORT;
    void VEngine_UnloadProjectAssembly() VE_NATIVEAOT_WEAK_IMPORT;
    const char* VEngine_GetScriptTypesJson() VE_NATIVEAOT_WEAK_IMPORT;
    void VEngine_FreeString(const char* text) VE_NATIVEAOT_WEAK_IMPORT;
    ve::ScriptInstanceHandle VEngine_CreateScript(void* nativeComponent, const char* scriptTypeName, ve::Int32 invokeOnCreate)
        VE_NATIVEAOT_WEAK_IMPORT;
    void VEngine_DestroyScript(ve::ScriptInstanceHandle script) VE_NATIVEAOT_WEAK_IMPORT;
    void VEngine_OnCreate(ve::ScriptInstanceHandle script) VE_NATIVEAOT_WEAK_IMPORT;
    void VEngine_OnUpdate(ve::ScriptInstanceHandle script, ve::Float32 deltaSeconds) VE_NATIVEAOT_WEAK_IMPORT;
    void VEngine_OnLateUpdate(ve::ScriptInstanceHandle script, ve::Float32 deltaSeconds) VE_NATIVEAOT_WEAK_IMPORT;
    void VEngine_OnEnable(ve::ScriptInstanceHandle script) VE_NATIVEAOT_WEAK_IMPORT;
    void VEngine_OnDisable(ve::ScriptInstanceHandle script) VE_NATIVEAOT_WEAK_IMPORT;
    const char* VEngine_GetScriptFieldsJson(ve::ScriptInstanceHandle script) VE_NATIVEAOT_WEAK_IMPORT;
    ve::Int32 VEngine_SetScriptFieldsJson(ve::ScriptInstanceHandle script, const char* fieldsJson) VE_NATIVEAOT_WEAK_IMPORT;
    ve::Int32 VEngine_SetScriptFieldJson(ve::ScriptInstanceHandle script, const char* fieldName, const char* valueJson)
        VE_NATIVEAOT_WEAK_IMPORT;
}

#undef VE_NATIVEAOT_WEAK_IMPORT
#endif

namespace ve
{
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
        entryPoints.registerNativeApi = &VEngine_RegisterNativeApi;
        entryPoints.loadProjectAssembly = &VEngine_LoadProjectAssembly;
        entryPoints.unloadProjectAssembly = &VEngine_UnloadProjectAssembly;
        entryPoints.getScriptTypesJson = &VEngine_GetScriptTypesJson;
        entryPoints.freeString = &VEngine_FreeString;
        entryPoints.create = &VEngine_CreateScript;
        entryPoints.destroy = &VEngine_DestroyScript;
        entryPoints.createEvent = &VEngine_OnCreate;
        entryPoints.update = &VEngine_OnUpdate;
        entryPoints.lateUpdate = &VEngine_OnLateUpdate;
        entryPoints.enable = &VEngine_OnEnable;
        entryPoints.disable = &VEngine_OnDisable;
        entryPoints.getFieldsJson = &VEngine_GetScriptFieldsJson;
        entryPoints.setFieldsJson = &VEngine_SetScriptFieldsJson;
        entryPoints.setFieldJson = &VEngine_SetScriptFieldJson;

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
