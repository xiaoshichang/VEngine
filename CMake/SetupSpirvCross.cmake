include_guard(GLOBAL)

get_filename_component(_VE_SPIRV_CROSS_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(VE_SPIRV_CROSS_EXECUTABLE "" CACHE FILEPATH "Path to the spirv-cross executable.")
set(VE_SPIRV_CROSS_GIT_TAG "vulkan-sdk-1.4.309.0" CACHE STRING "SPIRV-Cross git tag used by the setup script.")
set(VE_SPIRV_CROSS_THIRD_PARTY_ROOT "${_VE_SPIRV_CROSS_REPOSITORY_ROOT}/ThirdParty/SPIRV-Cross" CACHE PATH "SPIRV-Cross third-party root.")
set(VE_SPIRV_CROSS_SOURCE_DIR "${VE_SPIRV_CROSS_THIRD_PARTY_ROOT}/Source" CACHE PATH "Downloaded SPIRV-Cross source directory.")
set(VE_SPIRV_CROSS_BUILD_DIR "${VE_SPIRV_CROSS_THIRD_PARTY_ROOT}/Build/Windows64/${VE_SPIRV_CROSS_GIT_TAG}" CACHE PATH "SPIRV-Cross standalone build directory.")
set(VE_SPIRV_CROSS_BUILD_CONFIG "Release" CACHE STRING "SPIRV-Cross standalone build configuration used by VEngine.")
option(VE_SPIRV_CROSS_BUILD_IF_MISSING "Build SPIRV-Cross through its setup script when the executable is not set." ON)

function(ve_reset_old_spirv_cross_cache)
    if(VE_SPIRV_CROSS_EXECUTABLE MATCHES "\\$<TARGET_FILE:spirv-cross>")
        set(VE_SPIRV_CROSS_EXECUTABLE "" CACHE FILEPATH "Path to the spirv-cross executable." FORCE)
    endif()

    if(VE_SPIRV_CROSS_EXECUTABLE AND NOT EXISTS "${VE_SPIRV_CROSS_EXECUTABLE}")
        set(VE_SPIRV_CROSS_EXECUTABLE "" CACHE FILEPATH "Path to the spirv-cross executable." FORCE)
    endif()

    if(VE_SPIRV_CROSS_TARGET)
        set(VE_SPIRV_CROSS_TARGET "" CACHE INTERNAL "SPIRV-Cross executable target." FORCE)
    endif()
endfunction()

function(ve_setup_spirv_cross)
    ve_reset_old_spirv_cross_cache()

    if(NOT VE_SPIRV_CROSS_EXECUTABLE AND WIN32)
        set(builtSpirvCrossExecutable "${VE_SPIRV_CROSS_BUILD_DIR}/${VE_SPIRV_CROSS_BUILD_CONFIG}/spirv-cross.exe")

        if(NOT EXISTS "${builtSpirvCrossExecutable}" AND VE_SPIRV_CROSS_BUILD_IF_MISSING)
            execute_process(
                COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${VE_SPIRV_CROSS_THIRD_PARTY_ROOT}/Setup_Windows64.ps1" -Configuration ${VE_SPIRV_CROSS_BUILD_CONFIG}
                WORKING_DIRECTORY "${_VE_SPIRV_CROSS_REPOSITORY_ROOT}"
                RESULT_VARIABLE setupSpirvCrossResult
            )

            if(NOT setupSpirvCrossResult EQUAL 0)
                message(FATAL_ERROR "SPIRV-Cross setup failed with exit code ${setupSpirvCrossResult}.")
            endif()
        endif()

        if(EXISTS "${builtSpirvCrossExecutable}")
            set(VE_SPIRV_CROSS_EXECUTABLE "${builtSpirvCrossExecutable}" CACHE FILEPATH "Path to the spirv-cross executable." FORCE)
        endif()
    endif()

    if(VE_SPIRV_CROSS_EXECUTABLE)
        set(VE_SPIRV_CROSS_TARGET "" CACHE INTERNAL "SPIRV-Cross executable target.")
        message(STATUS "SPIRV-Cross executable: ${VE_SPIRV_CROSS_EXECUTABLE}")
        return()
    endif()

    message(FATAL_ERROR
        "SPIRV-Cross executable was not found. Run ThirdParty/SPIRV-Cross/Build_Windows64.bat "
        "or enable VE_SPIRV_CROSS_BUILD_IF_MISSING."
    )
endfunction()
