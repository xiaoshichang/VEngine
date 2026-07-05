include_guard(GLOBAL)

function(ve_copy_windows_scripting_payload target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:${target_name}>/Managed/VEngine.ScriptHost"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VE_ENGINE_SCRIPT_HOST_OUTPUT_DIR}"
            "$<TARGET_FILE_DIR:${target_name}>/Managed/VEngine.ScriptHost"
        COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:${target_name}>/DotNet"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VE_DOTNET_RUNTIME_ROOT}"
            "$<TARGET_FILE_DIR:${target_name}>/DotNet/win-x64/10.0.9"
        COMMENT "Copying app-local .NET scripting payload"
    )
endfunction()

function(ve_add_windows_player)
    add_executable(VEngineWinPlayer WIN32
        Player/Windows/main.cpp
        Player/Windows/VEnginePlayer.cpp
        Player/Windows/VEnginePlayer.h
    )

    target_link_libraries(VEngineWinPlayer
        PRIVATE
            VEngine
    )

    ve_add_engine_script_host()
    add_dependencies(VEngineWinPlayer VEngineEngineScriptHost)

    ve_copy_windows_scripting_payload(VEngineWinPlayer)

    ve_configure_target(VEngineWinPlayer)
endfunction()

function(ve_add_windows_editor)
    add_executable(VEngineWinEditor WIN32
        ${VE_EDITOR_COMMON_SOURCES}
        Editor/Windows/main.cpp
        Editor/Windows/WinEditorInputBackend.cpp
        Editor/Windows/WinEditorInputBackend.h
        Editor/Windows/WinEditorProjectPacker.cpp
        Editor/Windows/WinEditorProjectPacker.h
        Editor/Windows/WinEditorProjectRegistryBackend.cpp
        Editor/Windows/WinEditorProjectRegistryBackend.h
        Editor/Windows/WinEditorRenderBackend.cpp
        Editor/Windows/WinEditorRenderBackend.h
        Editor/Windows/WindowsEditorApplication.cpp
        Editor/Windows/WindowsEditorApplication.h
    )

    target_link_libraries(VEngineWinEditor
        PRIVATE
            VEngine
    )

    ve_add_shader_tool()
    ve_add_engine_script_host()
    add_dependencies(VEngineWinEditor VEngineShaderTool VEngineEngineScriptHost)

    ve_setup_imgui(VEngineWinEditor)
    ve_add_editor_packaging_definitions(VEngineWinEditor)

    add_custom_command(TARGET VEngineWinEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/Assets"
            "$<TARGET_FILE_DIR:VEngineWinEditor>/Assets"
        COMMENT "Copying VEngine editor asset roots"
    )

    ve_copy_windows_scripting_payload(VEngineWinEditor)

    ve_configure_target(VEngineWinEditor)
endfunction()
