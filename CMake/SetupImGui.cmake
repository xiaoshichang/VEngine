include_guard(GLOBAL)

get_filename_component(_VE_IMGUI_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(VE_IMGUI_GIT_TAG "v1.92.8" CACHE STRING "Dear ImGui git tag used by the setup script.")
set(VE_IMGUI_THIRD_PARTY_ROOT "${_VE_IMGUI_REPOSITORY_ROOT}/ThirdParty/ImGui" CACHE PATH "Dear ImGui third-party root.")
set(VE_IMGUI_SOURCE_DIR "${VE_IMGUI_THIRD_PARTY_ROOT}/Source" CACHE PATH "Downloaded Dear ImGui source directory.")
option(VE_IMGUI_DOWNLOAD_IF_MISSING "Download Dear ImGui into ThirdParty when the source directory is missing." ON)

function(ve_prepare_imgui_source)
    if(EXISTS "${VE_IMGUI_SOURCE_DIR}/imgui.cpp")
        return()
    endif()

    if(WIN32 AND VE_IMGUI_DOWNLOAD_IF_MISSING)
        execute_process(
            COMMAND powershell
                -NoProfile
                -ExecutionPolicy Bypass
                -File "${VE_IMGUI_THIRD_PARTY_ROOT}/Setup_Windows64.ps1"
                -Tag "${VE_IMGUI_GIT_TAG}"
            WORKING_DIRECTORY "${_VE_IMGUI_REPOSITORY_ROOT}"
            RESULT_VARIABLE setupImGuiResult
        )

        if(NOT setupImGuiResult EQUAL 0)
            message(FATAL_ERROR "Dear ImGui setup failed with exit code ${setupImGuiResult}.")
        endif()
    endif()

    if(NOT EXISTS "${VE_IMGUI_SOURCE_DIR}/imgui.cpp")
        message(FATAL_ERROR
            "Dear ImGui source was not found. Run ThirdParty/ImGui/Build_Windows64.bat "
            "or set VE_IMGUI_SOURCE_DIR to an existing Dear ImGui checkout."
        )
    endif()
endfunction()

function(ve_add_imgui)
    if(TARGET VEngineImGui)
        return()
    endif()

    if(NOT WIN32)
        message(FATAL_ERROR "Dear ImGui is currently integrated for the Windows Editor only.")
    endif()

    ve_prepare_imgui_source()

    set(imguiSources
        "${VE_IMGUI_SOURCE_DIR}/imgui.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui_demo.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
        "${VE_IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    )

    set(imguiHeaders
        "${VE_IMGUI_SOURCE_DIR}/imconfig.h"
        "${VE_IMGUI_SOURCE_DIR}/imgui.h"
        "${VE_IMGUI_SOURCE_DIR}/imgui_internal.h"
        "${VE_IMGUI_SOURCE_DIR}/imstb_rectpack.h"
        "${VE_IMGUI_SOURCE_DIR}/imstb_textedit.h"
        "${VE_IMGUI_SOURCE_DIR}/imstb_truetype.h"
        "${VE_IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.h"
    )

    foreach(imguiSource IN LISTS imguiSources)
        if(NOT EXISTS "${imguiSource}")
            message(FATAL_ERROR "Dear ImGui source is missing required file: ${imguiSource}")
        endif()
    endforeach()

    foreach(imguiHeader IN LISTS imguiHeaders)
        if(NOT EXISTS "${imguiHeader}")
            message(FATAL_ERROR "Dear ImGui source is missing required file: ${imguiHeader}")
        endif()
    endforeach()

    add_library(VEngineImGui STATIC)
    add_library(VEngine::ImGui ALIAS VEngineImGui)

    target_sources(VEngineImGui
        PRIVATE
            ${imguiSources}
        PUBLIC
            ${imguiHeaders}
    )

    target_include_directories(VEngineImGui
        SYSTEM
        PUBLIC
            "${VE_IMGUI_SOURCE_DIR}"
            "${VE_IMGUI_SOURCE_DIR}/backends"
    )

    target_compile_features(VEngineImGui
        PUBLIC
            cxx_std_20
    )

    target_link_libraries(VEngineImGui
        PUBLIC
            imm32
    )

    set_target_properties(VEngineImGui
        PROPERTIES
            CXX_EXTENSIONS OFF
            FOLDER ThirdParty
    )
endfunction()
