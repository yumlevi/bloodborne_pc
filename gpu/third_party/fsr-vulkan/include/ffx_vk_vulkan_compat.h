/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 *
 * Compatibility declarations for Vulkan SDK header sets that predate a
 * promoted extension.  These declarations are taken from the public
 * VK_KHR_compute_shader_derivatives specification (revision 1); no Vulkan
 * loader symbols are introduced.
 */

#pragma once

#include <vulkan/vulkan.h>

/* Ubuntu 24.04's libvulkan-dev header set predates this KHR extension.  Keep
 * the public library buildable there while still probing/enabling the real
 * extension by its stable runtime name when a driver provides it. */
#ifndef VK_KHR_compute_shader_derivatives
#define VK_KHR_compute_shader_derivatives 1
#define VK_KHR_COMPUTE_SHADER_DERIVATIVES_SPEC_VERSION 1
#define VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME \
    "VK_KHR_compute_shader_derivatives"

typedef struct VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR {
    VkStructureType sType;
    void* pNext;
    VkBool32 computeDerivativeGroupQuads;
    VkBool32 computeDerivativeGroupLinear;
} VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR;

typedef struct VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR {
    VkStructureType sType;
    void* pNext;
    VkBool32 meshAndTaskShaderDerivatives;
} VkPhysicalDeviceComputeShaderDerivativesPropertiesKHR;

/* VK_STRUCTURE_TYPE_* is an enum member in newer headers, so spell the
 * published value as a cast when backporting it to an older header set. */
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR \
    ((VkStructureType)1000201000)
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_PROPERTIES_KHR \
    ((VkStructureType)1000511000)
#endif
