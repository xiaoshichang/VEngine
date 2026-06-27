include_guard(GLOBAL)

get_filename_component(_VE_SLANG_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(VE_SLANG_EXECUTABLE "" CACHE FILEPATH "Path to the Slang command line compiler used for HLSL to SPIR-V.")
set(VE_SLANG_THIRD_PARTY_ROOT "${_VE_SLANG_REPOSITORY_ROOT}/ThirdParty/Slang" CACHE PATH "Slang third-party root.")

function(ve_reset_old_slang_cache)
    if(VE_SLANG_EXECUTABLE AND NOT EXISTS "${VE_SLANG_EXECUTABLE}")
        set(VE_SLANG_EXECUTABLE "" CACHE FILEPATH "Path to the Slang command line compiler used for HLSL to SPIR-V." FORCE)
    endif()
endfunction()

function(ve_setup_slang_compiler)
    ve_reset_old_slang_cache()

    if(NOT VE_SLANG_EXECUTABLE AND WIN32)
        set(projectSlangExecutable "${VE_SLANG_THIRD_PARTY_ROOT}/windows64/bin/slangc.exe")

        if(EXISTS "${projectSlangExecutable}")
            set(VE_SLANG_EXECUTABLE "${projectSlangExecutable}" CACHE FILEPATH "Path to the Slang command line compiler used for HLSL to SPIR-V." FORCE)
        endif()
    endif()

    if(NOT VE_SLANG_EXECUTABLE)
        find_program(foundSlangExecutable
            NAMES slangc slangc.exe
            DOC "Path to the Slang command line compiler used for HLSL to SPIR-V."
        )

        if(foundSlangExecutable)
            set(VE_SLANG_EXECUTABLE "${foundSlangExecutable}" CACHE FILEPATH "Path to the Slang command line compiler used for HLSL to SPIR-V." FORCE)
        endif()
    endif()

    if(NOT VE_SLANG_EXECUTABLE)
        message(FATAL_ERROR
            "slangc was not found. Set VE_SLANG_EXECUTABLE or install the project Slang payload under "
            "ThirdParty/Slang/windows64/bin."
        )
    endif()

    message(STATUS "Slang executable: ${VE_SLANG_EXECUTABLE}")
endfunction()
