include_guard(GLOBAL)

function(ve_add_rhi_metal_triangle_demo)
    if(NOT (APPLE AND VE_ENABLE_METAL AND NOT (CMAKE_SYSTEM_NAME STREQUAL "iOS")))
        return()
    endif()

    enable_language(OBJCXX)

    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)

    add_executable(VEngineRhiMetalTriangleDemo MACOSX_BUNDLE
        Tests/Demos/RHI/macOS/MetalTriangleDemo.mm
    )

    target_link_libraries(VEngineRhiMetalTriangleDemo
        PRIVATE
            VEngine
            ${FOUNDATION_FRAMEWORK}
            ${APPKIT_FRAMEWORK}
            "-framework QuartzCore"
            "-framework Metal"
    )

    set_target_properties(VEngineRhiMetalTriangleDemo
        PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/Tests/Demos/RHI/macOS/Info.plist.in
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.vengine.tests.rhi.metaltriangle"
            XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "macosx"
    )

    ve_configure_target(VEngineRhiMetalTriangleDemo)
endfunction()
