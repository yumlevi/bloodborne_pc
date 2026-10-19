cmake_minimum_required(VERSION 3.15)

project(ffx_vulkan_full_stack_contract LANGUAGES C CXX)

# This sample is deliberately a vendored-project integration, rather than an
# installed-package example: the current dependency-complete package exports
# FSR4 and WSI policy, whereas FSR3 retains its pinned SDK source closure.
set(FFX_VK_PORTABLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FFX_VK_PORTABLE_BUILD_PROBE OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_BINARY_DIR}/ffx-vulkan")

add_executable(ffx_vk_full_stack_contract integration_contract.cpp)
target_compile_features(ffx_vk_full_stack_contract PRIVATE cxx_std_20)
target_link_libraries(ffx_vk_full_stack_contract PRIVATE ffx-vulkan::effects)
