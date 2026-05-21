set(VE_TOOLCHAIN_PLATFORM "Windows64" CACHE STRING "VEngine target platform")

set(CMAKE_SYSTEM_NAME Windows CACHE STRING "Target system name" FORCE)
set(CMAKE_SYSTEM_PROCESSOR AMD64 CACHE STRING "Target system processor" FORCE)

if(NOT CMAKE_HOST_WIN32)
    message(STATUS "Windows64 toolchain selected on a non-Windows host; provide a Windows cross compiler explicitly.")
endif()

if(CMAKE_GENERATOR MATCHES "Visual Studio" AND CMAKE_GENERATOR_PLATFORM AND NOT CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    message(FATAL_ERROR "Windows64 toolchain requires the Visual Studio x64 generator platform.")
endif()
