if(NOT DEFINED VE_SHADER_TEST_OUTPUT_DIR)
    message(FATAL_ERROR "VE_SHADER_TEST_OUTPUT_DIR is required.")
endif()

function(ve_require_file fileName)
    set(filePath "${VE_SHADER_TEST_OUTPUT_DIR}/${fileName}")

    if(NOT EXISTS "${filePath}")
        message(FATAL_ERROR "Expected shader artifact does not exist: ${filePath}")
    endif()

    file(SIZE "${filePath}" fileSize)

    if(fileSize EQUAL 0)
        message(FATAL_ERROR "Expected shader artifact is empty: ${filePath}")
    endif()

endfunction()

function(ve_require_magic fileName expectedMagic)
    set(filePath "${VE_SHADER_TEST_OUTPUT_DIR}/${fileName}")
    file(READ "${filePath}" actualMagic LIMIT 4 HEX)
    string(TOLOWER "${actualMagic}" actualMagic)
    string(TOLOWER "${expectedMagic}" expectedMagic)

    if(NOT actualMagic STREQUAL expectedMagic)
        message(FATAL_ERROR "Unexpected magic for ${filePath}: expected ${expectedMagic}, got ${actualMagic}")
    endif()
endfunction()

function(ve_require_text fileName expectedPattern)
    set(filePath "${VE_SHADER_TEST_OUTPUT_DIR}/${fileName}")
    file(READ "${filePath}" content)

    if(NOT content MATCHES "${expectedPattern}")
        message(FATAL_ERROR "Expected ${filePath} to match pattern: ${expectedPattern}")
    endif()
endfunction()

function(ve_reject_text fileName rejectedPattern)
    set(filePath "${VE_SHADER_TEST_OUTPUT_DIR}/${fileName}")
    file(READ "${filePath}" content)

    if(content MATCHES "${rejectedPattern}")
        message(FATAL_ERROR "Expected ${filePath} to not match pattern: ${rejectedPattern}")
    endif()
endfunction()

set(expectedArtifacts
    BasicTriangle.D3D11.hlsl
    BasicTriangle.VS.dxbc
    BasicTriangle.PS.dxbc
    BasicTriangle.VS.dxil
    BasicTriangle.PS.dxil
    BasicTriangle.VS.spv
    BasicTriangle.PS.spv
    BasicTriangle.VS.metal
    BasicTriangle.PS.metal
    BasicTriangle.VS.reflect.json
    BasicTriangle.PS.reflect.json
    BasicTriangle.veshader.json
)

foreach(artifact IN LISTS expectedArtifacts)
    ve_require_file("${artifact}")
endforeach()

ve_require_magic(BasicTriangle.VS.dxbc "44584243")
ve_require_magic(BasicTriangle.PS.dxbc "44584243")
ve_require_magic(BasicTriangle.VS.dxil "44584243")
ve_require_magic(BasicTriangle.PS.dxil "44584243")
ve_require_magic(BasicTriangle.VS.spv "03022307")
ve_require_magic(BasicTriangle.PS.spv "03022307")

ve_require_text(BasicTriangle.D3D11.hlsl "register\\(b0\\)")
ve_reject_text(BasicTriangle.D3D11.hlsl "space0")

ve_require_text(BasicTriangle.VS.metal "vertex")
ve_require_text(BasicTriangle.PS.metal "fragment")

ve_require_text(BasicTriangle.VS.reflect.json "CameraConstants")
ve_require_text(BasicTriangle.PS.reflect.json "BaseColorTexture")
ve_require_text(BasicTriangle.PS.reflect.json "BaseColorSampler")

ve_require_text(BasicTriangle.veshader.json "\"schemaVersion\": 1")
ve_require_text(BasicTriangle.veshader.json "\"name\": \"BasicTriangle\"")
ve_require_text(BasicTriangle.veshader.json "\"d3d11\"")
ve_require_text(BasicTriangle.veshader.json "\"d3d12\"")
ve_require_text(BasicTriangle.veshader.json "\"spirv\"")
ve_require_text(BasicTriangle.veshader.json "\"metal\"")
ve_require_text(BasicTriangle.veshader.json "\"CameraConstants\"")
ve_require_text(BasicTriangle.veshader.json "\"BaseColorTexture\"")
ve_require_text(BasicTriangle.veshader.json "\"BaseColorSampler\"")
ve_require_text(BasicTriangle.veshader.json "\"bindGroup\": 0")
ve_require_text(BasicTriangle.veshader.json "\"bindGroup\": 1")
