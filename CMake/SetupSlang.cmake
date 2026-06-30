include_guard(GLOBAL)

get_filename_component(_VE_SLANG_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(VE_SLANG_PACKAGE_NAME "" CACHE STRING "Slang package directory name.")
set(VE_SLANG_THIRD_PARTY_ROOT "${_VE_SLANG_REPOSITORY_ROOT}/ThirdParty/Slang" CACHE PATH "Slang third-party root.")
set(VE_SLANG_PACKAGE_ROOT "${VE_SLANG_THIRD_PARTY_ROOT}/${VE_SLANG_PACKAGE_NAME}" CACHE PATH "Extracted Slang package root.")
set(VE_SLANG_EXECUTABLE "" CACHE FILEPATH "Path to the slangc executable.")

function(ve_reset_old_slang_cache)
    if(VE_SLANG_EXECUTABLE AND NOT EXISTS "${VE_SLANG_EXECUTABLE}")
        set(VE_SLANG_EXECUTABLE "" CACHE FILEPATH "Path to the slangc executable." FORCE)
    endif()
endfunction()

function(ve_get_default_slang_package_name outVariable)
    if(APPLE)
        set(packageName "slang-2026.12-macos-aarch64")
    else()
        set(packageName "slang-2026.12-windows-x86_64")
    endif()

    set(${outVariable} "${packageName}" PARENT_SCOPE)
endfunction()

function(ve_setup_slang)
    ve_reset_old_slang_cache()

    if(NOT VE_SLANG_PACKAGE_NAME)
        ve_get_default_slang_package_name(VE_SLANG_PACKAGE_NAME)
        set(VE_SLANG_PACKAGE_NAME "${VE_SLANG_PACKAGE_NAME}" CACHE STRING "Slang package directory name." FORCE)
    endif()

    set(defaultPackageRoot "${VE_SLANG_THIRD_PARTY_ROOT}/${VE_SLANG_PACKAGE_NAME}")
    if(EXISTS "${defaultPackageRoot}"
       AND (NOT VE_SLANG_PACKAGE_ROOT
            OR VE_SLANG_PACKAGE_ROOT STREQUAL "${VE_SLANG_THIRD_PARTY_ROOT}"
            OR VE_SLANG_PACKAGE_ROOT STREQUAL "${VE_SLANG_THIRD_PARTY_ROOT}/"))
        set(VE_SLANG_PACKAGE_ROOT "${defaultPackageRoot}" CACHE PATH "Extracted Slang package root." FORCE)
    endif()

    if(NOT VE_SLANG_EXECUTABLE AND EXISTS "${VE_SLANG_PACKAGE_ROOT}/bin/slangc.exe")
        set(VE_SLANG_EXECUTABLE "${VE_SLANG_PACKAGE_ROOT}/bin/slangc.exe" CACHE FILEPATH "Path to the slangc executable." FORCE)
    endif()

    if(NOT VE_SLANG_EXECUTABLE AND EXISTS "${VE_SLANG_PACKAGE_ROOT}/bin/slangc")
        set(VE_SLANG_EXECUTABLE "${VE_SLANG_PACKAGE_ROOT}/bin/slangc" CACHE FILEPATH "Path to the slangc executable." FORCE)
    endif()

    if(NOT VE_SLANG_EXECUTABLE)
        find_program(foundSlangExecutable
            NAMES slangc slangc.exe
            HINTS
                "${VE_SLANG_PACKAGE_ROOT}/bin"
                "${VE_SLANG_THIRD_PARTY_ROOT}"
            DOC "Path to the slangc executable used for shader generation."
        )

        if(foundSlangExecutable)
            set(VE_SLANG_EXECUTABLE "${foundSlangExecutable}" CACHE FILEPATH "Path to the slangc executable." FORCE)
        endif()
    endif()

    if(NOT VE_SLANG_EXECUTABLE)
        message(FATAL_ERROR
            "slangc was not found. Run ThirdParty/Build_Mac.sh or ThirdParty/Build_Windows64.bat, or set VE_SLANG_EXECUTABLE."
        )
    endif()

    message(STATUS "Slang executable: ${VE_SLANG_EXECUTABLE}")
endfunction()
