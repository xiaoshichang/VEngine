include_guard(GLOBAL)

set(VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Managed/VEngine.ScriptHost")
set(VE_SCRIPT_HOST_MANAGED_OBJ_DIR "${CMAKE_BINARY_DIR}/ManagedObj/VEngine.ScriptHost/")
set(VE_DOTNET_RUNTIME_ROOT "" CACHE PATH "App-local .NET runtime root used by managed scripting.")
set(VE_DOTNET_SDK_EXECUTABLE "" CACHE FILEPATH "Path to the system dotnet SDK used to build managed scripting.")

function(ve_find_dotnet_sdk output_variable)
    if(VE_DOTNET_SDK_EXECUTABLE AND EXISTS "${VE_DOTNET_SDK_EXECUTABLE}")
        set(${output_variable} "${VE_DOTNET_SDK_EXECUTABLE}" PARENT_SCOPE)
        message(STATUS "dotnet SDK: ${VE_DOTNET_SDK_EXECUTABLE}")
        return()
    endif()

    set(dotnetCandidates)
    if(DEFINED ENV{DOTNET_ROOT} AND EXISTS "$ENV{DOTNET_ROOT}/dotnet")
        list(APPEND dotnetCandidates "$ENV{DOTNET_ROOT}/dotnet")
    endif()

    if(EXISTS "/usr/local/share/dotnet/dotnet")
        list(APPEND dotnetCandidates "/usr/local/share/dotnet/dotnet")
    endif()

    find_program(foundDotnet dotnet)
    if(foundDotnet)
        list(APPEND dotnetCandidates "${foundDotnet}")
    endif()

    list(REMOVE_DUPLICATES dotnetCandidates)

    foreach(candidate IN LISTS dotnetCandidates)
        if(EXISTS "${candidate}")
            set(VE_DOTNET_SDK_EXECUTABLE "${candidate}" CACHE FILEPATH "Path to the system dotnet SDK used to build managed scripting." FORCE)
            set(${output_variable} "${candidate}" PARENT_SCOPE)
            message(STATUS "dotnet SDK: ${candidate}")
            return()
        endif()
    endforeach()

    if(dotnetCandidates)
        message(FATAL_ERROR "dotnet SDK was not found at any known path: ${dotnetCandidates}. Install the system .NET SDK or set VE_DOTNET_SDK_EXECUTABLE.")
    endif()

    message(FATAL_ERROR "dotnet SDK was not found. Install the system .NET SDK or set VE_DOTNET_SDK_EXECUTABLE.")
endfunction()

function(ve_find_dotnet_runtime_root output_variable)
    if(VE_DOTNET_RUNTIME_ROOT AND EXISTS "${VE_DOTNET_RUNTIME_ROOT}/dotnet")
        set(${output_variable} "${VE_DOTNET_RUNTIME_ROOT}" PARENT_SCOPE)
        message(STATUS ".NET runtime: ${VE_DOTNET_RUNTIME_ROOT}")
        return()
    endif()

    if(APPLE)
        set(runtimeCandidates
            "${PROJECT_SOURCE_DIR}/ThirdParty/DotNet/osx-arm64/10.0.9"
        )
    elseif(WIN32)
        set(runtimeCandidates
            "${PROJECT_SOURCE_DIR}/ThirdParty/DotNet/win-x64/10.0.9"
        )
    else()
        set(runtimeCandidates)
    endif()

    foreach(candidateRoot IN LISTS runtimeCandidates)
        if(EXISTS "${candidateRoot}/dotnet")
            set(VE_DOTNET_RUNTIME_ROOT "${candidateRoot}" CACHE PATH "App-local .NET runtime root used by managed scripting." FORCE)
            set(${output_variable} "${candidateRoot}" PARENT_SCOPE)
            message(STATUS ".NET runtime: ${candidateRoot}")
            return()
        endif()
    endforeach()

    message(FATAL_ERROR "Project-local .NET runtime was not found under ThirdParty/DotNet. Run ThirdParty/DotNet/Build_Mac.sh or the Windows runtime setup first.")
endfunction()

function(ve_add_managed_script_host)
    if(TARGET VEngineScriptHostManaged)
        return()
    endif()

    if(NOT (WIN32 OR APPLE))
        return()
    endif()

    ve_find_dotnet_sdk(dotnetSdkExecutable)
    ve_find_dotnet_runtime_root(dotnetRuntimeRoot)
    message(STATUS "Managed scripting build tool: ${dotnetSdkExecutable}")
    message(STATUS "Managed scripting runtime root: ${dotnetRuntimeRoot}")

    add_custom_target(VEngineScriptHostManaged
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}"
        COMMAND "${dotnetSdkExecutable}" publish
            "${PROJECT_SOURCE_DIR}/Engine/Managed/VEngine.ScriptHost/VEngine.ScriptHost.csproj"
            --configuration $<CONFIG>
            --framework net10.0
            --output "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}"
            -p:BaseIntermediateOutputPath="${VE_SCRIPT_HOST_MANAGED_OBJ_DIR}"
            -p:PublishSingleFile=false
            --nologo
        BYPRODUCTS
            "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}/VEngine.ScriptHost.dll"
            "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}/VEngine.ScriptHost.runtimeconfig.json"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Building VEngine.ScriptHost managed assembly"
        VERBATIM
    )
endfunction()
