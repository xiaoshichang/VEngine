include_guard(GLOBAL)

set(VE_ENGINE_SCRIPT_HOST_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Managed/VEngine.ScriptHost")
set(VE_ENGINE_SCRIPT_HOST_OBJ_DIR "${CMAKE_BINARY_DIR}/ManagedObj/VEngine.ScriptHost/")
set(VE_DOTNET_RUNTIME_ROOT "" CACHE PATH "App-local .NET runtime root used by the engine script host.")
set(VE_DOTNET_SDK_EXECUTABLE "" CACHE FILEPATH "Path to the system dotnet SDK used to build the engine script host.")

function(ve_find_dotnet_sdk output_variable)
    if(VE_DOTNET_SDK_EXECUTABLE AND EXISTS "${VE_DOTNET_SDK_EXECUTABLE}")
        set(${output_variable} "${VE_DOTNET_SDK_EXECUTABLE}" PARENT_SCOPE)
        message(STATUS "dotnet SDK: ${VE_DOTNET_SDK_EXECUTABLE}")
        return()
    endif()

    find_program(foundDotnet
        NAMES dotnet dotnet.exe
        HINTS
            "$ENV{DOTNET_ROOT}"
            "/usr/local/share/dotnet"
        DOC "Path to the system dotnet SDK used to build the engine script host."
    )
    if(foundDotnet)
        set(VE_DOTNET_SDK_EXECUTABLE "${foundDotnet}" CACHE FILEPATH "Path to the system dotnet SDK used to build the engine script host." FORCE)
        set(${output_variable} "${foundDotnet}" PARENT_SCOPE)
        message(STATUS "dotnet SDK: ${foundDotnet}")
        return()
    endif()

    message(FATAL_ERROR "dotnet SDK was not found. Install the system .NET SDK or set VE_DOTNET_SDK_EXECUTABLE.")
endfunction()

function(ve_find_dotnet_runtime_root output_variable)
    if(APPLE)
        set(dotnetRuntimeRoot "${PROJECT_SOURCE_DIR}/ThirdParty/DotNet/osx-arm64/10.0.9")
        set(dotnetRuntimeExecutable "dotnet")
    elseif(WIN32)
        set(dotnetRuntimeRoot "${PROJECT_SOURCE_DIR}/ThirdParty/DotNet/win-x64/10.0.9")
        set(dotnetRuntimeExecutable "dotnet.exe")
    else()
        message(FATAL_ERROR "Project-local .NET runtime is not configured for this platform.")
    endif()

    if(NOT EXISTS "${dotnetRuntimeRoot}/${dotnetRuntimeExecutable}" AND COMMAND ve_run_third_party_setup)
        ve_run_third_party_setup(dotnet)
    endif()

    if(NOT EXISTS "${dotnetRuntimeRoot}/${dotnetRuntimeExecutable}")
        message(FATAL_ERROR "Project-local .NET runtime was not found: ${dotnetRuntimeRoot}. CMake could not prepare DotNet. Run ThirdParty/DotNet/Build_Mac.sh or the Windows runtime setup first.")
    endif()

    set(VE_DOTNET_RUNTIME_ROOT "${dotnetRuntimeRoot}" CACHE PATH "App-local .NET runtime root used by the engine script host." FORCE)
    set(${output_variable} "${dotnetRuntimeRoot}" PARENT_SCOPE)
    message(STATUS ".NET runtime: ${dotnetRuntimeRoot}")
endfunction()

function(ve_add_engine_script_host)
    if(TARGET VEngineEngineScriptHost)
        return()
    endif()

    if(NOT (WIN32 OR APPLE))
        return()
    endif()

    ve_find_dotnet_sdk(dotnetSdkExecutable)
    ve_find_dotnet_runtime_root(dotnetRuntimeRoot)
    ve_add_third_party_marker_target(VEngineThirdPartyDotNet)
    message(STATUS "Engine script host build tool: ${dotnetSdkExecutable}")
    message(STATUS "Engine script host runtime root: ${dotnetRuntimeRoot}")

    add_custom_target(VEngineEngineScriptHost
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${VE_ENGINE_SCRIPT_HOST_OUTPUT_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VE_ENGINE_SCRIPT_HOST_OUTPUT_DIR}"
        COMMAND "${dotnetSdkExecutable}" publish
            "${PROJECT_SOURCE_DIR}/Engine/Managed/VEngine.ScriptHost/VEngine.ScriptHost.csproj"
            --configuration $<CONFIG>
            --framework net10.0
            --output "${VE_ENGINE_SCRIPT_HOST_OUTPUT_DIR}"
            -p:BaseIntermediateOutputPath="${VE_ENGINE_SCRIPT_HOST_OBJ_DIR}"
            -p:AssemblyName=VEngine.ScriptHost
            -p:TargetName=VEngine.ScriptHost
            -p:PublishSingleFile=false
            --nologo
        BYPRODUCTS
            "${VE_ENGINE_SCRIPT_HOST_OUTPUT_DIR}/VEngine.ScriptHost.dll"
            "${VE_ENGINE_SCRIPT_HOST_OUTPUT_DIR}/VEngine.ScriptHost.runtimeconfig.json"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Building VEngine.ScriptHost engine script host assembly"
        VERBATIM
    )
endfunction()
