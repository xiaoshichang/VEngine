include_guard(GLOBAL)

get_filename_component(_VE_SLANG_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(VE_SLANG_PACKAGE_NAME "slang-2026.12-windows-x86_64" CACHE STRING "Slang package directory name.")
set(VE_SLANG_THIRD_PARTY_ROOT "${_VE_SLANG_REPOSITORY_ROOT}/ThirdParty/Slang" CACHE PATH "Slang third-party root.")
set(VE_SLANG_PACKAGE_ROOT "${VE_SLANG_THIRD_PARTY_ROOT}/${VE_SLANG_PACKAGE_NAME}" CACHE PATH "Extracted Slang package root.")
set(VE_SLANG_EXECUTABLE "" CACHE FILEPATH "Path to the slangc executable.")
option(VE_SLANG_BUILD_IF_MISSING "Prepare Slang through its build script when the executable is not set." ON)

function(ve_reset_old_slang_cache)
    if(VE_SLANG_EXECUTABLE AND NOT EXISTS "${VE_SLANG_EXECUTABLE}")
        set(VE_SLANG_EXECUTABLE "" CACHE FILEPATH "Path to the slangc executable." FORCE)
    endif()
endfunction()

function(ve_setup_slang)
    ve_reset_old_slang_cache()

    if(NOT VE_SLANG_EXECUTABLE)
        find_program(foundSlangExecutable
            NAMES slangc slangc.exe
            HINTS
                "${VE_SLANG_PACKAGE_ROOT}/bin"
                "${VE_SLANG_THIRD_PARTY_ROOT}"
            DOC "Path to the slangc executable used for SPIR-V generation."
        )

        if(foundSlangExecutable)
            set(VE_SLANG_EXECUTABLE "${foundSlangExecutable}" CACHE FILEPATH "Path to the slangc executable." FORCE)
        endif()
    endif()

    if(NOT VE_SLANG_EXECUTABLE AND WIN32 AND VE_SLANG_BUILD_IF_MISSING)
        execute_process(
            COMMAND cmd /c "${VE_SLANG_THIRD_PARTY_ROOT}/Build_Windows64.bat"
            WORKING_DIRECTORY "${_VE_SLANG_REPOSITORY_ROOT}"
            RESULT_VARIABLE setupSlangResult
        )

        if(NOT setupSlangResult EQUAL 0)
            message(FATAL_ERROR "Slang preparation failed with exit code ${setupSlangResult}.")
        endif()
    endif()

    if(NOT VE_SLANG_EXECUTABLE)
        if(EXISTS "${VE_SLANG_PACKAGE_ROOT}/bin/slangc.exe")
            set(VE_SLANG_EXECUTABLE "${VE_SLANG_PACKAGE_ROOT}/bin/slangc.exe" CACHE FILEPATH "Path to the slangc executable." FORCE)
        endif()
    endif()

    if(NOT VE_SLANG_EXECUTABLE)
        message(FATAL_ERROR
            "slangc was not found. Run ThirdParty/Slang/Build_Windows64.bat or set VE_SLANG_EXECUTABLE."
        )
    endif()

    message(STATUS "Slang executable: ${VE_SLANG_EXECUTABLE}")
endfunction()
