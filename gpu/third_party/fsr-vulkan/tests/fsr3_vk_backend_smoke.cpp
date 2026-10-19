/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#if !defined(FFX_VK_PORTABLE_ONLY)
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#endif

#include "ffx_vk_portable.h"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <vector>

namespace {

struct ValidationState {
    std::atomic_uint errors{0};
    std::atomic_uint warnings{0};
};

std::atomic_uint khrMemoryRequirements2Queries{0};
std::atomic_uint coreMemoryRequirements2Queries{0};
std::atomic_uint auditedAllocationCount{0};
std::atomic_uint forbiddenDeviceCoherentAllocationCount{0};
std::atomic_uint auditedComputePipelineCount{0};
std::atomic_uint forbiddenRequiredSubgroupSizeCount{0};
std::atomic_uint disabledOptionalProcedureQueryCount{0};
VkPhysicalDevice allocationAuditPhysicalDevice = VK_NULL_HANDLE;

VKAPI_ATTR VkResult VKAPI_CALL auditedAllocateMemory(
    VkDevice device,
    const VkMemoryAllocateInfo* allocationInfo,
    const VkAllocationCallbacks* allocationCallbacks,
    VkDeviceMemory* memory)
{
    ++auditedAllocationCount;
    if (allocationAuditPhysicalDevice != VK_NULL_HANDLE && allocationInfo) {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(allocationAuditPhysicalDevice, &properties);
        if (allocationInfo->memoryTypeIndex < properties.memoryTypeCount &&
            (properties.memoryTypes[allocationInfo->memoryTypeIndex].propertyFlags &
             VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD)) {
            ++forbiddenDeviceCoherentAllocationCount;
        }
    }
    return vkAllocateMemory(device, allocationInfo, allocationCallbacks, memory);
}

VKAPI_ATTR VkResult VKAPI_CALL auditedCreateComputePipelines(
    VkDevice device,
    VkPipelineCache pipelineCache,
    uint32_t createInfoCount,
    const VkComputePipelineCreateInfo* createInfos,
    const VkAllocationCallbacks* allocationCallbacks,
    VkPipeline* pipelines)
{
    auditedComputePipelineCount += createInfoCount;
    for (uint32_t index = 0; index < createInfoCount; ++index) {
        const VkBaseInStructure* extension =
            static_cast<const VkBaseInStructure*>(createInfos[index].stage.pNext);
        while (extension) {
            if (extension->sType ==
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO) {
                ++forbiddenRequiredSubgroupSizeCount;
            }
            extension = extension->pNext;
        }
    }
    return vkCreateComputePipelines(
        device, pipelineCache, createInfoCount, createInfos, allocationCallbacks, pipelines);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL coreOnlyGetDeviceProcAddr(
    VkDevice device,
    const char* name)
{
    if (name && std::strcmp(name, "vkGetBufferMemoryRequirements2KHR") == 0) {
        ++khrMemoryRequirements2Queries;
        return nullptr;
    }
    if (name && std::strcmp(name, "vkGetBufferMemoryRequirements2") == 0)
        ++coreMemoryRequirements2Queries;
    if (name && std::strcmp(name, "vkAllocateMemory") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(auditedAllocateMemory);
    if (name && std::strcmp(name, "vkCreateComputePipelines") == 0)
        return reinterpret_cast<PFN_vkVoidFunction>(auditedCreateComputePipelines);
    if (name &&
        (std::strcmp(name, "vkSetDebugUtilsObjectNameEXT") == 0 ||
         std::strcmp(name, "vkCmdBeginDebugUtilsLabelEXT") == 0 ||
         std::strcmp(name, "vkCmdEndDebugUtilsLabelEXT") == 0 ||
         std::strcmp(name, "vkCmdWriteBufferMarkerAMD") == 0 ||
         std::strcmp(name, "vkCmdWriteBufferMarker2AMD") == 0)) {
        ++disabledOptionalProcedureQueryCount;
    }
    return vkGetDeviceProcAddr(device, name);
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void* userData)
{
    auto* state = static_cast<ValidationState*>(userData);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++state->errors;
    }
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        ++state->warnings;
    }
    if (severity & (VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)) {
        std::fprintf(stderr, "Vulkan validation: %s\n", data && data->pMessage ? data->pMessage : "(no message)");
    }
    return VK_FALSE;
}

bool hasLayer(const char* name)
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(layers.begin(), layers.end(), [name](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, name) == 0;
    });
}

bool hasInstanceExtension(const char* name)
{
    std::uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) {
        return false;
    }
    std::vector<VkExtensionProperties> extensions(count);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()) != VK_SUCCESS) {
        return false;
    }
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

bool hasDeviceExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

struct PhysicalDeviceChoice {
    VkPhysicalDevice device = VK_NULL_HANDLE;
    std::uint32_t queueFamily = UINT32_MAX;
    int score = -1;
};

PhysicalDeviceChoice choosePhysicalDevice(VkInstance instance)
{
    std::uint32_t deviceCount = 0;
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr) != VK_SUCCESS || deviceCount == 0) {
        return {};
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()) != VK_SUCCESS) {
        return {};
    }

    PhysicalDeviceChoice best;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (VK_API_VERSION_MAJOR(properties.apiVersion) < 1 ||
            (VK_API_VERSION_MAJOR(properties.apiVersion) == 1 && VK_API_VERSION_MINOR(properties.apiVersion) < 3)) {
            continue;
        }
        FfxVkPortableDeviceCapabilities capabilities{};
        capabilities.structSize = sizeof(capabilities);
        if (ffxVkPortableQueryDeviceCapabilities(device, &capabilities) != FFX_VK_PORTABLE_OK ||
            !capabilities.fsr3ComputePrerequisites) {
            continue;
        }

        std::uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());
        for (std::uint32_t i = 0; i < queueCount; ++i) {
            if (queues[i].queueCount == 0 || !(queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                continue;
            }
            int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 100 : 10;
            if (!(queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                ++score;
            }
            if (score > best.score) {
                best = {device, i, score};
            }
        }
    }
    return best;
}

std::uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                             std::uint32_t allowedTypes,
                             VkMemoryPropertyFlags required,
                             VkMemoryPropertyFlags preferred = 0)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    std::uint32_t fallback = UINT32_MAX;
    for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        if (!(allowedTypes & (1u << index))) {
            continue;
        }
        const VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[index].propertyFlags;
        // This smoke device deliberately leaves deviceCoherentMemory disabled.
        // Keep harness allocations legal so the audit isolates backend choices.
        if (flags & VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD) {
            continue;
        }
        if ((flags & required) != required) {
            continue;
        }
        if ((flags & preferred) == preferred) {
            return index;
        }
        if (fallback == UINT32_MAX) {
            fallback = index;
        }
    }
    return fallback;
}

struct TestImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
};

bool createImage(VkPhysicalDevice physicalDevice,
                 VkDevice device,
                 VkFormat format,
                 std::uint32_t width,
                 std::uint32_t height,
                 VkImageUsageFlags usage,
                 TestImage* output)
{
    output->createInfo.imageType = VK_IMAGE_TYPE_2D;
    output->createInfo.format = format;
    output->createInfo.extent = {width, height, 1};
    output->createInfo.mipLevels = 1;
    output->createInfo.arrayLayers = 1;
    output->createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    output->createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    output->createInfo.usage = usage;
    output->createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    output->createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &output->createInfo, nullptr, &output->image) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, output->image, &requirements);
    const std::uint32_t memoryType = findMemoryType(
        physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == UINT32_MAX) {
        vkDestroyImage(device, output->image, nullptr);
        output->image = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device, &allocation, nullptr, &output->memory) != VK_SUCCESS) {
        vkDestroyImage(device, output->image, nullptr);
        output->image = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindImageMemory(device, output->image, output->memory, 0) != VK_SUCCESS) {
        vkFreeMemory(device, output->memory, nullptr);
        vkDestroyImage(device, output->image, nullptr);
        output->memory = VK_NULL_HANDLE;
        output->image = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

struct ReadbackBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize allocationSize = 0;
    VkMemoryPropertyFlags memoryProperties = 0;
};

bool createReadbackBuffer(VkPhysicalDevice physicalDevice,
                          VkDevice device,
                          VkDeviceSize size,
                          ReadbackBuffer* output)
{
    VkBufferCreateInfo createInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    createInfo.size = size;
    createInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &createInfo, nullptr, &output->buffer) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, output->buffer, &requirements);
    const std::uint32_t memoryType = findMemoryType(
        physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (memoryType == UINT32_MAX) {
        vkDestroyBuffer(device, output->buffer, nullptr);
        output->buffer = VK_NULL_HANDLE;
        return false;
    }
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
    output->memoryProperties = memoryProperties.memoryTypes[memoryType].propertyFlags;
    output->allocationSize = requirements.size;

    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(device, &allocation, nullptr, &output->memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, output->buffer, nullptr);
        output->buffer = VK_NULL_HANDLE;
        return false;
    }
    if (vkBindBufferMemory(device, output->buffer, output->memory, 0) != VK_SUCCESS) {
        vkFreeMemory(device, output->memory, nullptr);
        vkDestroyBuffer(device, output->buffer, nullptr);
        output->memory = VK_NULL_HANDLE;
        output->buffer = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void destroyImage(VkDevice device, TestImage* image)
{
    if (image->image) vkDestroyImage(device, image->image, nullptr);
    if (image->memory) vkFreeMemory(device, image->memory, nullptr);
    *image = {};
}

void destroyReadbackBuffer(VkDevice device, ReadbackBuffer* buffer)
{
    if (buffer->buffer) vkDestroyBuffer(device, buffer->buffer, nullptr);
    if (buffer->memory) vkFreeMemory(device, buffer->memory, nullptr);
    *buffer = {};
}

VkImageMemoryBarrier imageBarrier(VkImage image,
                                  VkImageLayout oldLayout,
                                  VkImageLayout newLayout,
                                  VkAccessFlags sourceAccess,
                                  VkAccessFlags destinationAccess)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = sourceAccess;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    return barrier;
}

#if !defined(FFX_VK_PORTABLE_ONLY)
FfxResource asFfxResource(const TestImage& image,
                          const wchar_t* name,
                          FfxResourceStates state)
{
    return ffxGetResourceVK(reinterpret_cast<void*>(image.image),
                            ffxGetImageResourceDescriptionVK(image.image, image.createInfo),
                            name, state);
}
#endif

FfxVkPortableImage asPortableImage(const TestImage& image,
                                   FfxVkPortableResourceState state)
{
    FfxVkPortableImage resource{};
    resource.structSize = sizeof(resource);
    resource.image = image.image;
    resource.format = image.createInfo.format;
    resource.extent = {image.createInfo.extent.width, image.createInfo.extent.height};
    resource.mipCount = image.createInfo.mipLevels;
    resource.arrayLayers = image.createInfo.arrayLayers;
    resource.usage = image.createInfo.usage;
    resource.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    resource.state = state;
    return resource;
}

std::uint64_t fnv1a64(const void* data, std::size_t size)
{
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

#if !defined(FFX_VK_PORTABLE_ONLY)
bool matchesSharedResourceDescription(const FfxCreateResourceDescription& create,
                                      FfxSurfaceFormat format,
                                      FfxResourceUsage usage)
{
    const FfxResourceDescription& resource = create.resourceDescription;
    return create.heapType == FFX_HEAP_TYPE_DEFAULT &&
           create.initialState == FFX_RESOURCE_STATE_UNORDERED_ACCESS &&
           resource.type == FFX_RESOURCE_TYPE_TEXTURE2D &&
           resource.format == format &&
           resource.width == 64 && resource.height == 64 && resource.depth == 1 &&
           resource.mipCount == 1 && resource.flags == FFX_RESOURCE_FLAGS_NONE &&
           resource.usage == usage;
}

FfxCreatePipelineFunc originalCreatePipeline = nullptr;
FfxCreateResourceFunc originalCreateResource = nullptr;
std::uint32_t pipelinesCreated = 0;
std::uint32_t resourcesCreated = 0;

FfxErrorCode countCreatePipeline(FfxInterface* backendInterface,
                                 FfxEffect effect,
                                 FfxPass pass,
                                 std::uint32_t permutationOptions,
                                 const FfxPipelineDescription* description,
                                 FfxUInt32 contextId,
                                 FfxPipelineState* output)
{
    const FfxErrorCode result = originalCreatePipeline(backendInterface, effect, pass, permutationOptions,
                                                        description, contextId, output);
    if (result == FFX_OK) {
        ++pipelinesCreated;
    }
    return result;
}

FfxErrorCode countCreateResource(FfxInterface* backendInterface,
                                 const FfxCreateResourceDescription* description,
                                 FfxUInt32 contextId,
                                 FfxResourceInternal* output)
{
    const FfxErrorCode result = originalCreateResource(backendInterface, description, contextId, output);
    if (result == FFX_OK) {
        ++resourcesCreated;
    }
    return result;
}

void ffxMessage(FfxMsgType type, const wchar_t* message)
{
    std::fwprintf(stderr, type == FFX_MESSAGE_TYPE_ERROR ? L"FSR3 error: %ls\n" : L"FSR3 warning: %ls\n",
                  message ? message : L"(no message)");
}
#endif

bool runDispatchAndReadback(VkPhysicalDevice physicalDevice,
                            VkDevice device,
                            std::uint32_t queueFamily,
                            void* amdContext,
                            FfxVkPortableUpscaleContext* portableContext,
                            std::array<TestImage, 7>* retainedImages,
                            std::uint64_t* outputHash,
                            std::size_t* nonzeroColorComponents,
                            std::uint64_t frameId,
                            bool reset)
{
#if defined(FFX_VK_PORTABLE_ONLY)
    (void)amdContext;
#endif
    constexpr std::uint32_t renderWidth = 64;
    constexpr std::uint32_t renderHeight = 64;
    constexpr std::uint32_t outputWidth = 128;
    constexpr std::uint32_t outputHeight = 128;
    constexpr VkImageUsageFlags inputUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    constexpr VkImageUsageFlags storageUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    constexpr VkImageUsageFlags sharedRenderTargetUsage =
        storageUsage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    constexpr VkDeviceSize outputBytes =
        VkDeviceSize{outputWidth} * outputHeight * 4 * sizeof(std::uint16_t);

    enum ImageIndex : std::size_t {
        Color,
        Depth,
        MotionVectors,
        Output,
        DilatedDepth,
        DilatedMotionVectors,
        ReconstructedPreviousDepth,
        ImageCount,
    };
    static_assert(ImageCount == 7);
    std::array<TestImage, ImageCount>& images = *retainedImages;
    ReadbackBuffer readback{};
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    auto cleanup = [&] {
        if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
        destroyReadbackBuffer(device, &readback);
    };

    const bool imagesCreated =
        createImage(physicalDevice, device, VK_FORMAT_R16G16B16A16_SFLOAT,
                    renderWidth, renderHeight, inputUsage, &images[Color]) &&
        createImage(physicalDevice, device, VK_FORMAT_R32_SFLOAT,
                    renderWidth, renderHeight, inputUsage, &images[Depth]) &&
        createImage(physicalDevice, device, VK_FORMAT_R16G16_SFLOAT,
                    renderWidth, renderHeight, inputUsage, &images[MotionVectors]) &&
        createImage(physicalDevice, device, VK_FORMAT_R16G16B16A16_SFLOAT,
                    outputWidth, outputHeight, storageUsage, &images[Output]) &&
        createImage(physicalDevice, device, VK_FORMAT_R32_SFLOAT,
                    renderWidth, renderHeight, sharedRenderTargetUsage, &images[DilatedDepth]) &&
        createImage(physicalDevice, device, VK_FORMAT_R16G16_SFLOAT,
                    renderWidth, renderHeight, sharedRenderTargetUsage, &images[DilatedMotionVectors]) &&
        createImage(physicalDevice, device, VK_FORMAT_R32_UINT,
                    renderWidth, renderHeight, storageUsage, &images[ReconstructedPreviousDepth]);
    if (!imagesCreated || !createReadbackBuffer(physicalDevice, device, outputBytes, &readback)) {
        std::fprintf(stderr, "could not allocate dispatch test resources\n");
        cleanup();
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        std::fprintf(stderr, "could not create dispatch command pool\n");
        cleanup();
        return false;
    }
    VkCommandBufferAllocateInfo commandInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &commandInfo, &commandBuffer) != VK_SUCCESS) {
        std::fprintf(stderr, "could not allocate dispatch command buffer\n");
        cleanup();
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        std::fprintf(stderr, "could not begin dispatch command buffer\n");
        cleanup();
        return false;
    }

    std::array<VkImageMemoryBarrier, ImageCount> initializeBarriers{};
    for (std::size_t index = 0; index < images.size(); ++index) {
        initializeBarriers[index] = imageBarrier(
            images[index].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0, VK_ACCESS_TRANSFER_WRITE_BIT);
    }
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<std::uint32_t>(initializeBarriers.size()), initializeBarriers.data());

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    VkClearColorValue clear{};
    clear.float32[0] = 0.25f;
    clear.float32[1] = 0.5f;
    clear.float32[2] = 0.75f;
    clear.float32[3] = 1.0f;
    vkCmdClearColorImage(commandBuffer, images[Color].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &clear, 1, &range);
    clear = {};
    clear.float32[0] = 0.5f;
    vkCmdClearColorImage(commandBuffer, images[Depth].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &clear, 1, &range);
    clear = {};
    vkCmdClearColorImage(commandBuffer, images[MotionVectors].image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
    clear = {};
    // Use an exactly representable, finite half-float poison value.  A clear
    // color for an SFLOAT image is specified through float32 (not uint32), and
    // requiring every component to change proves the dispatch covered the
    // complete output extent.
    clear.float32[0] = -1.0f;
    clear.float32[1] = -1.0f;
    clear.float32[2] = -1.0f;
    clear.float32[3] = -1.0f;
    vkCmdClearColorImage(commandBuffer, images[Output].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &clear, 1, &range);
    clear = {};
    for (std::size_t index = DilatedDepth; index < images.size(); ++index) {
        vkCmdClearColorImage(commandBuffer, images[index].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clear, 1, &range);
    }

    std::array<VkImageMemoryBarrier, ImageCount> prepareBarriers{};
    for (std::size_t index = 0; index < images.size(); ++index) {
        const bool input = index <= MotionVectors;
        prepareBarriers[index] = imageBarrier(
            images[index].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            input ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            input ? VK_ACCESS_SHADER_READ_BIT : (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT));
    }
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<std::uint32_t>(prepareBarriers.size()), prepareBarriers.data());

    if (portableContext) {
        FfxVkPortableUpscaleDispatchInfo dispatch{};
        dispatch.structSize = sizeof(dispatch);
        dispatch.commandBuffer = commandBuffer;
        dispatch.color = asPortableImage(
            images[Color], FFX_VK_PORTABLE_RESOURCE_STATE_COMPUTE_READ);
        dispatch.depth = asPortableImage(
            images[Depth], FFX_VK_PORTABLE_RESOURCE_STATE_COMPUTE_READ);
        dispatch.motionVectors = asPortableImage(
            images[MotionVectors], FFX_VK_PORTABLE_RESOURCE_STATE_COMPUTE_READ);
        dispatch.exposure.structSize = sizeof(dispatch.exposure);
        dispatch.reactiveMask.structSize = sizeof(dispatch.reactiveMask);
        dispatch.transparencyAndCompositionMask.structSize =
            sizeof(dispatch.transparencyAndCompositionMask);
        dispatch.output = asPortableImage(
            images[Output], FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.motionVectorScale = {
            static_cast<float>(renderWidth), static_cast<float>(renderHeight)};
        dispatch.renderSize = {renderWidth, renderHeight};
        dispatch.outputSize = {outputWidth, outputHeight};
        dispatch.frameTimeMilliseconds = 16.6667f;
        dispatch.preExposure = 1.0f;
        dispatch.reset = reset ? VK_TRUE : VK_FALSE;
        dispatch.cameraNear = 0.1f;
        dispatch.cameraFar = 1000.0f;
        dispatch.cameraVerticalFovRadians = 1.0f;
        dispatch.viewSpaceToMeters = 1.0f;
        dispatch.frameId = frameId;
        const FfxVkPortableResult dispatchResult =
            ffxVkPortableUpscaleContextRecordDispatch(portableContext, &dispatch);
        if (dispatchResult != FFX_VK_PORTABLE_OK) {
            std::fprintf(stderr, "portable FSR3 record dispatch failed: %d\n",
                         static_cast<int>(dispatchResult));
            vkEndCommandBuffer(commandBuffer);
            cleanup();
            return false;
        }
    }
#if !defined(FFX_VK_PORTABLE_ONLY)
    else {
        FfxFsr3UpscalerDispatchDescription dispatch{};
        dispatch.commandList = ffxGetCommandListVK(commandBuffer);
        dispatch.color = asFfxResource(images[Color], L"FSR3 test color", FFX_RESOURCE_STATE_COMPUTE_READ);
        dispatch.depth = asFfxResource(images[Depth], L"FSR3 test depth", FFX_RESOURCE_STATE_COMPUTE_READ);
        dispatch.motionVectors = asFfxResource(
            images[MotionVectors], L"FSR3 test motion vectors", FFX_RESOURCE_STATE_COMPUTE_READ);
        dispatch.output = asFfxResource(images[Output], L"FSR3 test output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.dilatedDepth = asFfxResource(
            images[DilatedDepth], L"FSR3 test dilated depth", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.dilatedMotionVectors = asFfxResource(
            images[DilatedMotionVectors], L"FSR3 test dilated motion vectors", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.reconstructedPrevNearestDepth = asFfxResource(
            images[ReconstructedPreviousDepth], L"FSR3 test reconstructed depth", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.motionVectorScale = {static_cast<float>(renderWidth), static_cast<float>(renderHeight)};
        dispatch.renderSize = {renderWidth, renderHeight};
        dispatch.upscaleSize = {outputWidth, outputHeight};
        dispatch.frameTimeDelta = 16.6667f;
        dispatch.preExposure = 1.0f;
        dispatch.reset = reset;
        dispatch.cameraNear = 0.1f;
        dispatch.cameraFar = 1000.0f;
        dispatch.cameraFovAngleVertical = 1.0f;
        dispatch.viewSpaceToMetersFactor = 1.0f;
        const FfxErrorCode dispatchResult = ffxFsr3UpscalerContextDispatch(
            static_cast<FfxFsr3UpscalerContext*>(amdContext), &dispatch);
        if (dispatchResult != FFX_OK) {
            std::fprintf(stderr, "ffxFsr3UpscalerContextDispatch failed: %d\n",
                         static_cast<int>(dispatchResult));
            vkEndCommandBuffer(commandBuffer);
            cleanup();
            return false;
        }
    }
#else
    else {
        std::fprintf(stderr, "portable API smoke test received no portable context\n");
        vkEndCommandBuffer(commandBuffer);
        cleanup();
        return false;
    }
#endif

    VkImageMemoryBarrier copyBarrier = imageBarrier(
        images[Output].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         1, &copyBarrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {outputWidth, outputHeight, 1};
    vkCmdCopyImageToBuffer(commandBuffer, images[Output].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback.buffer, 1, &copy);

    VkBufferMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = readback.buffer;
    hostBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1, &hostBarrier, 0, nullptr);
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        std::fprintf(stderr, "could not end dispatch command buffer\n");
        cleanup();
        return false;
    }

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        vkQueueWaitIdle(queue) != VK_SUCCESS) {
        std::fprintf(stderr, "dispatch submission failed\n");
        cleanup();
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(device, readback.memory, 0, readback.allocationSize, 0, &mapped) != VK_SUCCESS) {
        std::fprintf(stderr, "could not map dispatch readback\n");
        cleanup();
        return false;
    }
    if (!(readback.memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange mappedRange{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        mappedRange.memory = readback.memory;
        mappedRange.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(device, 1, &mappedRange);
    }

    const auto* components = static_cast<const std::uint16_t*>(mapped);
    constexpr std::size_t componentCount = outputBytes / sizeof(std::uint16_t);
    bool finite = true;
    bool fullyOverwritten = true;
    bool alphaIsOne = true;
    std::size_t colored = 0;
    for (std::size_t index = 0; index < componentCount; ++index) {
        if ((components[index] & UINT16_C(0x7c00)) == UINT16_C(0x7c00)) {
            finite = false;
            break;
        }
        if ((index % 4) < 3 && (components[index] & UINT16_C(0x7fff)) != 0) {
            ++colored;
        }
        if (components[index] == UINT16_C(0xbc00)) {
            fullyOverwritten = false;
        }
        if ((index % 4) == 3 && components[index] != UINT16_C(0x3c00)) {
            alphaIsOne = false;
        }
    }
    *outputHash = fnv1a64(mapped, outputBytes);
    *nonzeroColorComponents = colored;
    vkUnmapMemory(device, readback.memory);
    cleanup();

    if (!finite || colored == 0 || !fullyOverwritten || !alphaIsOne) {
        std::fprintf(stderr, "dispatch output was non-finite, unchanged, incomplete, or all-zero\n");
        return false;
    }
    return true;
}

/*
 * This deliberately uses only the public portable API.  In particular, it
 * proves that the independently-owned Optical Flow and Frame Interpolation
 * backend contexts can record into one command buffer, retain their temporal
 * state across real frames, and write every output pixel.  A presenter is
 * intentionally outside this test: it needs a real swapchain/acquire policy.
 */
bool runFrameGenerationAndReadback(VkPhysicalDevice physicalDevice,
                                   VkDevice device,
                                   std::uint32_t queueFamily,
                                   FfxVkPortableFrameGenerationContext* context,
                                   std::uint64_t frameId,
                                   bool reset,
                                   std::uint64_t* outputHash,
                                   std::size_t* nonzeroColorComponents)
{
    constexpr std::uint32_t renderWidth = 64;
    constexpr std::uint32_t renderHeight = 64;
    constexpr std::uint32_t displayWidth = 128;
    constexpr std::uint32_t displayHeight = 128;
    constexpr VkImageUsageFlags inputUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    constexpr VkImageUsageFlags outputUsage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    constexpr VkDeviceSize outputBytes =
        VkDeviceSize{displayWidth} * displayHeight * 4 * sizeof(std::uint16_t);

    std::array<TestImage, 4> images{};
    ReadbackBuffer readback{};
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    auto cleanup = [&] {
        if (pool) vkDestroyCommandPool(device, pool, nullptr);
        destroyReadbackBuffer(device, &readback);
        for (TestImage& image : images) destroyImage(device, &image);
    };
    if (!createImage(physicalDevice, device, VK_FORMAT_R16G16B16A16_SFLOAT,
                     displayWidth, displayHeight, inputUsage, &images[0]) ||
        !createImage(physicalDevice, device, VK_FORMAT_R32_SFLOAT,
                     renderWidth, renderHeight, inputUsage, &images[1]) ||
        !createImage(physicalDevice, device, VK_FORMAT_R16G16_SFLOAT,
                     renderWidth, renderHeight, inputUsage, &images[2]) ||
        !createImage(physicalDevice, device, VK_FORMAT_R16G16B16A16_SFLOAT,
                     displayWidth, displayHeight, outputUsage, &images[3]) ||
        !createReadbackBuffer(physicalDevice, device, outputBytes, &readback)) {
        std::fprintf(stderr, "could not allocate FSR3 frame-generation test resources\n");
        cleanup();
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        std::fprintf(stderr, "could not allocate FSR3 frame-generation command resources\n");
        cleanup();
        return false;
    }
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &allocation, &commandBuffer) != VK_SUCCESS) {
        std::fprintf(stderr, "could not allocate FSR3 frame-generation command resources\n");
        cleanup();
        return false;
    }
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &begin) != VK_SUCCESS) {
        cleanup();
        return false;
    }

    std::array<VkImageMemoryBarrier, 4> initialize{};
    for (std::size_t index = 0; index < images.size(); ++index) {
        initialize[index] = imageBarrier(images[index].image, VK_IMAGE_LAYOUT_UNDEFINED,
                                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                                         VK_ACCESS_TRANSFER_WRITE_BIT);
    }
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<std::uint32_t>(initialize.size()), initialize.data());
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;
    VkClearColorValue color{};
    color.float32[0] = frameId == 1 ? 0.2f : 0.7f;
    color.float32[1] = 0.4f;
    color.float32[2] = frameId == 1 ? 0.8f : 0.1f;
    color.float32[3] = 1.0f;
    vkCmdClearColorImage(commandBuffer, images[0].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &color, 1, &range);
    color = {};
    color.float32[0] = 0.5f;
    vkCmdClearColorImage(commandBuffer, images[1].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &color, 1, &range);
    color = {};
    vkCmdClearColorImage(commandBuffer, images[2].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &color, 1, &range);
    color = {};
    color.float32[0] = color.float32[1] = color.float32[2] = color.float32[3] = -1.0f;
    vkCmdClearColorImage(commandBuffer, images[3].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &color, 1, &range);

    std::array<VkImageMemoryBarrier, 4> ready{};
    for (std::size_t index = 0; index < images.size(); ++index) {
        const bool input = index < 3;
        ready[index] = imageBarrier(images[index].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    input ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
                                    VK_ACCESS_TRANSFER_WRITE_BIT,
                                    input ? VK_ACCESS_SHADER_READ_BIT : VK_ACCESS_SHADER_WRITE_BIT);
    }
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<std::uint32_t>(ready.size()), ready.data());

    FfxVkPortableFrameGenerationPrepareInfo prepare{};
    prepare.structSize = sizeof(prepare);
    prepare.commandBuffer = commandBuffer;
    prepare.depth = asPortableImage(images[1], FFX_VK_PORTABLE_RESOURCE_STATE_COMPUTE_READ);
    prepare.motionVectors = asPortableImage(images[2], FFX_VK_PORTABLE_RESOURCE_STATE_COMPUTE_READ);
    prepare.renderSize = {renderWidth, renderHeight};
    prepare.motionVectorScale = {static_cast<float>(renderWidth), static_cast<float>(renderHeight)};
    prepare.frameTimeMilliseconds = 16.6667f;
    prepare.cameraNear = 0.1f;
    prepare.cameraFar = 1000.0f;
    prepare.cameraVerticalFovRadians = 1.0f;
    prepare.viewSpaceToMeters = 1.0f;
    prepare.minLuminance = 0.0f;
    prepare.maxLuminance = 1000.0f;
    prepare.transferFunction = FFX_VK_PORTABLE_TRANSFER_FUNCTION_SCRGB;
    prepare.cameraUp = {0.0f, 1.0f, 0.0f};
    prepare.cameraRight = {1.0f, 0.0f, 0.0f};
    prepare.cameraForward = {0.0f, 0.0f, 1.0f};
    prepare.reset = reset ? VK_TRUE : VK_FALSE;
    prepare.frameId = frameId;
    FfxVkPortableImage source = asPortableImage(images[0], FFX_VK_PORTABLE_RESOURCE_STATE_COMPUTE_READ);
    if (ffxVkPortableFrameGenerationContextPrepare(context, &prepare, &source) != FFX_VK_PORTABLE_OK) {
        std::fprintf(stderr, "portable FSR3 frame-generation prepare failed\n");
        vkEndCommandBuffer(commandBuffer);
        cleanup();
        return false;
    }

    FfxVkPortableFrameGenerationDispatchInfo dispatch{};
    dispatch.structSize = sizeof(dispatch);
    dispatch.commandBuffer = commandBuffer;
    dispatch.currentColor = source;
    dispatch.hudlessColor.structSize = sizeof(dispatch.hudlessColor);
    dispatch.distortionField.structSize = sizeof(dispatch.distortionField);
    dispatch.output = asPortableImage(images[3], FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatch.displaySize = {displayWidth, displayHeight};
    dispatch.interpolationRect = {0, 0, displayWidth, displayHeight};
    dispatch.frameTimeMilliseconds = prepare.frameTimeMilliseconds;
    dispatch.cameraNear = prepare.cameraNear;
    dispatch.cameraFar = prepare.cameraFar;
    dispatch.cameraVerticalFovRadians = prepare.cameraVerticalFovRadians;
    dispatch.viewSpaceToMeters = prepare.viewSpaceToMeters;
    dispatch.minLuminance = prepare.minLuminance;
    dispatch.maxLuminance = prepare.maxLuminance;
    dispatch.transferFunction = prepare.transferFunction;
    dispatch.reset = prepare.reset;
    dispatch.frameId = prepare.frameId;
    if (ffxVkPortableFrameGenerationContextRecordDispatch(context, &dispatch) != FFX_VK_PORTABLE_OK) {
        std::fprintf(stderr, "portable FSR3 frame-generation dispatch failed\n");
        vkEndCommandBuffer(commandBuffer);
        cleanup();
        return false;
    }

    VkImageMemoryBarrier copyBarrier = imageBarrier(
        images[3].image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &copyBarrier);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {displayWidth, displayHeight, 1};
    vkCmdCopyImageToBuffer(commandBuffer, images[3].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback.buffer, 1, &copy);
    VkBufferMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    hostBarrier.buffer = readback.buffer;
    hostBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, 1, &hostBarrier, 0, nullptr);
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        cleanup();
        return false;
    }
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS ||
        vkQueueWaitIdle(queue) != VK_SUCCESS) {
        cleanup();
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(device, readback.memory, 0, readback.allocationSize, 0, &mapped) != VK_SUCCESS) {
        cleanup();
        return false;
    }
    if (!(readback.memoryProperties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        VkMappedMemoryRange mappedRange{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        mappedRange.memory = readback.memory;
        mappedRange.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(device, 1, &mappedRange);
    }
    const auto* components = static_cast<const std::uint16_t*>(mapped);
    bool finite = true;
    bool fullyOverwritten = true;
    std::size_t colored = 0;
    constexpr std::size_t componentCount = outputBytes / sizeof(std::uint16_t);
    for (std::size_t index = 0; index < componentCount; ++index) {
        finite &= (components[index] & UINT16_C(0x7c00)) != UINT16_C(0x7c00);
        fullyOverwritten &= components[index] != UINT16_C(0xbc00);
        if ((index & 3u) != 3u && components[index] != 0)
            ++colored;
    }
    *outputHash = fnv1a64(mapped, static_cast<std::size_t>(outputBytes));
    *nonzeroColorComponents = colored;
    vkUnmapMemory(device, readback.memory);
    cleanup();
    if (!finite || !fullyOverwritten || colored == 0) {
        std::fprintf(stderr, "FSR3 frame-generation output was invalid: finite=%d overwrite=%d color=%zu\n",
                     finite, fullyOverwritten, colored);
        return false;
    }
    return true;
}

} // namespace

int main()
{
    constexpr const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    ValidationState validation;
    const bool enableValidation = hasLayer(validationLayer) &&
                                  hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    std::uint32_t loaderVersion = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&loaderVersion);
    if (loaderVersion < VK_API_VERSION_1_3) {
        std::fprintf(stderr, "SKIP: Vulkan 1.3 loader is unavailable\n");
        return 77;
    }

    VkDebugUtilsMessengerCreateInfoEXT debugInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = debugCallback;
    debugInfo.pUserData = &validation;

    const char* layers[] = {validationLayer};
    const char* instanceExtensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "FSR3 Vulkan backend smoke";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "ffx-vulkan standalone test";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pNext = enableValidation ? &debugInfo : nullptr;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledLayerCount = enableValidation ? 1u : 0u;
    instanceInfo.ppEnabledLayerNames = enableValidation ? layers : nullptr;
    instanceInfo.enabledExtensionCount = enableValidation ? 1u : 0u;
    instanceInfo.ppEnabledExtensionNames = enableValidation ? instanceExtensions : nullptr;

    VkInstance instance = VK_NULL_HANDLE;
    VkResult vkResult = vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (vkResult != VK_SUCCESS) {
        std::fprintf(stderr, "SKIP: vkCreateInstance failed: %d\n", static_cast<int>(vkResult));
        return 77;
    }

    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (enableValidation &&
        (!createMessenger || createMessenger(instance, &debugInfo, nullptr, &messenger) != VK_SUCCESS)) {
        std::fprintf(stderr, "could not create Vulkan validation messenger\n");
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    const PhysicalDeviceChoice choice = choosePhysicalDevice(instance);
    if (choice.device == VK_NULL_HANDLE) {
        std::fprintf(stderr, "SKIP: no Vulkan 1.3 device satisfies the FSR3 compute prerequisites\n");
        if (messenger && destroyMessenger) destroyMessenger(instance, messenger, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 77;
    }
    allocationAuditPhysicalDevice = choice.device;

    VkPhysicalDeviceMemoryProperties physicalMemoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(choice.device, &physicalMemoryProperties);
    std::uint32_t deviceCoherentMemoryTypeCount = 0;
    for (std::uint32_t index = 0; index < physicalMemoryProperties.memoryTypeCount; ++index) {
        if (physicalMemoryProperties.memoryTypes[index].propertyFlags &
            VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD) {
            ++deviceCoherentMemoryTypeCount;
        }
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(choice.device, &properties);

    std::uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(choice.device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> supportedExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(choice.device, nullptr, &extensionCount, supportedExtensions.data());

    std::vector<const char*> enabledExtensions;
    constexpr const char* usefulExtensions[] = {
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
        VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
        VK_AMD_BUFFER_MARKER_EXTENSION_NAME,
        VK_AMD_DEVICE_COHERENT_MEMORY_EXTENSION_NAME,
    };
    for (const char* extension : usefulExtensions) {
        if (hasDeviceExtension(supportedExtensions, extension)) {
            enabledExtensions.push_back(extension);
        }
    }

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features features11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features.pNext = &features11;
    features11.pNext = &features12;
    features12.pNext = &features13;
    // VK_AMD_device_coherent_memory may be enabled as an extension, but its
    // optional deviceCoherentMemory feature is intentionally absent from this
    // device-create chain. This reproduces Q2's logical-device contract.
    vkGetPhysicalDeviceFeatures2(choice.device, &features);
    const VkBool32 enabledShaderFloat16 = features12.shaderFloat16;
    const VkBool32 enabledStorageBufferNonUniformIndexing =
        features12.shaderStorageBufferArrayNonUniformIndexing;
    const VkBool32 enabledStorageImageWriteWithoutFormat =
        features.features.shaderStorageImageWriteWithoutFormat;
    // Match Q2's device: promoted optional features can be physically
    // supported while absent from the logical-device feature chain.
    features13.subgroupSizeControl = VK_FALSE;
    features13.computeFullSubgroups = VK_FALSE;
    features13.synchronization2 = VK_FALSE;

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = choice.queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &features;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size());
    deviceInfo.ppEnabledExtensionNames = enabledExtensions.data();

    VkDevice device = VK_NULL_HANDLE;
    vkResult = vkCreateDevice(choice.device, &deviceInfo, nullptr, &device);
    if (vkResult != VK_SUCCESS) {
        std::fprintf(stderr, "vkCreateDevice failed on %s: %d\n", properties.deviceName, static_cast<int>(vkResult));
        if (messenger && destroyMessenger) destroyMessenger(instance, messenger, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    std::uint64_t memoryUsageBytes = 0;
    std::uint32_t pipelineCount = 0;
    std::uint32_t resourceCount = 0;
    bool creationComplete = true;
    bool dispatchComplete = true;
    bool destroyComplete = true;
    bool disabledOptionalCapabilitiesSuppressed = true;
    std::uint64_t outputHash = 0;
    std::size_t nonzeroColorComponents = 0;

#if !defined(FFX_VK_PORTABLE_ONLY)
    VkDeviceContext deviceContext{};
    deviceContext.vkDevice = device;
    deviceContext.vkPhysicalDevice = choice.device;
    deviceContext.vkDeviceProcAddr = coreOnlyGetDeviceProcAddr;
    deviceContext.deviceCoherentMemoryEnabled = VK_FALSE;
    deviceContext.shaderFloat16Enabled = enabledShaderFloat16;
    deviceContext.shaderStorageBufferArrayNonUniformIndexingEnabled =
        enabledStorageBufferNonUniformIndexing;
    const std::size_t scratchSize = ffxGetScratchMemorySizeVK(choice.device, 1);
    // Use a deterministic 32-byte-aligned base. The v1.1.4 Vulkan backend's
    // EffectContext is over-aligned, and its original scratch layout placed
    // that array at base + 8. This catches regressions where internal sections
    // are not aligned independently of the caller-provided scratch base.
    std::vector<std::uint64_t> scratchStorage(
        (scratchSize + 31 + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t));
    const std::uintptr_t alignedScratchAddress =
        (reinterpret_cast<std::uintptr_t>(scratchStorage.data()) + 31u) & ~std::uintptr_t{31u};
    void* const scratch = reinterpret_cast<void*>(alignedScratchAddress);
    FfxInterface backend{};
    FfxErrorCode ffxResult = ffxGetInterfaceVK(&backend, ffxGetDeviceVK(&deviceContext), scratch,
                                               scratchSize, 1);
    if (ffxResult != FFX_OK || backend.fpSwapChainConfigureFrameGeneration != nullptr) {
        std::fprintf(stderr, "ffxGetInterfaceVK failed or exposed the excluded presenter callback: %d\n",
                     static_cast<int>(ffxResult));
        vkDestroyDevice(device, nullptr);
        if (messenger && destroyMessenger) destroyMessenger(instance, messenger, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    FfxDeviceCapabilities reportedCapabilities{};
    ffxResult = backend.fpGetDeviceCapabilities(&backend, &reportedCapabilities);
    disabledOptionalCapabilitiesSuppressed =
        ffxResult == FFX_OK &&
        reportedCapabilities.waveLaneCountMin == 32 &&
        reportedCapabilities.waveLaneCountMax == 32 &&
        !reportedCapabilities.deviceCoherentMemorySupported &&
        !reportedCapabilities.bufferMarkerSupported &&
        !reportedCapabilities.extendedSynchronizationSupported;

    originalCreatePipeline = backend.fpCreatePipeline;
    originalCreateResource = backend.fpCreateResource;
    backend.fpCreatePipeline = countCreatePipeline;
    backend.fpCreateResource = countCreateResource;

    FfxFsr3UpscalerContextDescription description{};
    description.flags = FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE |
                        FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE |
                        FFX_FSR3UPSCALER_ENABLE_DEBUG_CHECKING;
    description.maxRenderSize = {64, 64};
    description.maxUpscaleSize = {128, 128};
    description.fpMessage = ffxMessage;
    description.backendInterface = backend;

    FfxFsr3UpscalerContext context{};
    ffxResult = ffxFsr3UpscalerContextCreate(&context, &description);
    if (ffxResult != FFX_OK) {
        std::fprintf(stderr, "ffxFsr3UpscalerContextCreate failed on %s: %d\n",
                     properties.deviceName, static_cast<int>(ffxResult));
        vkDestroyDevice(device, nullptr);
        if (messenger && destroyMessenger) destroyMessenger(instance, messenger, nullptr);
        vkDestroyInstance(instance, nullptr);
        return 1;
    }

    FfxEffectMemoryUsage memoryUsage{};
    const FfxErrorCode memoryResult = ffxFsr3UpscalerContextGetGpuMemoryUsage(&context, &memoryUsage);
    FfxFsr3UpscalerSharedResourceDescriptions sharedDescriptions{};
    const FfxErrorCode sharedResult =
        ffxFsr3UpscalerGetSharedResourceDescriptions(&context, &sharedDescriptions);
    const auto sharedRenderTargetUsage = static_cast<FfxResourceUsage>(
        FFX_RESOURCE_USAGE_RENDERTARGET | FFX_RESOURCE_USAGE_UAV |
        FFX_RESOURCE_USAGE_DCC_RENDERTARGET);
    const bool sharedResourceContract =
        sharedResult == FFX_OK &&
        matchesSharedResourceDescription(sharedDescriptions.dilatedDepth,
                                         FFX_SURFACE_FORMAT_R32_FLOAT,
                                         sharedRenderTargetUsage) &&
        matchesSharedResourceDescription(sharedDescriptions.dilatedMotionVectors,
                                         FFX_SURFACE_FORMAT_R16G16_FLOAT,
                                         sharedRenderTargetUsage) &&
        matchesSharedResourceDescription(sharedDescriptions.reconstructedPrevNearestDepth,
                                         FFX_SURFACE_FORMAT_R32_UINT,
                                         FFX_RESOURCE_USAGE_UAV);
    if (!sharedResourceContract) {
        std::fprintf(stderr, "FSR3 shared-resource description contract changed\n");
    }
    creationComplete = pipelinesCreated == 11 && resourcesCreated == 23 &&
                       memoryResult == FFX_OK && memoryUsage.totalUsageInBytes > 0 &&
                       sharedResourceContract;
    memoryUsageBytes = memoryUsage.totalUsageInBytes;
    pipelineCount = pipelinesCreated;
    resourceCount = resourcesCreated;
    std::array<TestImage, 7> importedImages{};
    dispatchComplete = creationComplete && runDispatchAndReadback(
        choice.device, device, choice.queueFamily, &context, nullptr, &importedImages,
        &outputHash, &nonzeroColorComponents, 1, true);

    vkDeviceWaitIdle(device);
    destroyComplete = ffxFsr3UpscalerContextDestroy(&context) == FFX_OK;
    for (TestImage& image : importedImages) destroyImage(device, &image);
#endif

    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, choice.queueFamily, 0, &queue);
    FfxVkPortableDeviceInfo portableDevice{};
    portableDevice.structSize = sizeof(portableDevice);
    portableDevice.instance = instance;
    portableDevice.physicalDevice = choice.device;
    portableDevice.device = device;
    portableDevice.getDeviceProcAddr = coreOnlyGetDeviceProcAddr;
    portableDevice.queue = queue;
    portableDevice.queueFamilyIndex = choice.queueFamily;
    portableDevice.deviceCoherentMemoryEnabled = VK_FALSE;
    portableDevice.shaderFloat16Enabled = enabledShaderFloat16;
    portableDevice.shaderStorageBufferArrayNonUniformIndexingEnabled =
        enabledStorageBufferNonUniformIndexing;
    portableDevice.shaderStorageImageWriteWithoutFormatEnabled =
        enabledStorageImageWriteWithoutFormat;
    FfxVkPortableUpscaleCreateInfo portableDescription{};
    portableDescription.structSize = sizeof(portableDescription);
    portableDescription.flags = FFX_VK_PORTABLE_CONTEXT_HDR_COLOR_INPUT |
                                FFX_VK_PORTABLE_CONTEXT_AUTO_EXPOSURE |
                                FFX_VK_PORTABLE_CONTEXT_DEBUG_CHECKING;
    portableDescription.maxRenderSize = {64, 64};
    portableDescription.maxOutputSize = {128, 128};

    /* Physical support is not enough: the owner must explicitly carry this
     * feature into VkDeviceCreateInfo. The wrapper must reject an otherwise
     * valid device contract that says it did not. */
    FfxVkPortableDeviceInfo missingStorageWriteFeature = portableDevice;
    missingStorageWriteFeature.shaderStorageImageWriteWithoutFormatEnabled = VK_FALSE;
    FfxVkPortableUpscaleContext* rejectedPortableContext =
        reinterpret_cast<FfxVkPortableUpscaleContext*>(static_cast<uintptr_t>(1));
    const FfxVkPortableResult missingStorageWriteFeatureResult =
        ffxVkPortableUpscaleContextCreate(&missingStorageWriteFeature,
                                          &portableDescription,
                                          &rejectedPortableContext);
    const bool missingStorageWriteFeatureRejected =
        missingStorageWriteFeatureResult == FFX_VK_PORTABLE_ERROR_UNSUPPORTED &&
        rejectedPortableContext == nullptr;

    FfxVkPortableUpscaleContext* portableContext = nullptr;
    const FfxVkPortableResult portableCreateResult = ffxVkPortableUpscaleContextCreate(
        &portableDevice, &portableDescription, &portableContext);
    FfxVkPortableMemoryUsage portableMemoryUsage{};
    portableMemoryUsage.structSize = sizeof(portableMemoryUsage);
    const FfxVkPortableResult portableMemoryUsageResult =
        portableCreateResult == FFX_VK_PORTABLE_OK
            ? ffxVkPortableUpscaleContextGetMemoryUsage(portableContext, &portableMemoryUsage)
            : portableCreateResult;
    const bool portableMemoryUsageValid =
        portableMemoryUsageResult == FFX_VK_PORTABLE_OK &&
        portableMemoryUsage.totalUsageInBytes > 0u &&
        portableMemoryUsage.totalUsageInBytes >= portableMemoryUsage.aliasableUsageInBytes;
    std::array<TestImage, 7> portableImportedImages{};
    std::array<TestImage, 7> portableSecondImportedImages{};
    std::uint64_t portableOutputHash = 0;
    std::uint64_t portableSecondOutputHash = 0;
    std::size_t portableNonzeroColorComponents = 0;
    std::size_t portableSecondNonzeroColorComponents = 0;
    const bool portableFirstDispatchComplete =
        portableCreateResult == FFX_VK_PORTABLE_OK &&
        runDispatchAndReadback(
            choice.device, device, choice.queueFamily, nullptr, portableContext,
            &portableImportedImages, &portableOutputHash,
            &portableNonzeroColorComponents, 1, true);
    const bool portableSecondDispatchComplete =
        portableFirstDispatchComplete &&
        runDispatchAndReadback(
            choice.device, device, choice.queueFamily, nullptr, portableContext,
            &portableSecondImportedImages, &portableSecondOutputHash,
            &portableSecondNonzeroColorComponents, 2, false);
    vkDeviceWaitIdle(device);
    const FfxVkPortableResult portableDestroyResult =
        ffxVkPortableUpscaleContextDestroy(portableContext);
    for (TestImage& image : portableImportedImages) destroyImage(device, &image);
    for (TestImage& image : portableSecondImportedImages) destroyImage(device, &image);

    FfxVkPortableFrameGenerationCreateInfo frameGenerationDescription{};
    frameGenerationDescription.structSize = sizeof(frameGenerationDescription);
    frameGenerationDescription.flags = FFX_VK_PORTABLE_CONTEXT_HDR_COLOR_INPUT;
    frameGenerationDescription.maxRenderSize = {64, 64};
    frameGenerationDescription.displaySize = {128, 128};
    frameGenerationDescription.interpolationSourceFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    frameGenerationDescription.outputFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    FfxVkPortableFrameGenerationContext* frameGenerationContext = nullptr;
    const FfxVkPortableResult frameGenerationCreateResult =
        ffxVkPortableFrameGenerationContextCreate(
            &portableDevice, &frameGenerationDescription, &frameGenerationContext);
    std::uint64_t frameGenerationFirstHash = 0;
    std::uint64_t frameGenerationSecondHash = 0;
    std::size_t frameGenerationFirstNonzero = 0;
    std::size_t frameGenerationSecondNonzero = 0;
    const bool frameGenerationFirstDispatchComplete =
        frameGenerationCreateResult == FFX_VK_PORTABLE_OK &&
        runFrameGenerationAndReadback(choice.device, device, choice.queueFamily,
                                      frameGenerationContext, 1, true,
                                      &frameGenerationFirstHash, &frameGenerationFirstNonzero);
    const bool frameGenerationSecondDispatchComplete =
        frameGenerationFirstDispatchComplete &&
        runFrameGenerationAndReadback(choice.device, device, choice.queueFamily,
                                      frameGenerationContext, 2, false,
                                      &frameGenerationSecondHash, &frameGenerationSecondNonzero);
    /* FI performs Prepare plus Dispatch for every real frame. Exercise more
     * than the former four-call dynamic-view ring so the reusable backend
     * continues to cover a multi-dispatch host instead of only the first
     * reset/temporal pair. Each helper submission waits before freeing its
     * imported test images; Q2RTX separately verifies the in-flight case. */
    std::uint64_t frameGenerationRingHash = 0;
    std::size_t frameGenerationRingNonzero = 0;
    bool frameGenerationRingComplete = frameGenerationSecondDispatchComplete;
    for (std::uint64_t frameId = 3; frameGenerationRingComplete && frameId <= 5;
         ++frameId) {
        frameGenerationRingComplete = runFrameGenerationAndReadback(
            choice.device, device, choice.queueFamily, frameGenerationContext,
            frameId, false, &frameGenerationRingHash,
            &frameGenerationRingNonzero);
    }
    vkDeviceWaitIdle(device);
    const FfxVkPortableResult frameGenerationDestroyResult =
        ffxVkPortableFrameGenerationContextDestroy(frameGenerationContext);

    vkDestroyDevice(device, nullptr);
    if (messenger && destroyMessenger) {
        destroyMessenger(instance, messenger, nullptr);
    }
    vkDestroyInstance(instance, nullptr);

    std::printf("device=%s pipelines=%u resources=%u VRAM=%llu bytes output=%016llx "
                "nonzero=%zu portable_output=%016llx portable_nonzero=%zu "
                "portable_second_output=%016llx portable_second_nonzero=%zu "
                "framegen_first_output=%016llx framegen_first_nonzero=%zu "
                "framegen_second_output=%016llx framegen_second_nonzero=%zu "
                "memory_requirements2_khr_queries=%u memory_requirements2_core_queries=%u "
                "coherent_memory_types=%u audited_allocations=%u forbidden_coherent_allocations=%u "
                "audited_compute_pipelines=%u forbidden_subgroup_size=%u disabled_optional_queries=%u "
                "disabled_optional_caps=%s "
                "validation=%s warnings=%u errors=%u\n",
                properties.deviceName, pipelineCount, resourceCount,
                static_cast<unsigned long long>(memoryUsageBytes),
                static_cast<unsigned long long>(outputHash), nonzeroColorComponents,
                static_cast<unsigned long long>(portableOutputHash),
                portableNonzeroColorComponents,
                static_cast<unsigned long long>(portableSecondOutputHash),
                portableSecondNonzeroColorComponents,
                static_cast<unsigned long long>(frameGenerationFirstHash), frameGenerationFirstNonzero,
                static_cast<unsigned long long>(frameGenerationSecondHash), frameGenerationSecondNonzero,
                khrMemoryRequirements2Queries.load(),
                coreMemoryRequirements2Queries.load(),
                deviceCoherentMemoryTypeCount,
                auditedAllocationCount.load(),
                forbiddenDeviceCoherentAllocationCount.load(),
                auditedComputePipelineCount.load(),
                forbiddenRequiredSubgroupSizeCount.load(),
                disabledOptionalProcedureQueryCount.load(),
                disabledOptionalCapabilitiesSuppressed ? "suppressed" : "exposed",
                enableValidation ? "enabled" : "unavailable", validation.warnings.load(),
                validation.errors.load());
    if (!creationComplete || !dispatchComplete || !destroyComplete ||
        !disabledOptionalCapabilitiesSuppressed ||
        !missingStorageWriteFeatureRejected ||
        portableCreateResult != FFX_VK_PORTABLE_OK || !portableMemoryUsageValid ||
        !portableFirstDispatchComplete ||
        !portableSecondDispatchComplete ||
        portableDestroyResult != FFX_VK_PORTABLE_OK ||
        frameGenerationCreateResult != FFX_VK_PORTABLE_OK ||
        !frameGenerationFirstDispatchComplete || !frameGenerationSecondDispatchComplete ||
        !frameGenerationRingComplete ||
        frameGenerationDestroyResult != FFX_VK_PORTABLE_OK ||
        khrMemoryRequirements2Queries.load() == 0 ||
        coreMemoryRequirements2Queries.load() == 0 ||
        auditedAllocationCount.load() == 0 ||
        forbiddenDeviceCoherentAllocationCount.load() != 0 ||
        auditedComputePipelineCount.load() == 0 ||
        forbiddenRequiredSubgroupSizeCount.load() != 0 ||
        disabledOptionalProcedureQueryCount.load() != 0 ||
        validation.warnings.load() != 0 || validation.errors.load() != 0) {
        std::fprintf(stderr, "FSR3 Vulkan backend smoke validation failed\n");
        return 1;
    }
    return 0;
}
