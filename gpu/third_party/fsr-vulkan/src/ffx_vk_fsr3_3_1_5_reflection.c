/*
 * Copyright (c) 2026 Q2RTX FSR Vulkan contributors
 * SPDX-License-Identifier: MIT
 */

#include "ffx_vk_fsr3_3_1_5_reflection.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SPIRV_MAGIC 0x07230203u
#define SPIRV_OP_ENTRY_POINT 15u
#define SPIRV_OP_NAME 5u
#define SPIRV_OP_TYPE_IMAGE 25u
#define SPIRV_OP_TYPE_SAMPLER 26u
#define SPIRV_OP_TYPE_SAMPLED_IMAGE 27u
#define SPIRV_OP_TYPE_STRUCT 30u
#define SPIRV_OP_TYPE_POINTER 32u
#define SPIRV_OP_VARIABLE 59u
#define SPIRV_OP_DECORATE 71u
#define SPIRV_DECORATION_BINDING 33u
#define SPIRV_DECORATION_DESCRIPTOR_SET 34u
#define SPIRV_STORAGE_CLASS_UNIFORM_CONSTANT 0u
#define SPIRV_STORAGE_CLASS_UNIFORM 2u
#define SPIRV_STORAGE_CLASS_STORAGE_BUFFER 12u
#define NO_BINDING UINT_MAX
#define NO_DESCRIPTOR_SET UINT_MAX
#define NO_ID UINT_MAX
#define MAX_SPIRV_BOUND (1u << 20)
#define SPIRV_EXECUTION_MODEL_GL_COMPUTE 5u

typedef enum SpirvTypeKind {
    SPIRV_TYPE_UNKNOWN = 0,
    SPIRV_TYPE_IMAGE,
    SPIRV_TYPE_SAMPLER,
    SPIRV_TYPE_SAMPLED_IMAGE,
    SPIRV_TYPE_STRUCT,
} SpirvTypeKind;

static int descriptor_class(uint32_t variableId, const char* name,
                            const uint32_t* variablePointerTypes,
                            const uint32_t* pointerPointees,
                            const uint32_t* pointerStorageClasses,
                            const uint8_t* typeKinds,
                            const uint32_t* imageSampled,
                            uint32_t bound,
                            FfxVkFsr3_3_1_5DescriptorClass* outClass)
{
    if (!variablePointerTypes || !pointerPointees || !pointerStorageClasses ||
        !typeKinds || !imageSampled || !outClass || variableId >= bound)
        return 0;
    const uint32_t pointerType = variablePointerTypes[variableId];
    if (pointerType == NO_ID || pointerType >= bound)
        return 0;
    const uint32_t pointee = pointerPointees[pointerType];
    const uint32_t storageClass = pointerStorageClasses[pointerType];
    if (pointee == NO_ID || pointee >= bound)
        return 0;
    if (storageClass == SPIRV_STORAGE_CLASS_STORAGE_BUFFER) {
        /* Vulkan uses VK_DESCRIPTOR_TYPE_STORAGE_BUFFER for both access
         * directions. The SDK deliberately exposes separate SRV/UAV job
         * tables, so retain its r_/rw_ naming convention only for that final
         * access-direction split; the SPIR-V StorageBuffer declaration—not
         * the name—selects the buffer descriptor class. */
        *outClass = name && strncmp(name, "rw_", 3u) == 0 ?
                        FFX_VK_FSR3_3_1_5_DESCRIPTOR_BUFFER_UAV :
                        FFX_VK_FSR3_3_1_5_DESCRIPTOR_BUFFER_SRV;
        return 1;
    }
    if (storageClass == SPIRV_STORAGE_CLASS_UNIFORM &&
        typeKinds[pointee] == SPIRV_TYPE_STRUCT) {
        *outClass = FFX_VK_FSR3_3_1_5_DESCRIPTOR_CONSTANT_BUFFER;
        return 1;
    }
    if (storageClass != SPIRV_STORAGE_CLASS_UNIFORM_CONSTANT)
        return 0;
    if (typeKinds[pointee] == SPIRV_TYPE_SAMPLER) {
        *outClass = FFX_VK_FSR3_3_1_5_DESCRIPTOR_SAMPLER;
        return 1;
    }
    if (typeKinds[pointee] == SPIRV_TYPE_IMAGE) {
        if (imageSampled[pointee] == 1u)
            *outClass = FFX_VK_FSR3_3_1_5_DESCRIPTOR_SRV;
        else if (imageSampled[pointee] == 2u)
            *outClass = FFX_VK_FSR3_3_1_5_DESCRIPTOR_UAV;
        else
            return 0;
        return 1;
    }
    return 0;
}

static void copy_name(char destination[64], const char* source, uint32_t wordCount)
{
    const size_t available = (size_t)wordCount * sizeof(uint32_t);
    size_t count = 0;
    if (!source) {
        destination[0] = '\0';
        return;
    }
    while (count < available && count < 63u && source[count] != '\0')
        ++count;
    memcpy(destination, source, count);
    destination[count] = '\0';
}

static int compare_binding(const void* left, const void* right)
{
    const FfxVkFsr3_3_1_5DescriptorBinding* a = left;
    const FfxVkFsr3_3_1_5DescriptorBinding* b = right;
    return (a->binding > b->binding) - (a->binding < b->binding);
}

FfxVkPortableResult ffxVkFsr3_3_1_5ReflectComputeEntryPoint(
    const uint32_t* words,
    size_t wordCount,
    char outEntryPoint[64])
{
    int found = 0;
    size_t offset;

    if (!words || !outEntryPoint)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (wordCount < 5u || words[0] != SPIRV_MAGIC)
        return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;

    for (offset = 5u; offset < wordCount;) {
        const uint32_t instruction = words[offset];
        const uint32_t instructionWords = instruction >> 16u;
        const uint32_t opcode = instruction & 0xffffu;
        if (instructionWords == 0u || instructionWords > wordCount - offset)
            return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
        if (opcode == SPIRV_OP_ENTRY_POINT && instructionWords >= 4u &&
            words[offset + 1u] == SPIRV_EXECUTION_MODEL_GL_COMPUTE) {
            const char* name = (const char*)&words[offset + 3u];
            const size_t available = (size_t)(instructionWords - 3u) * sizeof(uint32_t);
            size_t length = 0u;
            while (length < available && name[length] != '\0')
                ++length;
            if (length == available || length >= 64u)
                return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
            if (found)
                return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
            memcpy(outEntryPoint, name, length + 1u);
            found = 1;
        }
        offset += instructionWords;
    }
    return found ? FFX_VK_PORTABLE_OK : FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
}

FfxVkPortableResult ffxVkFsr3_3_1_5ReflectSpirv(
    const uint32_t* words,
    size_t wordCount,
    FfxVkFsr3_3_1_5DescriptorBinding* outBindings,
    uint32_t* inOutBindingCount)
{
    const char** names;
    uint32_t* bindings;
    uint32_t* descriptorSets;
    uint32_t* variablePointerTypes;
    uint32_t* pointerPointees;
    uint32_t* pointerStorageClasses;
    uint8_t* typeKinds;
    uint32_t* imageSampled;
    uint32_t bound;
    uint32_t count = 0;
    uint8_t seenBindings[1024] = {};
    size_t offset;

    if (!words || !inOutBindingCount)
        return FFX_VK_PORTABLE_ERROR_INVALID_POINTER;
    if (wordCount < 5u || words[0] != SPIRV_MAGIC)
        return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
    bound = words[3];
    if (bound == 0u || bound > MAX_SPIRV_BOUND)
        return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;

    names = calloc(bound, sizeof(*names));
    bindings = malloc((size_t)bound * sizeof(*bindings));
    descriptorSets = malloc((size_t)bound * sizeof(*descriptorSets));
    variablePointerTypes = malloc((size_t)bound * sizeof(*variablePointerTypes));
    pointerPointees = malloc((size_t)bound * sizeof(*pointerPointees));
    pointerStorageClasses = malloc((size_t)bound * sizeof(*pointerStorageClasses));
    typeKinds = calloc(bound, sizeof(*typeKinds));
    imageSampled = malloc((size_t)bound * sizeof(*imageSampled));
    if (!names || !bindings || !descriptorSets || !variablePointerTypes || !pointerPointees ||
        !pointerStorageClasses || !typeKinds || !imageSampled) {
        free(imageSampled); free(typeKinds); free(pointerStorageClasses); free(pointerPointees);
        free(variablePointerTypes); free(descriptorSets); free(bindings); free(names);
        return FFX_VK_PORTABLE_ERROR_OUT_OF_MEMORY;
    }
    for (uint32_t index = 0; index < bound; ++index) {
        bindings[index] = NO_BINDING;
        descriptorSets[index] = NO_DESCRIPTOR_SET;
        variablePointerTypes[index] = NO_ID;
        pointerPointees[index] = NO_ID;
        pointerStorageClasses[index] = NO_ID;
        imageSampled[index] = NO_ID;
    }

    for (offset = 5u; offset < wordCount;) {
        const uint32_t instruction = words[offset];
        const uint32_t instructionWords = instruction >> 16u;
        const uint32_t opcode = instruction & 0xffffu;
        if (instructionWords == 0u || instructionWords > wordCount - offset) {
            free(imageSampled); free(typeKinds); free(pointerStorageClasses); free(pointerPointees);
            free(variablePointerTypes); free(descriptorSets); free(bindings); free(names);
            return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
        }
        if (opcode == SPIRV_OP_NAME && instructionWords >= 3u && words[offset + 1u] < bound) {
            names[words[offset + 1u]] = (const char*)&words[offset + 2u];
        } else if (opcode == SPIRV_OP_DECORATE && instructionWords >= 4u &&
                   words[offset + 1u] < bound &&
                   words[offset + 2u] == SPIRV_DECORATION_BINDING) {
            bindings[words[offset + 1u]] = words[offset + 3u];
        } else if (opcode == SPIRV_OP_DECORATE && instructionWords >= 4u &&
                   words[offset + 1u] < bound &&
                   words[offset + 2u] == SPIRV_DECORATION_DESCRIPTOR_SET) {
            descriptorSets[words[offset + 1u]] = words[offset + 3u];
        } else if (opcode == SPIRV_OP_TYPE_IMAGE && instructionWords >= 9u &&
                   words[offset + 1u] < bound) {
            typeKinds[words[offset + 1u]] = SPIRV_TYPE_IMAGE;
            imageSampled[words[offset + 1u]] = words[offset + 7u];
        } else if (opcode == SPIRV_OP_TYPE_SAMPLER && instructionWords >= 2u &&
                   words[offset + 1u] < bound) {
            typeKinds[words[offset + 1u]] = SPIRV_TYPE_SAMPLER;
        } else if (opcode == SPIRV_OP_TYPE_SAMPLED_IMAGE && instructionWords >= 3u &&
                   words[offset + 1u] < bound) {
            typeKinds[words[offset + 1u]] = SPIRV_TYPE_SAMPLED_IMAGE;
        } else if (opcode == SPIRV_OP_TYPE_STRUCT && instructionWords >= 2u &&
                   words[offset + 1u] < bound) {
            typeKinds[words[offset + 1u]] = SPIRV_TYPE_STRUCT;
        } else if (opcode == SPIRV_OP_TYPE_POINTER && instructionWords >= 4u &&
                   words[offset + 1u] < bound && words[offset + 3u] < bound) {
            pointerStorageClasses[words[offset + 1u]] = words[offset + 2u];
            pointerPointees[words[offset + 1u]] = words[offset + 3u];
        } else if (opcode == SPIRV_OP_VARIABLE && instructionWords >= 4u &&
                   words[offset + 1u] < bound && words[offset + 2u] < bound) {
            variablePointerTypes[words[offset + 2u]] = words[offset + 1u];
        }
        offset += instructionWords;
    }

    for (uint32_t index = 0; index < bound; ++index) {
        if (bindings[index] != NO_BINDING) {
            if (descriptorSets[index] != 0u || bindings[index] >= 1024u ||
                !descriptor_class(index, names[index], variablePointerTypes, pointerPointees,
                                  pointerStorageClasses, typeKinds, imageSampled, bound,
                                  &(FfxVkFsr3_3_1_5DescriptorClass){0})) {
                free(imageSampled); free(typeKinds); free(pointerStorageClasses); free(pointerPointees);
                free(variablePointerTypes); free(descriptorSets); free(bindings); free(names);
                return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
            }
            if (seenBindings[bindings[index]]) {
                free(imageSampled); free(typeKinds); free(pointerStorageClasses); free(pointerPointees);
                free(variablePointerTypes); free(descriptorSets); free(bindings); free(names);
                return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
            }
            seenBindings[bindings[index]] = 1u;
            ++count;
        }
    }
    if (outBindings && *inOutBindingCount < count) {
        *inOutBindingCount = count;
        free(imageSampled); free(typeKinds); free(pointerStorageClasses); free(pointerPointees);
        free(variablePointerTypes); free(descriptorSets); free(bindings); free(names);
        return FFX_VK_PORTABLE_ERROR_INVALID_ARGUMENT;
    }
    if (outBindings) {
        uint32_t output = 0;
        for (uint32_t index = 0; index < bound; ++index) {
            if (bindings[index] == NO_BINDING)
                continue;
            outBindings[output].binding = bindings[index];
            if (!descriptor_class(index, names[index], variablePointerTypes, pointerPointees,
                                  pointerStorageClasses, typeKinds, imageSampled, bound,
                                  &outBindings[output].descriptorClass)) {
                free(imageSampled); free(typeKinds); free(pointerStorageClasses); free(pointerPointees);
                free(variablePointerTypes); free(descriptorSets); free(bindings); free(names);
                return FFX_VK_PORTABLE_ERROR_UNSUPPORTED;
            }
            copy_name(outBindings[output].name, names[index],
                      names[index] ? (uint32_t)((wordCount - (size_t)(names[index] - (const char*)words) / sizeof(uint32_t))) : 0u);
            ++output;
        }
        qsort(outBindings, count, sizeof(*outBindings), compare_binding);
    }
    *inOutBindingCount = count;
    free(imageSampled); free(typeKinds); free(pointerStorageClasses); free(pointerPointees);
    free(variablePointerTypes); free(descriptorSets); free(bindings); free(names);
    return FFX_VK_PORTABLE_OK;
}
