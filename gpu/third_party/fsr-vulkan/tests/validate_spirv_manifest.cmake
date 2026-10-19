# Copyright (c) 2026 Q2RTX FSR Vulkan contributors
# SPDX-License-Identifier: MIT

if(NOT DEFINED MANIFEST OR NOT DEFINED BASE_DIR OR NOT DEFINED VALIDATOR)
    message(FATAL_ERROR "MANIFEST, BASE_DIR, and VALIDATOR are required")
endif()

file(STRINGS "${MANIFEST}" lines)
set(checked 0)
foreach(line IN LISTS lines)
    if(line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif()
    if(NOT line MATCHES "^[0-9a-f]+  (.+\\.spv)$")
        message(FATAL_ERROR "manifest entry is not a SPIR-V module: ${line}")
    endif()
    set(module "${BASE_DIR}/${CMAKE_MATCH_1}")
    if(NOT EXISTS "${module}")
        message(FATAL_ERROR "manifest module is missing: ${module}")
    endif()
    execute_process(
        COMMAND "${VALIDATOR}" --target-env vulkan1.2 "${module}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "SPIR-V validation failed for ${module}:\n${output}${error}")
    endif()
    math(EXPR checked "${checked} + 1")
endforeach()

if(NOT DEFINED EXPECTED_COUNT)
    set(EXPECTED_COUNT 11)
endif()
if(NOT checked EQUAL EXPECTED_COUNT)
    message(FATAL_ERROR "expected ${EXPECTED_COUNT} SPIR-V modules, validated ${checked}")
endif()
message(STATUS "Validated ${checked} FSR3.1.5 SPIR-V modules")
