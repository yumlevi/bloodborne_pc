# Copyright (c) 2026 Q2RTX FSR Vulkan contributors
# SPDX-License-Identifier: MIT

if(NOT DEFINED DUMPER OR NOT DEFINED VALIDATOR OR NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "DUMPER, VALIDATOR, and OUTPUT_DIR are required")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
execute_process(
    COMMAND "${DUMPER}" --dump "${OUTPUT_DIR}"
    RESULT_VARIABLE dump_result
    OUTPUT_VARIABLE dump_output
    ERROR_VARIABLE dump_error
)
if(NOT dump_result EQUAL 0)
    message(FATAL_ERROR "shader dumper failed (${dump_result}):\n${dump_output}${dump_error}")
endif()

file(GLOB modules "${OUTPUT_DIR}/*.spv")
list(LENGTH modules module_count)
if(module_count LESS 10)
    message(FATAL_ERROR "shader dumper produced only ${module_count} modules")
endif()

foreach(module IN LISTS modules)
    execute_process(
        COMMAND "${VALIDATOR}" --target-env vulkan1.2 "${module}"
        RESULT_VARIABLE validation_result
        OUTPUT_VARIABLE validation_output
        ERROR_VARIABLE validation_error
    )
    if(NOT validation_result EQUAL 0)
        message(FATAL_ERROR
            "spirv-val failed for ${module} (${validation_result}):\n"
            "${validation_output}${validation_error}")
    endif()
endforeach()

message(STATUS "Validated ${module_count} unique FSR3 upscaler SPIR-V modules")
