cmake_minimum_required(VERSION 3.15)

project(ffx_vulkan_installed_full_stack_contract LANGUAGES CXX)

# Build this sample after installing ffx-vulkan, for example:
# cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/ffx-vulkan
find_package(ffx-vulkan CONFIG REQUIRED)

add_executable(ffx_vk_installed_full_stack_contract
    integration_contract.cpp)
target_compile_features(ffx_vk_installed_full_stack_contract PRIVATE cxx_std_20)
target_link_libraries(ffx_vk_installed_full_stack_contract PRIVATE
    ffx-vulkan::effects)
