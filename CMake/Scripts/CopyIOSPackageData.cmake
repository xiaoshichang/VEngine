if(NOT DEFINED VE_IOS_PACKAGE_MANIFEST OR VE_IOS_PACKAGE_MANIFEST STREQUAL "")
    message(FATAL_ERROR "VE_IOS_PACKAGE_MANIFEST is required.")
endif()

if(NOT DEFINED VE_IOS_PACKAGE_DESTINATION OR VE_IOS_PACKAGE_DESTINATION STREQUAL "")
    message(FATAL_ERROR "VE_IOS_PACKAGE_DESTINATION is required.")
endif()

if(NOT EXISTS "${VE_IOS_PACKAGE_MANIFEST}")
    message(FATAL_ERROR "iOS package data manifest does not exist: ${VE_IOS_PACKAGE_MANIFEST}")
endif()

file(READ "${VE_IOS_PACKAGE_MANIFEST}" ve_ios_package_manifest_text)
string(JSON ve_ios_package_file_count LENGTH "${ve_ios_package_manifest_text}" files)

file(REMOVE_RECURSE "${VE_IOS_PACKAGE_DESTINATION}")
file(MAKE_DIRECTORY "${VE_IOS_PACKAGE_DESTINATION}")

if(ve_ios_package_file_count EQUAL 0)
    return()
endif()

math(EXPR ve_ios_package_last_index "${ve_ios_package_file_count} - 1")
foreach(ve_ios_package_index RANGE 0 ${ve_ios_package_last_index})
    string(JSON ve_ios_package_source GET "${ve_ios_package_manifest_text}" files ${ve_ios_package_index} source)
    string(JSON ve_ios_package_destination GET "${ve_ios_package_manifest_text}" files ${ve_ios_package_index} destination)

    if(NOT EXISTS "${ve_ios_package_source}")
        message(FATAL_ERROR "iOS package input does not exist: ${ve_ios_package_source}")
    endif()

    if(ve_ios_package_destination STREQUAL "" OR ve_ios_package_destination MATCHES "^/" OR ve_ios_package_destination MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "Invalid iOS package destination path: ${ve_ios_package_destination}")
    endif()

    get_filename_component(ve_ios_package_destination_directory "${VE_IOS_PACKAGE_DESTINATION}/${ve_ios_package_destination}" DIRECTORY)
    file(MAKE_DIRECTORY "${ve_ios_package_destination_directory}")
    file(COPY_FILE
        "${ve_ios_package_source}"
        "${VE_IOS_PACKAGE_DESTINATION}/${ve_ios_package_destination}"
        ONLY_IF_DIFFERENT
    )
endforeach()
