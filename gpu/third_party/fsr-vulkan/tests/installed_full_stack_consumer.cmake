# Verify the exported package from a copied, self-contained consumer example.
# The generated directory is rooted under the known CMake build directory and
# contains only test artefacts, so it is safe to recreate on every CTest run.
foreach(_ffx_vk_required FFX_VK_PACKAGE_SOURCE_DIR FFX_VK_PACKAGE_BUILD_DIR)
    if(NOT DEFINED ${_ffx_vk_required} OR "${${_ffx_vk_required}}" STREQUAL "")
        message(FATAL_ERROR "${_ffx_vk_required} is required")
    endif()
endforeach()

set(_ffx_vk_test_root
    "${FFX_VK_PACKAGE_BUILD_DIR}/installed-full-stack-consumer-test")
set(_ffx_vk_prefix "${_ffx_vk_test_root}/prefix")
set(_ffx_vk_source "${_ffx_vk_test_root}/source")
set(_ffx_vk_build "${_ffx_vk_test_root}/build")

file(REMOVE_RECURSE "${_ffx_vk_test_root}")
file(MAKE_DIRECTORY "${_ffx_vk_test_root}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${FFX_VK_PACKAGE_BUILD_DIR}"
            --prefix "${_ffx_vk_prefix}"
    RESULT_VARIABLE _ffx_vk_install_result
    OUTPUT_VARIABLE _ffx_vk_install_output
    ERROR_VARIABLE _ffx_vk_install_error)
if(NOT _ffx_vk_install_result EQUAL 0)
    message(FATAL_ERROR "ffx-vulkan installation failed:\n${_ffx_vk_install_output}${_ffx_vk_install_error}")
endif()

# A source-only package deliberately has no FSR4-v07 payload. If a distributor
# explicitly opts in, however, the installed path must contain the complete
# coherent set rather than merely enough files to configure the provider.
if(FFX_VK_PACKAGE_EXPECT_FSR4_V07_ASSETS)
    set(_ffx_vk_fsr4_asset_dir "${_ffx_vk_prefix}/share/ffx-vulkan/fsr4-v07")
    if(NOT EXISTS "${_ffx_vk_fsr4_asset_dir}/LICENSE-FSR4-v07.txt")
        message(FATAL_ERROR "installed FSR4-v07 asset notice is missing")
    endif()
    foreach(_ffx_vk_shared_asset
            fsr4_initializers.bin fsr4_pre_weights.bin fsr4_shader_manifest.json
            rcas.spv spd_auto_exposure.spv)
        if(NOT EXISTS "${_ffx_vk_fsr4_asset_dir}/${_ffx_vk_shared_asset}")
            message(FATAL_ERROR
                "installed FSR4-v07 shared asset is missing: ${_ffx_vk_shared_asset}")
        endif()
    endforeach()
    foreach(_ffx_vk_model native quality balanced performance ultraperf drs)
        set(_ffx_vk_model_prefix
            "${_ffx_vk_fsr4_asset_dir}/fsr4_model_v07_i8_${_ffx_vk_model}")
        foreach(_ffx_vk_suffix initializers.bin pre_weights.bin shader_manifest.json)
            if(NOT EXISTS "${_ffx_vk_model_prefix}_${_ffx_vk_suffix}")
                message(FATAL_ERROR
                    "installed FSR4-v07 model asset is missing: "
                    "${_ffx_vk_model_prefix}_${_ffx_vk_suffix}")
            endif()
        endforeach()
        file(GLOB _ffx_vk_model_spirv "${_ffx_vk_model_prefix}_*.spv")
        if(NOT _ffx_vk_model_spirv)
            message(FATAL_ERROR
                "installed FSR4-v07 model SPIR-V is missing: ${_ffx_vk_model}")
        endif()
    endforeach()
    file(GLOB _ffx_vk_installed_fsr4_files "${_ffx_vk_fsr4_asset_dir}/*")
    list(LENGTH _ffx_vk_installed_fsr4_files _ffx_vk_installed_fsr4_file_count)
    if(NOT _ffx_vk_installed_fsr4_file_count EQUAL 288)
        message(FATAL_ERROR
            "installed FSR4-v07 file count ${_ffx_vk_installed_fsr4_file_count}, expected 288")
    endif()
endif()

file(COPY "${FFX_VK_PACKAGE_SOURCE_DIR}/examples/installed-full-stack/"
    DESTINATION "${_ffx_vk_source}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_ffx_vk_source}" -B "${_ffx_vk_build}"
            "-DCMAKE_PREFIX_PATH=${_ffx_vk_prefix}"
    RESULT_VARIABLE _ffx_vk_configure_result
    OUTPUT_VARIABLE _ffx_vk_configure_output
    ERROR_VARIABLE _ffx_vk_configure_error)
if(NOT _ffx_vk_configure_result EQUAL 0)
    message(FATAL_ERROR "installed consumer configure failed:\n${_ffx_vk_configure_output}${_ffx_vk_configure_error}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_ffx_vk_build}"
    RESULT_VARIABLE _ffx_vk_build_result
    OUTPUT_VARIABLE _ffx_vk_build_output
    ERROR_VARIABLE _ffx_vk_build_error)
if(NOT _ffx_vk_build_result EQUAL 0)
    message(FATAL_ERROR "installed consumer build failed:\n${_ffx_vk_build_output}${_ffx_vk_build_error}")
endif()

execute_process(
    COMMAND "${_ffx_vk_build}/ffx_vk_installed_full_stack_contract"
    RESULT_VARIABLE _ffx_vk_run_result
    OUTPUT_VARIABLE _ffx_vk_run_output
    ERROR_VARIABLE _ffx_vk_run_error)
if(NOT _ffx_vk_run_result EQUAL 0)
    message(FATAL_ERROR "installed consumer failed:\n${_ffx_vk_run_output}${_ffx_vk_run_error}")
endif()
