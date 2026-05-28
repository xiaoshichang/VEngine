include_guard(GLOBAL)

set(VE_DOTNET_ROOT "" CACHE PATH "Root directory of the .NET installation used for VEngine scripting")

function(ve_get_dotnet_host_rid outVariable)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(${outVariable} "win-arm64" PARENT_SCOPE)
        return()
    endif()

    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM")
        set(${outVariable} "win-arm" PARENT_SCOPE)
        return()
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(${outVariable} "win-x64" PARENT_SCOPE)
        return()
    endif()

    set(${outVariable} "win-x86" PARENT_SCOPE)
endfunction()

function(ve_resolve_dotnet_root outVariable)
    if(VE_DOTNET_ROOT)
        file(TO_CMAKE_PATH "${VE_DOTNET_ROOT}" resolvedRoot)
        set(${outVariable} "${resolvedRoot}" PARENT_SCOPE)
        return()
    endif()

    if(DEFINED ENV{DOTNET_ROOT} AND NOT "$ENV{DOTNET_ROOT}" STREQUAL "")
        file(TO_CMAKE_PATH "$ENV{DOTNET_ROOT}" resolvedRoot)
        set(${outVariable} "${resolvedRoot}" PARENT_SCOPE)
        return()
    endif()

    if(WIN32 AND EXISTS "C:/Program Files/dotnet/dotnet.exe")
        set(${outVariable} "C:/Program Files/dotnet" PARENT_SCOPE)
        return()
    endif()

    find_program(foundDotNet dotnet)
    if(foundDotNet)
        get_filename_component(dotnetBinDir "${foundDotNet}" DIRECTORY)
        set(${outVariable} "${dotnetBinDir}" PARENT_SCOPE)
        return()
    endif()

    set(${outVariable} "" PARENT_SCOPE)
endfunction()

function(ve_setup_dotnet_hosting targetName)
    if(NOT TARGET ${targetName})
        message(FATAL_ERROR "ve_setup_dotnet_hosting target does not exist: ${targetName}")
    endif()

    if(NOT WIN32)
        return()
    endif()

    ve_resolve_dotnet_root(dotnetRoot)
    if(NOT dotnetRoot)
        message(FATAL_ERROR "VEngine scripting requires .NET on Windows. Install the .NET SDK or set VE_DOTNET_ROOT.")
    endif()

    find_program(veDotNetExecutable
        NAMES dotnet.exe dotnet
        HINTS "${dotnetRoot}"
        NO_DEFAULT_PATH
    )
    if(NOT veDotNetExecutable)
        find_program(veDotNetExecutable NAMES dotnet.exe dotnet)
    endif()
    if(NOT veDotNetExecutable)
        message(FATAL_ERROR "VEngine scripting requires the dotnet command. Set VE_DOTNET_ROOT or update PATH.")
    endif()

    execute_process(
        COMMAND "${veDotNetExecutable}" --version
        OUTPUT_VARIABLE dotnetSdkVersion
        ERROR_VARIABLE dotnetSdkError
        RESULT_VARIABLE dotnetSdkResult
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE
    )
    if(NOT dotnetSdkResult EQUAL 0)
        message(FATAL_ERROR "dotnet --version failed: ${dotnetSdkError}")
    endif()

    ve_get_dotnet_host_rid(dotnetHostRid)
    set(dotnetHostPackRoot "${dotnetRoot}/packs/Microsoft.NETCore.App.Host.${dotnetHostRid}")
    if(NOT EXISTS "${dotnetHostPackRoot}")
        message(FATAL_ERROR
            "VEngine scripting requires Microsoft.NETCore.App.Host.${dotnetHostRid} under ${dotnetRoot}/packs.")
    endif()

    file(GLOB dotnetHostPackVersions
        LIST_DIRECTORIES true
        RELATIVE "${dotnetHostPackRoot}"
        "${dotnetHostPackRoot}/*"
    )
    if(NOT dotnetHostPackVersions)
        message(FATAL_ERROR "No .NET host pack versions found under ${dotnetHostPackRoot}.")
    endif()

    list(SORT dotnetHostPackVersions COMPARE NATURAL ORDER DESCENDING)
    list(GET dotnetHostPackVersions 0 dotnetHostPackVersion)
    set(dotnetNativeHostDir "${dotnetHostPackRoot}/${dotnetHostPackVersion}/runtimes/${dotnetHostRid}/native")

    set(requiredDotNetHostingFiles
        "${dotnetNativeHostDir}/nethost.h"
        "${dotnetNativeHostDir}/hostfxr.h"
        "${dotnetNativeHostDir}/coreclr_delegates.h"
        "${dotnetNativeHostDir}/nethost.lib"
    )
    foreach(requiredFile IN LISTS requiredDotNetHostingFiles)
        if(NOT EXISTS "${requiredFile}")
            message(FATAL_ERROR "Missing .NET native hosting file: ${requiredFile}")
        endif()
    endforeach()

    if(NOT TARGET VEngine::DotNetHosting)
        add_library(VEngineDotNetHosting INTERFACE)
        add_library(VEngine::DotNetHosting ALIAS VEngineDotNetHosting)

        target_include_directories(VEngineDotNetHosting
            INTERFACE
                "${dotnetNativeHostDir}"
        )

        target_link_libraries(VEngineDotNetHosting
            INTERFACE
                "${dotnetNativeHostDir}/nethost.lib"
        )

        set(VE_DOTNET_EXECUTABLE
            "${veDotNetExecutable}"
            CACHE FILEPATH
            "dotnet executable used by VEngine scripting"
            FORCE
        )
        set(VE_DOTNET_ROOT
            "${dotnetRoot}"
            CACHE PATH
            "Root directory of the .NET installation used for VEngine scripting"
            FORCE
        )
        set(VE_DOTNET_HOST_RID "${dotnetHostRid}" CACHE STRING "Runtime identifier for .NET native hosting" FORCE)
        set(VE_DOTNET_HOST_PACK_VERSION "${dotnetHostPackVersion}" CACHE STRING "Version of the .NET host pack" FORCE)

        message(STATUS "VEngine scripting uses .NET SDK ${dotnetSdkVersion}, "
                       "target framework ${VE_DOTNET_TARGET_FRAMEWORK}, "
                       "host pack ${dotnetHostRid}/${dotnetHostPackVersion}.")
    endif()

    target_link_libraries(${targetName}
        PUBLIC
            VEngine::DotNetHosting
    )

    target_compile_definitions(${targetName}
        PUBLIC
            VE_DOTNET_TARGET_FRAMEWORK="${VE_DOTNET_TARGET_FRAMEWORK}"
    )
endfunction()

function(ve_add_managed_scripting_targets)
    if(NOT WIN32)
        return()
    endif()

    if(NOT VE_DOTNET_EXECUTABLE)
        message(FATAL_ERROR "ve_add_managed_scripting_targets requires ve_setup_dotnet_hosting to run first.")
    endif()

    if(TARGET VEngineScriptAPI)
        return()
    endif()

    set(scriptApiProject "${PROJECT_SOURCE_DIR}/Managed/VEngine.ScriptAPI/VEngine.ScriptAPI.csproj")
    if(NOT EXISTS "${scriptApiProject}")
        message(FATAL_ERROR "Missing managed scripting project: ${scriptApiProject}")
    endif()

    set(scriptApiIntermediateOutput
        "${PROJECT_BINARY_DIR}/Generated/Intermediates/Managed/VEngine.ScriptAPI/$<CONFIG>/"
    )

    add_custom_target(VEngineScriptAPI ALL
        COMMAND "${VE_DOTNET_EXECUTABLE}"
            build "${scriptApiProject}"
            --configuration $<CONFIG>
            --framework "${VE_DOTNET_TARGET_FRAMEWORK}"
            --output "${PROJECT_BINARY_DIR}/Generated/Scripts/Windows/$<CONFIG>/VEngine.ScriptAPI"
            -p:BaseIntermediateOutputPath="${scriptApiIntermediateOutput}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Building VEngine.ScriptAPI (${VE_DOTNET_TARGET_FRAMEWORK})"
        VERBATIM
    )

    set_target_properties(VEngineScriptAPI
        PROPERTIES
            FOLDER "Managed"
    )
endfunction()
