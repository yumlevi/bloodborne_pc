cmake_minimum_required(VERSION 3.15)

project(ffx_vulkan_consumer_contract LANGUAGES C)

# Build this sample after installing ffx-vulkan, for example:
# cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/ffx-vulkan
find_package(ffx-vulkan CONFIG REQUIRED)

add_executable(ffx_vk_consumer_contract integration_contract.c)
target_compile_features(ffx_vk_consumer_contract PRIVATE c_std_11)
target_link_libraries(ffx_vk_consumer_contract PRIVATE
    ffx-vulkan::fsr4-v07-vulkan
    ffx-vulkan::framegeneration-presenter-policy)
