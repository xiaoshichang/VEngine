include_guard(GLOBAL)

get_filename_component(_VE_JOLT_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(VE_JOLT_GIT_TAG "v5.5.0" CACHE STRING "Jolt Physics git tag used by the setup script.")
set(VE_JOLT_THIRD_PARTY_ROOT "${_VE_JOLT_REPOSITORY_ROOT}/ThirdParty/Jolt" CACHE PATH "Jolt Physics third-party root.")
set(VE_JOLT_SOURCE_DIR "${VE_JOLT_THIRD_PARTY_ROOT}/Source" CACHE PATH "Downloaded Jolt Physics source directory.")
option(VE_JOLT_SETUP_IF_MISSING "Download Jolt Physics through the setup script when the source is missing." ON)
option(VE_JOLT_BUILD_TESTS_AND_DEMOS "Build Jolt upstream tests and demos as part of the VEngine CMake graph." OFF)
option(VE_JOLT_ENABLE_DEBUG_RENDERER "Enable Jolt debug renderer support in the embedded VEngine build." OFF)
option(VE_JOLT_ENABLE_PROFILER "Enable Jolt profiler support in the embedded VEngine build." OFF)
option(VE_JOLT_ENABLE_OBJECT_STREAM "Enable Jolt ObjectStream support in the embedded VEngine build." OFF)
option(VE_JOLT_USE_STATIC_MSVC_RUNTIME_LIBRARY "Build embedded Jolt with the static MSVC runtime." OFF)

function(ve_prepare_jolt_source)
    set(requiredJoltFiles
        "${VE_JOLT_SOURCE_DIR}/Build/CMakeLists.txt"
        "${VE_JOLT_SOURCE_DIR}/Jolt/Jolt.h"
        "${VE_JOLT_SOURCE_DIR}/Jolt/Jolt.cmake"
    )

    set(joltSourceReady ON)
    foreach(requiredJoltFile IN LISTS requiredJoltFiles)
        if(NOT EXISTS "${requiredJoltFile}")
            set(joltSourceReady OFF)
        endif()
    endforeach()

    if(joltSourceReady)
        return()
    endif()

    if(WIN32 AND VE_JOLT_SETUP_IF_MISSING)
        execute_process(
            COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${VE_JOLT_THIRD_PARTY_ROOT}/Setup_Windows64.ps1" -Tag ${VE_JOLT_GIT_TAG}
            WORKING_DIRECTORY "${_VE_JOLT_REPOSITORY_ROOT}"
            RESULT_VARIABLE setupJoltResult
        )

        if(NOT setupJoltResult EQUAL 0)
            message(FATAL_ERROR "Jolt Physics setup failed with exit code ${setupJoltResult}.")
        endif()
    endif()

    foreach(requiredJoltFile IN LISTS requiredJoltFiles)
        if(NOT EXISTS "${requiredJoltFile}")
            message(FATAL_ERROR
                "Jolt Physics source is missing required file: ${requiredJoltFile}\n"
                "Run ThirdParty/Jolt/Setup_Windows64.ps1 or set VE_JOLT_SOURCE_DIR to a complete Jolt Physics checkout."
            )
        endif()
    endforeach()
endfunction()

function(ve_add_jolt_library)
    if(TARGET VEngine::Jolt)
        return()
    endif()

    ve_prepare_jolt_source()

    message(STATUS "Jolt Physics source: ${VE_JOLT_SOURCE_DIR}")
    message(STATUS "Jolt Physics tag: ${VE_JOLT_GIT_TAG}")

    set(BUILD_SHARED_LIBS OFF)
    set(ENABLE_INSTALL OFF)
    set(ENABLE_ALL_WARNINGS OFF)
    set(OVERRIDE_CXX_FLAGS OFF)
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY ${VE_JOLT_USE_STATIC_MSVC_RUNTIME_LIBRARY})
    set(TARGET_UNIT_TESTS ${VE_JOLT_BUILD_TESTS_AND_DEMOS})
    set(TARGET_HELLO_WORLD ${VE_JOLT_BUILD_TESTS_AND_DEMOS})
    set(TARGET_PERFORMANCE_TEST ${VE_JOLT_BUILD_TESTS_AND_DEMOS})
    set(TARGET_SAMPLES ${VE_JOLT_BUILD_TESTS_AND_DEMOS})
    set(TARGET_VIEWER ${VE_JOLT_BUILD_TESTS_AND_DEMOS})
    set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ${VE_JOLT_ENABLE_DEBUG_RENDERER})
    set(DEBUG_RENDERER_IN_DISTRIBUTION ${VE_JOLT_ENABLE_DEBUG_RENDERER})
    set(PROFILER_IN_DEBUG_AND_RELEASE ${VE_JOLT_ENABLE_PROFILER})
    set(PROFILER_IN_DISTRIBUTION ${VE_JOLT_ENABLE_PROFILER})
    set(ENABLE_OBJECT_STREAM ${VE_JOLT_ENABLE_OBJECT_STREAM})

    add_subdirectory("${VE_JOLT_SOURCE_DIR}/Build" "${CMAKE_BINARY_DIR}/ThirdParty/Jolt" EXCLUDE_FROM_ALL)

    if(NOT TARGET Jolt)
        message(FATAL_ERROR "Jolt Physics CMake did not create the Jolt target.")
    endif()

    if(NOT TARGET VEngine::Jolt)
        add_library(VEngine::Jolt ALIAS Jolt)
    endif()

    set_target_properties(Jolt
        PROPERTIES
            FOLDER "ThirdParty/Jolt"
    )
endfunction()

function(ve_setup_jolt_library targetName)
    if(NOT TARGET ${targetName})
        message(FATAL_ERROR "ve_setup_jolt_library target does not exist: ${targetName}")
    endif()

    ve_add_jolt_library()

    target_link_libraries(${targetName}
        PRIVATE
            VEngine::Jolt
    )
endfunction()
