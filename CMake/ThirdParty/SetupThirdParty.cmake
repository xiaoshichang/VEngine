include_guard(GLOBAL)

get_filename_component(_VE_THIRD_PARTY_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_VE_THIRD_PARTY_ROOT "${_VE_THIRD_PARTY_REPOSITORY_ROOT}/ThirdParty")

function(ve_find_third_party_python outVariable)
    if(VE_THIRD_PARTY_PYTHON_EXECUTABLE AND EXISTS "${VE_THIRD_PARTY_PYTHON_EXECUTABLE}")
        set(${outVariable} "${VE_THIRD_PARTY_PYTHON_EXECUTABLE}" PARENT_SCOPE)
        return()
    endif()

    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_EXECUTABLE)
        set(VE_THIRD_PARTY_PYTHON_EXECUTABLE "${Python3_EXECUTABLE}" CACHE FILEPATH "Python executable used for VEngine third-party setup." FORCE)
        set(${outVariable} "${Python3_EXECUTABLE}" PARENT_SCOPE)
        return()
    endif()

    find_program(foundPython3 python3)
    if(foundPython3)
        set(VE_THIRD_PARTY_PYTHON_EXECUTABLE "${foundPython3}" CACHE FILEPATH "Python executable used for VEngine third-party setup." FORCE)
        set(${outVariable} "${foundPython3}" PARENT_SCOPE)
        return()
    endif()

    message(FATAL_ERROR "Python 3 was not found. Install Python 3 so CMake can prepare missing ThirdParty payloads.")
endfunction()

function(ve_run_third_party_setup dependency)
    string(TOUPPER "${dependency}" dependencyUpper)
    string(REGEX REPLACE "[^A-Z0-9]" "_" dependencyKey "${dependencyUpper}")
    set(ranCacheVariable "VE_THIRD_PARTY_SETUP_RAN_${dependencyKey}")

    if(${ranCacheVariable})
        return()
    endif()

    ve_find_third_party_python(pythonExecutable)

    set(command "${pythonExecutable}" "${_VE_THIRD_PARTY_ROOT}/main.py" "${dependency}" ${ARGN})
    set(environmentArgs)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND VE_IOS_DEPLOYMENT_TARGET)
        list(APPEND environmentArgs "VE_IOS_DEPLOYMENT_TARGET=${VE_IOS_DEPLOYMENT_TARGET}")
    endif()

    message(STATUS "Preparing ThirdParty dependency '${dependency}'")
    if(environmentArgs)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env ${environmentArgs} ${command}
            WORKING_DIRECTORY "${_VE_THIRD_PARTY_ROOT}"
            RESULT_VARIABLE setupResult
        )
    else()
        execute_process(
            COMMAND ${command}
            WORKING_DIRECTORY "${_VE_THIRD_PARTY_ROOT}"
            RESULT_VARIABLE setupResult
        )
    endif()

    if(NOT setupResult EQUAL 0)
        message(FATAL_ERROR "ThirdParty setup failed for '${dependency}' with exit code ${setupResult}.")
    endif()

    set(${ranCacheVariable} ON CACHE INTERNAL "ThirdParty setup already ran for ${dependency} in this build tree." FORCE)
endfunction()

function(ve_add_third_party_marker_target targetName)
    if(NOT TARGET ${targetName})
        add_custom_target(${targetName})
        set_target_properties(${targetName}
            PROPERTIES
                FOLDER "ThirdParty"
        )
    endif()
endfunction()
