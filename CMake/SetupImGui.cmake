include_guard(GLOBAL)

get_filename_component(_VE_IMGUI_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(VE_IMGUI_THIRD_PARTY_ROOT "${_VE_IMGUI_REPOSITORY_ROOT}/ThirdParty/ImGui" CACHE PATH "Dear ImGui third-party root.")
set(VE_IMGUI_VERSION "1.92.8" CACHE STRING "Vendored Dear ImGui version.")

set(_VE_IMGUI_DEFAULT_SOURCE_DIR "${VE_IMGUI_THIRD_PARTY_ROOT}/imgui-${VE_IMGUI_VERSION}")
if(VE_IMGUI_SOURCE_DIR MATCHES "/ThirdParty/ImGui/Source$")
    set(VE_IMGUI_SOURCE_DIR "${_VE_IMGUI_DEFAULT_SOURCE_DIR}" CACHE PATH "Vendored Dear ImGui source directory." FORCE)
else()
    set(VE_IMGUI_SOURCE_DIR "${_VE_IMGUI_DEFAULT_SOURCE_DIR}" CACHE PATH "Vendored Dear ImGui source directory.")
endif()

function(ve_validate_imgui_source)
    set(requiredImGuiFiles
        "${VE_IMGUI_SOURCE_DIR}/imgui.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui.h"
        "${VE_IMGUI_SOURCE_DIR}/imgui_demo.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    )

    if(WIN32)
        list(APPEND requiredImGuiFiles
            "${VE_IMGUI_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
            "${VE_IMGUI_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
            "${VE_IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
        )
    endif()

    foreach(requiredImGuiFile IN LISTS requiredImGuiFiles)
        if(NOT EXISTS "${requiredImGuiFile}")
            message(FATAL_ERROR
                "Dear ImGui vendored source is missing required file: ${requiredImGuiFile}\n"
                "Restore ThirdParty/ImGui/imgui-${VE_IMGUI_VERSION} or set VE_IMGUI_SOURCE_DIR to a complete ImGui checkout."
            )
        endif()
    endforeach()
endfunction()

function(ve_add_imgui_library)
    if(TARGET VEngineImGui)
        return()
    endif()

    ve_validate_imgui_source()

    message(STATUS "Dear ImGui source: ${VE_IMGUI_SOURCE_DIR}")
    message(STATUS "Dear ImGui version: ${VE_IMGUI_VERSION}")

    add_library(VEngineImGui STATIC
        "${VE_IMGUI_SOURCE_DIR}/imgui.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui_demo.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
        "${VE_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    )

    if(WIN32)
        target_sources(VEngineImGui
            PRIVATE
                "${VE_IMGUI_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
                "${VE_IMGUI_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
                "${VE_IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
        )
    endif()

    add_library(VEngine::ImGui ALIAS VEngineImGui)

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

    set_target_properties(VEngineImGui
        PROPERTIES
            CXX_EXTENSIONS OFF
    )

    if(MSVC)
        target_compile_definitions(VEngineImGui
            PUBLIC
                IMGUI_IMPL_API=
        )
    endif()

    if(WIN32)
        target_link_libraries(VEngineImGui
            PUBLIC
                d3d11
                d3d12
                d3dcompiler
                dxgi
        )
    elseif(APPLE)
        target_sources(VEngineImGui
            PRIVATE
                "${VE_IMGUI_SOURCE_DIR}/backends/imgui_impl_osx.mm"
                "${VE_IMGUI_SOURCE_DIR}/backends/imgui_impl_metal.mm"
        )
    endif()
endfunction()

function(ve_setup_imgui targetName)
    if(NOT TARGET ${targetName})
        message(FATAL_ERROR "ve_setup_imgui target does not exist: ${targetName}")
    endif()

    ve_add_imgui_library()

    target_link_libraries(${targetName}
        PRIVATE
            VEngine::ImGui
    )
endfunction()
