include_guard(GLOBAL)

get_filename_component(_VE_IMGUI_NODE_EDITOR_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(VE_IMGUI_NODE_EDITOR_THIRD_PARTY_ROOT
    "${_VE_IMGUI_NODE_EDITOR_REPOSITORY_ROOT}/ThirdParty/ImGuiNodeEditor"
    CACHE PATH "imgui-node-editor third-party root."
)
set(VE_IMGUI_NODE_EDITOR_REVISION "55a7dbf" CACHE STRING "Vendored imgui-node-editor revision.")

set(_VE_IMGUI_NODE_EDITOR_DEFAULT_SOURCE_DIR
    "${VE_IMGUI_NODE_EDITOR_THIRD_PARTY_ROOT}/imgui-node-editor-${VE_IMGUI_NODE_EDITOR_REVISION}"
)

# Migrate the derived source directory cached by the original wrapper. A development override outside the repository-vendored path is preserved.
if(DEFINED CACHE{VE_IMGUI_NODE_EDITOR_SOURCE_DIR})
    get_property(_VE_IMGUI_NODE_EDITOR_SOURCE_DIR_HELP CACHE VE_IMGUI_NODE_EDITOR_SOURCE_DIR PROPERTY HELPSTRING)
    file(TO_CMAKE_PATH "${VE_IMGUI_NODE_EDITOR_SOURCE_DIR}" _VE_IMGUI_NODE_EDITOR_CACHED_SOURCE_DIR)
    file(TO_CMAKE_PATH "${VE_IMGUI_NODE_EDITOR_THIRD_PARTY_ROOT}/imgui-node-editor-" _VE_IMGUI_NODE_EDITOR_VENDORED_PREFIX)
    string(FIND "${_VE_IMGUI_NODE_EDITOR_CACHED_SOURCE_DIR}" "${_VE_IMGUI_NODE_EDITOR_VENDORED_PREFIX}" _VE_IMGUI_NODE_EDITOR_PREFIX_INDEX)
    if(_VE_IMGUI_NODE_EDITOR_SOURCE_DIR_HELP STREQUAL "Vendored imgui-node-editor source directory."
       AND _VE_IMGUI_NODE_EDITOR_PREFIX_INDEX EQUAL 0)
        unset(VE_IMGUI_NODE_EDITOR_SOURCE_DIR CACHE)
    endif()
endif()

set(VE_IMGUI_NODE_EDITOR_SOURCE_DIR "" CACHE PATH "Optional imgui-node-editor development source override.")

if(VE_IMGUI_NODE_EDITOR_SOURCE_DIR)
    get_filename_component(_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR "${VE_IMGUI_NODE_EDITOR_SOURCE_DIR}" ABSOLUTE)
else()
    set(_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR "${_VE_IMGUI_NODE_EDITOR_DEFAULT_SOURCE_DIR}")
endif()

if("${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}" STREQUAL "${_VE_IMGUI_NODE_EDITOR_DEFAULT_SOURCE_DIR}")
    set(_VE_IMGUI_NODE_EDITOR_VALIDATE_SNAPSHOT_HASHES ON)
else()
    set(_VE_IMGUI_NODE_EDITOR_VALIDATE_SNAPSHOT_HASHES OFF)
endif()

function(ve_validate_imgui_node_editor_source)
    set(requiredImGuiNodeEditorFilesAndHashes
        "LICENSE|2d176cd30f8dc07a5b8e922fb0de308ffea7b8f2d7ff5a5f2ee99b562ed05b0c"
        "crude_json.cpp|e81eecfe7d55968b5369e57d7d3d38fda415d641aee068c25aac21ef4a892b93"
        "crude_json.h|68120f5ff1f87379c53c02143c9999d4b511545ccf69f7964fb008344edd26f7"
        "imgui_bezier_math.h|f2e111af449e782f00fa75212ddbf4758f32bca5abd2450892a1c83793850373"
        "imgui_bezier_math.inl|122864804b047b5e071459b748dfa9b549202c1031ba2d9c1e5d3167592ffdf2"
        "imgui_canvas.cpp|a62c9c18803d8976fbee1791c1f5b30377a7a8f0b1380f294209469a629822fc"
        "imgui_canvas.h|0612018954976400b86894582037531077532fc02c548db70fc100bc847bfc51"
        "imgui_extra_math.h|c96ca0a272bf4c966b2b99bb738705f0475411b3d8d017f366278fcae5b5349e"
        "imgui_extra_math.inl|fec446094f474b96095f0c3bd43e0397e6b1cef3de3c9d432567f7ef7b1305b5"
        "imgui_node_editor.cpp|91524ee17ecffd6fc5d2e9e809cea1a99e480f9eece268294c30dcb116703ae8"
        "imgui_node_editor_api.cpp|ee9a21a9b9fbb628be8e2bd909e1166dc655dd9561e58a58aeb9cd5b1054a508"
        "imgui_node_editor.h|82e72d2b1acf87d0b57d220f48023b59c39ed0b52d894c3b228b9266de010fdb"
        "imgui_node_editor_internal.h|50e20d87b0ee2f1fee2079b1244c4b388bd82dc753833d59c7fff4fc0d60b25d"
        "imgui_node_editor_internal.inl|545c91e1a0c19326661734440a98ffc78ee0e9b628248320feb804ec88c7c2c4"
    )

    foreach(requiredImGuiNodeEditorFileAndHash IN LISTS requiredImGuiNodeEditorFilesAndHashes)
        string(REPLACE "|" ";" requiredImGuiNodeEditorParts "${requiredImGuiNodeEditorFileAndHash}")
        list(GET requiredImGuiNodeEditorParts 0 requiredImGuiNodeEditorFileName)
        list(GET requiredImGuiNodeEditorParts 1 expectedImGuiNodeEditorHash)
        set(requiredImGuiNodeEditorFile "${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}/${requiredImGuiNodeEditorFileName}")

        if(NOT EXISTS "${requiredImGuiNodeEditorFile}" OR IS_DIRECTORY "${requiredImGuiNodeEditorFile}")
            message(FATAL_ERROR
                "imgui-node-editor source is missing required file: ${requiredImGuiNodeEditorFile}\n"
                "Restore the vendored snapshot or set VE_IMGUI_NODE_EDITOR_SOURCE_DIR to a complete development source tree."
            )
        endif()

        if(_VE_IMGUI_NODE_EDITOR_VALIDATE_SNAPSHOT_HASHES)
            file(SHA256 "${requiredImGuiNodeEditorFile}" actualImGuiNodeEditorHash)
            if(NOT actualImGuiNodeEditorHash STREQUAL expectedImGuiNodeEditorHash)
                message(FATAL_ERROR
                    "imgui-node-editor vendored source hash mismatch: ${requiredImGuiNodeEditorFile}\n"
                    "Expected SHA-256: ${expectedImGuiNodeEditorHash}\n"
                    "Actual SHA-256:   ${actualImGuiNodeEditorHash}\n"
                    "Restore ThirdParty/ImGuiNodeEditor/imgui-node-editor-${VE_IMGUI_NODE_EDITOR_REVISION}."
                )
            endif()
        endif()
    endforeach()
endfunction()

function(ve_add_imgui_node_editor_library)
    if(TARGET VEngineImGuiNodeEditor)
        return()
    endif()

    ve_validate_imgui_node_editor_source()

    message(STATUS "imgui-node-editor source: ${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}")
    message(STATUS "imgui-node-editor revision: ${VE_IMGUI_NODE_EDITOR_REVISION}")

    add_library(VEngineImGuiNodeEditor STATIC
        "${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}/crude_json.cpp"
        "${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}/imgui_canvas.cpp"
        "${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}/imgui_node_editor.cpp"
        "${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}/imgui_node_editor_api.cpp"
    )
    add_library(VEngine::ImGuiNodeEditor ALIAS VEngineImGuiNodeEditor)

    target_include_directories(VEngineImGuiNodeEditor
        SYSTEM
        PUBLIC
            "${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}"
    )

    target_link_libraries(VEngineImGuiNodeEditor
        PUBLIC
            VEngine::ImGui
    )

    target_compile_features(VEngineImGuiNodeEditor
        PUBLIC
            cxx_std_20
    )

    set_target_properties(VEngineImGuiNodeEditor
        PROPERTIES
            CXX_EXTENSIONS OFF
            FOLDER "ThirdParty/ImGuiNodeEditor"
    )
endfunction()

function(ve_setup_imgui_node_editor targetName)
    if(NOT TARGET ${targetName})
        message(FATAL_ERROR "ve_setup_imgui_node_editor target does not exist: ${targetName}")
    endif()

    ve_add_imgui_node_editor_library()

    target_link_libraries(${targetName}
        PRIVATE
            VEngine::ImGuiNodeEditor
    )
endfunction()
