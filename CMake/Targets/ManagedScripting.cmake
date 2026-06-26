include_guard(GLOBAL)

set(VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Managed/VEngine.ScriptHost")
set(VE_SCRIPT_HOST_MANAGED_OBJ_DIR "${CMAKE_BINARY_DIR}/ManagedObj/VEngine.ScriptHost/")

function(ve_find_dotnet_sdk output_variable)
    find_program(VE_DOTNET_EXECUTABLE dotnet)
    if(NOT VE_DOTNET_EXECUTABLE)
        message(FATAL_ERROR "dotnet SDK was not found. VEngine managed scripting requires dotnet on PATH.")
    endif()

    set(${output_variable} "${VE_DOTNET_EXECUTABLE}" PARENT_SCOPE)
endfunction()

function(ve_add_managed_script_host)
    if(TARGET VEngineScriptHostManaged)
        return()
    endif()

    if(NOT WIN32)
        return()
    endif()

    ve_find_dotnet_sdk(dotnet_executable)

    add_custom_target(VEngineScriptHostManaged
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}"
        COMMAND "${dotnet_executable}" build
            "${PROJECT_SOURCE_DIR}/Engine/Managed/VEngine.ScriptHost/VEngine.ScriptHost.csproj"
            --configuration $<CONFIG>
            --output "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}"
            -p:BaseIntermediateOutputPath="${VE_SCRIPT_HOST_MANAGED_OBJ_DIR}"
            --nologo
        BYPRODUCTS
            "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}/VEngine.ScriptHost.dll"
            "${VE_SCRIPT_HOST_MANAGED_OUTPUT_DIR}/VEngine.ScriptHost.runtimeconfig.json"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        COMMENT "Building VEngine.ScriptHost managed assembly"
        VERBATIM
    )
endfunction()
