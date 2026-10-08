 /*
  * VANILLA Modules
  * Copyright (C) 2026 Samed
  *
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, version 2 only,
  * as published by the Free Software Foundation.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  *
  * See the LICENSE file for the full license text.
  */

#include "vanilla_window_vulkan_utils.h"

VANILLA_PipelineCreateInfo* pipelineInfos = NULL;

static VkShaderModule createShaderModule(const uint32_t* code, size_t size) {
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode = code
    };

    VkShaderModule module;
    if (vkCreateShaderModule(
        vulkan.device,
        &createInfo,
        NULL,
        &module) != VK_SUCCESS) { return VK_NULL_HANDLE; }
    return module;
}

static VkFormat getVertexFormat(VANILLA_VertexFormat format) {
    switch (format) {
        case VANILLA_VERTEXFORMAT_R32_FLOAT:
            return VK_FORMAT_R32_SFLOAT;
        case VANILLA_VERTEXFORMAT_R32G32_FLOAT:
            return VK_FORMAT_R32G32_SFLOAT;
        case VANILLA_VERTEXFORMAT_R32G32B32_FLOAT:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case VANILLA_VERTEXFORMAT_R32G32B32A32_FLOAT:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

static VkPrimitiveTopology getPrimitiveTopology(VANILLA_PrimitiveTopology topology) {
    switch (topology) {
        case VANILLA_PRIMITIVE_TOPOLOGY_POINT_LIST:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case VANILLA_PRIMITIVE_TOPOLOGY_LINE_LIST:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case VANILLA_PRIMITIVE_TOPOLOGY_LINE_STRIP:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case VANILLA_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case VANILLA_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case VANILLA_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        default:
            return VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
    }
}

static VkPolygonMode getPolygonMode(VANILLA_PolygonMode mode) {
    switch (mode) {
        case VANILLA_POLYGON_MODE_FILL:
            return VK_POLYGON_MODE_FILL;
        case VANILLA_POLYGON_MODE_LINE:
            return VK_POLYGON_MODE_LINE;
        case VANILLA_POLYGON_MODE_POINT:
            return VK_POLYGON_MODE_POINT;
        default:
            return VK_POLYGON_MODE_MAX_ENUM;
    }
}

static VkCullModeFlags getCullMode(VANILLA_CullMode mode) {
    switch (mode) {
        case VANILLA_CULL_MODE_NONE:
            return VK_CULL_MODE_NONE;
        case VANILLA_CULL_MODE_FRONT:
            return VK_CULL_MODE_FRONT_BIT;
        case VANILLA_CULL_MODE_BACK:
            return VK_CULL_MODE_BACK_BIT;
        case VANILLA_CULL_MODE_FRONT_AND_BACK:
            return VK_CULL_MODE_FRONT_AND_BACK;
        default:
            return VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
    }
}

static VkFrontFace getFrontFace(VANILLA_FrontFace frontFace) {
    switch (frontFace) {
        case VANILLA_FRONT_FACE_COUNTER_CLOCKWISE:
            return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        case VANILLA_FRONT_FACE_CLOCKWISE:
            return VK_FRONT_FACE_CLOCKWISE;
        default:
            return VK_FRONT_FACE_MAX_ENUM;
    }
}

static VkCompareOp getCompareOp(VANILLA_CompareOp compareOp) {
    switch (compareOp) {
        case VANILLA_COMPARE_NEVER:
            return VK_COMPARE_OP_NEVER;
        case VANILLA_COMPARE_LESS:
            return VK_COMPARE_OP_LESS;
        case VANILLA_COMPARE_EQUAL:
            return VK_COMPARE_OP_EQUAL;
        case VANILLA_COMPARE_LESS_OR_EQUAL:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case VANILLA_COMPARE_GREATER:
            return VK_COMPARE_OP_GREATER;
        case VANILLA_COMPARE_NOT_EQUAL:
            return VK_COMPARE_OP_NOT_EQUAL;
        case VANILLA_COMPARE_GREATER_OR_EQUAL:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case VANILLA_COMPARE_ALWAYS:
            return VK_COMPARE_OP_ALWAYS;
        default:
            return VK_COMPARE_OP_MAX_ENUM;
    }
}

static VkBlendFactor getBlendFactor(VANILLA_BlendFactor factor) {
    switch (factor) {
        case VANILLA_BLEND_FACTOR_ZERO:
            return VK_BLEND_FACTOR_ZERO;
        case VANILLA_BLEND_FACTOR_ONE:
            return VK_BLEND_FACTOR_ONE;
        case VANILLA_BLEND_FACTOR_SRC_COLOR:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case VANILLA_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case VANILLA_BLEND_FACTOR_DST_COLOR:
            return VK_BLEND_FACTOR_DST_COLOR;
        case VANILLA_BLEND_FACTOR_ONE_MINUS_DST_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case VANILLA_BLEND_FACTOR_SRC_ALPHA:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case VANILLA_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case VANILLA_BLEND_FACTOR_DST_ALPHA:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case VANILLA_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        default:
            return VK_BLEND_FACTOR_MAX_ENUM;
    }
}

static VkBlendOp getBlendOp(VANILLA_BlendOp op) {
    switch (op) {
        case VANILLA_BLEND_OP_ADD:
            return VK_BLEND_OP_ADD;
        case VANILLA_BLEND_OP_SUBTRACT:
            return VK_BLEND_OP_SUBTRACT;
        case VANILLA_BLEND_OP_REVERSE_SUBTRACT:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case VANILLA_BLEND_OP_MIN:
            return VK_BLEND_OP_MIN;
        case VANILLA_BLEND_OP_MAX:
            return VK_BLEND_OP_MAX;
        default:
            return VK_BLEND_OP_MAX_ENUM;
    }
}

static VkStencilOp getStencilOp(VANILLA_StencilOp op) {
    switch (op) {
        case VANILLA_STENCIL_KEEP:
            return VK_STENCIL_OP_KEEP;
        case VANILLA_STENCIL_ZERO:
            return VK_STENCIL_OP_ZERO;
        case VANILLA_STENCIL_REPLACE:
            return VK_STENCIL_OP_REPLACE;
        case VANILLA_STENCIL_INCREMENT_AND_CLAMP:
            return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
        case VANILLA_STENCIL_DECREMENT_AND_CLAMP:
            return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
        case VANILLA_STENCIL_INVERT:
            return VK_STENCIL_OP_INVERT;
        case VANILLA_STENCIL_INCREMENT_AND_WRAP:
            return VK_STENCIL_OP_INCREMENT_AND_WRAP;
        case VANILLA_STENCIL_DECREMENT_AND_WRAP:
            return VK_STENCIL_OP_DECREMENT_AND_WRAP;
        default:
            return VK_STENCIL_OP_MAX_ENUM;
    }
}

static VkSampleCountFlagBits getSampleCount(VANILLA_SampleCount samples) {
    switch (samples) {
        case VANILLA_SAMPLE_COUNT_1:
            return VK_SAMPLE_COUNT_1_BIT;
        case VANILLA_SAMPLE_COUNT_2:
            return VK_SAMPLE_COUNT_2_BIT;
        case VANILLA_SAMPLE_COUNT_4:
            return VK_SAMPLE_COUNT_4_BIT;
        case VANILLA_SAMPLE_COUNT_8:
            return VK_SAMPLE_COUNT_8_BIT;
        case VANILLA_SAMPLE_COUNT_16:
            return VK_SAMPLE_COUNT_16_BIT;
        case VANILLA_SAMPLE_COUNT_32:
            return VK_SAMPLE_COUNT_32_BIT;
        case VANILLA_SAMPLE_COUNT_64:
            return VK_SAMPLE_COUNT_64_BIT;
        default:
            return VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM;
    }
}

static VkColorComponentFlags getColorWriteMask(uint8_t mask) {
    VkColorComponentFlags result = 0;
    if (mask & 0x01) { result |= VK_COLOR_COMPONENT_R_BIT; }
    if (mask & 0x02) { result |= VK_COLOR_COMPONENT_G_BIT; }
    if (mask & 0x04) { result |= VK_COLOR_COMPONENT_B_BIT; }
    if (mask & 0x08) { result |= VK_COLOR_COMPONENT_A_BIT; }
    return result;
}

static VkStencilOpState getStencilState(const VANILLA_StencilState* state) {
    VkStencilOpState result = {
        .failOp = getStencilOp(state->failOp),
        .passOp = getStencilOp(state->passOp),
        .depthFailOp = getStencilOp(state->depthFailOp),
        .compareOp = getCompareOp(state->compareOp),
        .compareMask = state->compareMask,
        .writeMask = state->writeMask,
        .reference = state->reference
    };
    return result;
}

int VANILLA_VK_createPipeline(void) {
    if (vertexLayout.attributes == NULL || vertexLayout.attributeCount > 16) { return 1; }

    for (uint32_t i = 0; i < vulkan.pipelineCount; i++) {
        const VANILLA_PipelineCreateInfo* info = &pipelineInfos[i];
        if (info->vertexShader == NULL || info->fragmentShader == NULL) { return 1; }

        VkShaderModule vertShader = createShaderModule(info->vertexShader, info->vertexShaderSize);
        if (vertShader == VK_NULL_HANDLE) { return 1; }

        VkShaderModule fragShader = createShaderModule(info->fragmentShader, info->fragmentShaderSize);
        if (fragShader == VK_NULL_HANDLE) {
            vkDestroyShaderModule(
                vulkan.device,
                vertShader,
                NULL
            );
            return 1;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {
            {
                .sType =VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertShader,
                .pName = "main"
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragShader,
                .pName = "main"
            }
        };

        VkVertexInputBindingDescription bindingDesc = {
            .binding = 0,
            .stride = stride,
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
        };

        VkVertexInputAttributeDescription attrDescs[16] = {0};
        for (uint32_t j = 0; j < vertexLayout.attributeCount; j++) {
            VkFormat format = getVertexFormat(vertexLayout.attributes[j].format);
            if (format == VK_FORMAT_UNDEFINED) {
                vkDestroyShaderModule(
                    vulkan.device,
                    vertShader,
                    NULL
                );
                vkDestroyShaderModule(
                    vulkan.device,
                    fragShader,
                    NULL
                );
                return 1;
            }

            attrDescs[j].binding = 0;
            attrDescs[j].location = vertexLayout.attributes[j].location;
            attrDescs[j].format = format;
            attrDescs[j].offset = vertexLayout.attributes[j].vanillaOffset;
        }

        VkPipelineVertexInputStateCreateInfo vertexInput = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &bindingDesc,
            .vertexAttributeDescriptionCount = vertexLayout.attributeCount,
            .pVertexAttributeDescriptions = attrDescs
        };

        VkPrimitiveTopology topology = getPrimitiveTopology(info->topology);
        VkPolygonMode polygonMode = getPolygonMode(info->polygonMode);
        VkCullModeFlags cullMode = getCullMode(info->cullMode);
        VkFrontFace frontFace = getFrontFace(info->frontFace);
        VkCompareOp depthCompareOp = getCompareOp(info->depthCompareOp);
        VkSampleCountFlagBits sampleCount = getSampleCount(info->samples);

        if (topology == VK_PRIMITIVE_TOPOLOGY_MAX_ENUM ||
            polygonMode == VK_POLYGON_MODE_MAX_ENUM ||
            cullMode == VK_CULL_MODE_FLAG_BITS_MAX_ENUM ||
            frontFace == VK_FRONT_FACE_MAX_ENUM ||
            depthCompareOp == VK_COMPARE_OP_MAX_ENUM ||
            sampleCount == VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM) {

            vkDestroyShaderModule(
                vulkan.device,
                vertShader,
                NULL
            );
            vkDestroyShaderModule(
                vulkan.device,
                fragShader,
                NULL
            );
            return 1;
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = topology,
            .primitiveRestartEnable = VK_FALSE
        };

        VkPipelineViewportStateCreateInfo viewportState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1
        };

        VkPipelineRasterizationStateCreateInfo rasterizer = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = polygonMode,
            .lineWidth = 1.0f,
            .cullMode = cullMode,
            .frontFace = frontFace,
            .depthBiasEnable = VK_FALSE
        };

        VkPipelineMultisampleStateCreateInfo multisampling = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .sampleShadingEnable = info->sampleShadingEnable ? VK_TRUE : VK_FALSE,
            .rasterizationSamples = sampleCount,
            .minSampleShading = info->minSampleShading
        };

        VkStencilOpState stencilFront = getStencilState(&info->stencilFront);
        VkStencilOpState stencilBack = getStencilState(&info->stencilBack);

        VkPipelineDepthStencilStateCreateInfo depthStencil = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = info->depthTestEnable ? VK_TRUE : VK_FALSE,
            .depthWriteEnable = info->depthWriteEnable ? VK_TRUE : VK_FALSE,
            .depthCompareOp = depthCompareOp,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = info->stencilTestEnable ? VK_TRUE : VK_FALSE,
            .front = stencilFront,
            .back = stencilBack
        };

        VkBlendFactor srcColorBlendFactor = getBlendFactor(info->srcColorBlendFactor);
        VkBlendFactor dstColorBlendFactor = getBlendFactor(info->dstColorBlendFactor);
        VkBlendFactor srcAlphaBlendFactor = getBlendFactor(info->srcAlphaBlendFactor);
        VkBlendFactor dstAlphaBlendFactor = getBlendFactor(info->dstAlphaBlendFactor);
        VkBlendOp colorBlendOp = getBlendOp(info->colorBlendOp);
        VkBlendOp alphaBlendOp = getBlendOp(info->alphaBlendOp);

        if (srcColorBlendFactor == VK_BLEND_FACTOR_MAX_ENUM ||
            dstColorBlendFactor == VK_BLEND_FACTOR_MAX_ENUM ||
            srcAlphaBlendFactor == VK_BLEND_FACTOR_MAX_ENUM ||
            dstAlphaBlendFactor == VK_BLEND_FACTOR_MAX_ENUM ||
            colorBlendOp == VK_BLEND_OP_MAX_ENUM ||
            alphaBlendOp == VK_BLEND_OP_MAX_ENUM) {

            vkDestroyShaderModule(
                vulkan.device,
                vertShader,
                NULL
            );
            vkDestroyShaderModule(
                vulkan.device,
                fragShader,
                NULL
            );
            return 1;
        }

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {
            .blendEnable = info->blendEnable ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = srcColorBlendFactor,
            .dstColorBlendFactor = dstColorBlendFactor,
            .colorBlendOp = colorBlendOp,
            .srcAlphaBlendFactor = srcAlphaBlendFactor,
            .dstAlphaBlendFactor = dstAlphaBlendFactor,
            .alphaBlendOp = alphaBlendOp,
            .colorWriteMask = getColorWriteMask(info->colorWriteMask)
        };

        VkPipelineColorBlendStateCreateInfo colorBlending = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment
        };

        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = dynamicStates
        };

        VkPipelineRenderingCreateInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &vulkan.swapchainFormat,
            .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT
        };

        VkGraphicsPipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2,
            .pStages = stages,
            .pVertexInputState = &vertexInput,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = &depthStencil,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = vulkan.pipelineLayout,
            .renderPass = VK_NULL_HANDLE,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
            .pNext = &renderingInfo
        };

        VkResult result = vkCreateGraphicsPipelines(
            vulkan.device,
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            NULL,
            &vulkan.pipelines[i]
        );

        vkDestroyShaderModule(
            vulkan.device,
            vertShader,
            NULL
        );

        vkDestroyShaderModule(
            vulkan.device,
            fragShader,
            NULL
        );

        if (result != VK_SUCCESS) {
            vulkan.pipelines[i] = VK_NULL_HANDLE;
            return 1;
        }
    }

    return 0;
}

void VANILLA_VK_destroyPipeline(void) {
    if (vulkan.pipelines == NULL) { return; }

    for (uint32_t i = 0; i < vulkan.pipelineCount; i++) {
        if (vulkan.pipelines[i] != VK_NULL_HANDLE) {
            vkDestroyPipeline(
                vulkan.device,
                vulkan.pipelines[i],
                NULL
            );
            vulkan.pipelines[i] = VK_NULL_HANDLE;
        }
    }
}

int VANILLA_VK_createPipelineImages(void) {
    if (!vulkan.pipelineCount) { return 0; }

    vulkan.pipelineImages = (VkImage*)VANILLA_mmap(sizeof(VkImage) * vulkan.pipelineCount);
    vulkan.pipelineImageMemories = (VkDeviceMemory*)VANILLA_mmap(sizeof(VkDeviceMemory) * vulkan.pipelineCount);
    vulkan.pipelineImageViews = (VkImageView*)VANILLA_mmap(sizeof(VkImageView) * vulkan.pipelineCount);

    if (!vulkan.pipelineImages || !vulkan.pipelineImageMemories || !vulkan.pipelineImageViews) {
        VANILLA_VK_destroyPipelineImages();
        return 1;
    }

    memset(vulkan.pipelineImages, 0, sizeof(VkImage) * vulkan.pipelineCount);
    memset(vulkan.pipelineImageMemories, 0, sizeof(VkDeviceMemory) * vulkan.pipelineCount);
    memset(vulkan.pipelineImageViews, 0, sizeof(VkImageView) * vulkan.pipelineCount);

    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = {
            .width = vulkan.swapchainExtent.width,
            .height = vulkan.swapchainExtent.height,
            .depth = 1
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = vulkan.swapchainFormat,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    for (uint16_t i = 0; i < vulkan.pipelineCount; i++) {
        if (vkCreateImage(
            vulkan.device,
            &imageInfo,
            NULL,
            &vulkan.pipelineImages[i]) != VK_SUCCESS) {
            
            VANILLA_VK_destroyPipelineImages();
            return 1;
        }

        VkMemoryRequirements memoryRequirements;
        vkGetImageMemoryRequirements(
            vulkan.device,
            vulkan.pipelineImages[i],
            &memoryRequirements
        );

        vulkan.pipelineImageMemories[i] = VANILLA_gmmap(&memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vulkan.pipelineImageMemories[i] == VK_NULL_HANDLE) {
            VANILLA_VK_destroyPipelineImages();
            return 1;
        }

        if (vkBindImageMemory(vulkan.device, vulkan.pipelineImages[i], vulkan.pipelineImageMemories[i], 0) != VK_SUCCESS) {
            VANILLA_VK_destroyPipelineImages();
            return 1;
        }

        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vulkan.pipelineImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vulkan.swapchainFormat,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        if (vkCreateImageView(vulkan.device, &viewInfo, NULL, &vulkan.pipelineImageViews[i]) != VK_SUCCESS) {
            VANILLA_VK_destroyPipelineImages();
            return 1;
        }
    }

    return 0;
}

int VANILLA_VK_createPostProcessingImages(void) {
    vulkan.postProcessingImages = (VkImage*)VANILLA_mmap(sizeof(VkImage) * 2);
    vulkan.postProcessingImageMemories = (VkDeviceMemory*)VANILLA_mmap(sizeof(VkDeviceMemory) * 2);
    vulkan.postProcessingImageViews = (VkImageView*)VANILLA_mmap(sizeof(VkImageView) * 2);

    if (vulkan.postProcessingImages == VANILLA_MAP_FAILED || vulkan.postProcessingImageMemories == VANILLA_MAP_FAILED || vulkan.postProcessingImageViews == VANILLA_MAP_FAILED) {
        VANILLA_VK_destroyPostProcessingImages();
        return 1;
    }

    memset(vulkan.postProcessingImages, 0, sizeof(VkImage) * 2);
    memset(vulkan.postProcessingImageMemories, 0, sizeof(VkDeviceMemory) * 2);
    memset(vulkan.postProcessingImageViews, 0, sizeof(VkImageView) * 2);

    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = {
            .width = vulkan.swapchainExtent.width,
            .height = vulkan.swapchainExtent.height,
            .depth = 1
        },
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = vulkan.swapchainFormat,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    for (uint32_t i = 0; i < 2; i++) {
        if (vkCreateImage(
            vulkan.device,
            &imageInfo,
            NULL,
            &vulkan.postProcessingImages[i]) != VK_SUCCESS) {

            VANILLA_VK_destroyPostProcessingImages();
            return 1;
        }

        VkMemoryRequirements memoryRequirements;
        vkGetImageMemoryRequirements(
            vulkan.device,
            vulkan.postProcessingImages[i],
            &memoryRequirements
        );

        vulkan.postProcessingImageMemories[i] = VANILLA_gmmap(&memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vulkan.postProcessingImageMemories[i] == VK_NULL_HANDLE) {
            VANILLA_VK_destroyPostProcessingImages();
            return 1;
        }

        if (vkBindImageMemory(
            vulkan.device,
            vulkan.postProcessingImages[i],
            vulkan.postProcessingImageMemories[i],
            0) != VK_SUCCESS) {

            VANILLA_VK_destroyPostProcessingImages();
            return 1;
        }

        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vulkan.postProcessingImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vulkan.swapchainFormat,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        if (vkCreateImageView(
            vulkan.device,
            &viewInfo,
            NULL,
            &vulkan.postProcessingImageViews[i]) != VK_SUCCESS) {

            VANILLA_VK_destroyPostProcessingImages();
            return 1;
        }
    }

    return 0;
}

void VANILLA_VK_destroyPipelineImages(void) {
    if (!vulkan.pipelineCount) { return; }

    if (vulkan.pipelineImageViews) {
        for (uint16_t i = 0; i < vulkan.pipelineCount; i++) {
            if (vulkan.pipelineImageViews[i]) {
                vkDestroyImageView(
                    vulkan.device,
                    vulkan.pipelineImageViews[i],
                    NULL
                );
            }
        }
        VANILLA_munmap(vulkan.pipelineImageViews, sizeof(VkImageView) * vulkan.pipelineCount);
        vulkan.pipelineImageViews = NULL;
    }

    if (vulkan.pipelineImages) {
        for (uint16_t i = 0; i < vulkan.pipelineCount; i++) {
            if (vulkan.pipelineImages[i]) {
                vkDestroyImage(
                    vulkan.device,
                    vulkan.pipelineImages[i],
                    NULL
                );
            }
        }
        VANILLA_munmap(vulkan.pipelineImages, sizeof(VkImage) * vulkan.pipelineCount);
        vulkan.pipelineImages = NULL;
    }

    if (vulkan.pipelineImageMemories) {
        for (uint16_t i = 0; i < vulkan.pipelineCount; i++) {
            if (vulkan.pipelineImageMemories[i]) {
                VANILLA_gmunmap(vulkan.pipelineImageMemories[i]);
            }
        }
        VANILLA_munmap(vulkan.pipelineImageMemories, sizeof(VkDeviceMemory) * vulkan.pipelineCount);
        vulkan.pipelineImageMemories = NULL;
    }
}

void VANILLA_VK_destroyPostProcessingImages(void) {
    if (vulkan.postProcessingImageViews) {
        for (uint32_t i = 0; i < 2; i++) {
            if (vulkan.postProcessingImageViews[i]) {
                vkDestroyImageView(
                    vulkan.device,
                    vulkan.postProcessingImageViews[i],
                    NULL
                );
            }
        }
        VANILLA_munmap(vulkan.postProcessingImageViews, sizeof(VkImageView) * 2);
        vulkan.postProcessingImageViews = NULL;
    }

    if (vulkan.postProcessingImages) {
        for (uint32_t i = 0; i < 2; i++) {
            if (vulkan.postProcessingImages[i]) {
                vkDestroyImage(
                    vulkan.device,
                    vulkan.postProcessingImages[i],
                    NULL
                );
            }
        }
        VANILLA_munmap(vulkan.postProcessingImages, sizeof(VkImage) * 2);
        vulkan.postProcessingImages = NULL;
    }

    if (vulkan.postProcessingImageMemories) {
        for (uint32_t i = 0; i < 2; i++) {
            if (vulkan.postProcessingImageMemories[i]) {
                VANILLA_gmunmap(vulkan.postProcessingImageMemories[i]);
            }
        }
        VANILLA_munmap(vulkan.postProcessingImageMemories, sizeof(VkDeviceMemory) * 2);
        vulkan.postProcessingImageMemories = NULL;
    }
}

VANILLA_EXPORT VANILLA_RESULT VANILLA_configurePipelines(const VANILLA_PipelineCreateInfo* infos, uint16_t count) {
    if (didstart == true) { return VANILLA_ERR_ALREADY_STARTED; }
    if (pipelineInfos != NULL || count == 0) { return VANILLA_SUCCESS; }
    if (infos == NULL) { return VANILLA_ERR_NULL_POINTER; }

    pipelineInfos = VANILLA_mmap(sizeof(VANILLA_PipelineCreateInfo) * count);
    if (pipelineInfos == VANILLA_MAP_FAILED) {
        pipelineInfos = NULL;
        return VANILLA_ERR_ALLOC_FAILED;
    }

    vulkan.pipelines = VANILLA_mmap(sizeof(VkPipeline) * count);
    if (vulkan.pipelines == VANILLA_MAP_FAILED) {
        VANILLA_munmap(pipelineInfos, sizeof(VANILLA_PipelineCreateInfo) * count);
        pipelineInfos = NULL;
        vulkan.pipelines = NULL;
        return VANILLA_ERR_ALLOC_FAILED;
    }

    memcpy(pipelineInfos, infos, sizeof(VANILLA_PipelineCreateInfo) * count);
    vulkan.pipelineCount = count;

    return VANILLA_SUCCESS;
}

VANILLA_EXPORT void VANILLA_setPipelineIndex(EntityID entityID, uint16_t pipelineidx, bool didcreate) {
    if (entityID.slot >= stream->chunkLimit * CHUNKSIZE) { return; }
    vulkan.entityPipelineIndices[entityID.slot] = pipelineidx;

    if (didcreate) { return; }

    const uint32_t entityCapacity = stream->chunkLimit * CHUNKSIZE;
    VANILLA_GPUMaterialSlot* gpuMaterial = &((VANILLA_GPUMaterialSlot*)vulkan.mappedMaterialSlotBufferPtr)[entityID.slot];

    const uint32_t renderEnabled = ((gpuMaterial->flags1 & VANILLA_ENTITYFLAGS1_NO_RENDER) == 0u);
    for (uint16_t currentPipeline = 0; currentPipeline < vulkan.pipelineCount; currentPipeline++) {
        const uint32_t indirectIndex = ((uint32_t)currentPipeline * entityCapacity) + entityID.slot;
        vulkan.mappedIndirectCommands[indirectIndex].instanceCount = (currentPipeline == pipelineidx && renderEnabled) ? 1 : 0;
    }
}

///

int VANILLA_VK_createPostProcessingPipeline(void) {
    if (vulkan.postProcessingCount == 0) { return 0; }
    if (postProcessingInfos == NULL) { return 1; }

    vulkan.postProcessingPipelines = VANILLA_mmap(sizeof(VkPipeline) * vulkan.postProcessingCount );
    if (vulkan.postProcessingPipelines == VANILLA_MAP_FAILED) { 
        vulkan.postProcessingPipelines = NULL;
        return 1;
    }

    for (uint16_t i = 0; i < vulkan.postProcessingCount; i++) {
        const VANILLA_PostProcessingCreateInfo* info = &postProcessingInfos[i];
        if (info->vertexShader == NULL || info->fragmentShader == NULL) { return 1; }

        VkShaderModule vertShader = createShaderModule(info->vertexShader, info->vertexShaderSize);

        if (vertShader == VK_NULL_HANDLE) { return 1; }

        VkShaderModule fragShader = createShaderModule(info->fragmentShader, info->fragmentShaderSize);
        if (fragShader == VK_NULL_HANDLE) {
            vkDestroyShaderModule(
                vulkan.device,
                vertShader,
                NULL
            );
            return 1;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertShader,
                .pName = "main"
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragShader,
                .pName = "main"
            }
        };

        VkPipelineVertexInputStateCreateInfo vertexInput = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 0,
            .pVertexBindingDescriptions = NULL,
            .vertexAttributeDescriptionCount = 0,
            .pVertexAttributeDescriptions = NULL
        };

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE
        };

        VkPipelineViewportStateCreateInfo viewportState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1
        };

        VkPipelineRasterizationStateCreateInfo rasterizer = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .lineWidth = 1.0f,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE
        };

        VkPipelineMultisampleStateCreateInfo multisampling = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .sampleShadingEnable = VK_FALSE,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
        };

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {
            .blendEnable = VK_FALSE,
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
            .colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT
        };

        VkPipelineColorBlendStateCreateInfo colorBlending = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = VK_FALSE,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment
        };

        VkDynamicState dynamicStates[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamicState = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates = dynamicStates
        };

        VkPipelineRenderingCreateInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
            .colorAttachmentCount = 1,
            .pColorAttachmentFormats = &vulkan.swapchainFormat,
            .depthAttachmentFormat = VK_FORMAT_UNDEFINED,
            .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
        };

        VkGraphicsPipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount = 2,
            .pStages = stages,
            .pVertexInputState = &vertexInput,
            .pInputAssemblyState = &inputAssembly,
            .pViewportState = &viewportState,
            .pRasterizationState = &rasterizer,
            .pMultisampleState = &multisampling,
            .pDepthStencilState = NULL,
            .pColorBlendState = &colorBlending,
            .pDynamicState = &dynamicState,
            .layout = vulkan.postProcessingPipelineLayout,
            .renderPass = VK_NULL_HANDLE,
            .subpass = 0,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = -1,
            .pNext = &renderingInfo
        };

        VkResult result = vkCreateGraphicsPipelines(
            vulkan.device,
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            NULL,
            &vulkan.postProcessingPipelines[i]
        );

        vkDestroyShaderModule(
            vulkan.device,
            vertShader,
            NULL
        );

        vkDestroyShaderModule(
            vulkan.device,
            fragShader,
            NULL
        );

        if (result != VK_SUCCESS) {
            vulkan.postProcessingPipelines[i] = VK_NULL_HANDLE;
            return 1;
        }
    }

    return 0;
}

void VANILLA_VK_destroyPostProcessingPipeline(void) {
    if (vulkan.postProcessingPipelines == NULL) { return; }

    for (uint16_t i = 0; i < vulkan.postProcessingCount; i++) {
        if (vulkan.postProcessingPipelines[i] != VK_NULL_HANDLE) {
            vkDestroyPipeline(
                vulkan.device,
                vulkan.postProcessingPipelines[i],
                NULL
            );
            vulkan.postProcessingPipelines[i] = VK_NULL_HANDLE;
        }
    }

    VANILLA_munmap(vulkan.postProcessingPipelines, sizeof(VkPipeline) * vulkan.postProcessingCount);
    vulkan.postProcessingPipelines = NULL;
}