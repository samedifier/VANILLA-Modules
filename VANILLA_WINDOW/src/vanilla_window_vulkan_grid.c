/*
 * VANILLA Modules
 * Copyright (C) 2026 Samed
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the LICENSE file for the full license text.
 */

/*
 * Editor grid (3D only, hidden by default).
 *
 * One quad on the plane y = height around the camera, drawn into the first HDR pipeline's
 * rendering after its entities and the sky: depth test against the scene, no depth write,
 * alpha blended. Lines are computed per pixel (constant pixel width, anti-aliased, distance fade),
 * so the cost is one draw call and does not depend on the grid size.
 * The pipeline follows the MSAA sample count (rebuilt by VANILLA_VK_applyAntiAliasingChanges).
 */

#include "vanilla_window_vulkan_utils.h"
#include "vanilla_window_grid_spv.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* std430 mirror of GridPush in grid.vert / grid.frag (128 bytes: the guaranteed push limit) */
typedef struct {
    mat4  viewProjection;
    float cameraHeight[4];   /* xyz camera position, w plane height */
    float params[4];         /* cell size, major every, fade distance, quad half extent */
    float minorColor[4];
    float majorColor[4];
} VANILLA_GridPush;

static VANILLA_GridInfo VANILLA_VK_defaultGrid(void) {
    return (VANILLA_GridInfo){
        .height       = 0.0f,
        .cellSize     = 1.0f,
        .majorEvery   = 10,
        .fadeDistance = 150.0f,
        .minorColor   = { 0.55f, 0.55f, 0.55f, 0.25f },
        .majorColor   = { 0.75f, 0.75f, 0.75f, 0.5f }
    };
}

static VANILLA_GridInfo gridInfo;
static bool             gridInfoSet = false;
static bool             gridVisible = false;

static bool             gridActive         = false;
static VkPipelineLayout gridPipelineLayout = VK_NULL_HANDLE;
static VkPipeline       gridPipeline       = VK_NULL_HANDLE;

/* --------------------------------------------------------- */
/* Pipeline                                                   */
/* --------------------------------------------------------- */

int VANILLA_VK_createGridPipeline(void) {
    if (!gridActive || gridPipelineLayout == VK_NULL_HANDLE) { return 0; }
    if (VANILLA_VK_firstHdrPipeline() < 0) { return 0; }   /* nothing to draw the grid into */

    VkShaderModuleCreateInfo vertInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = VANILLA_gridVertSpv_len * sizeof(uint32_t),
        .pCode = VANILLA_gridVertSpv
    };
    VkShaderModuleCreateInfo fragInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = VANILLA_gridFragSpv_len * sizeof(uint32_t),
        .pCode = VANILLA_gridFragSpv
    };

    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vulkan.device, &vertInfo, NULL, &vert) != VK_SUCCESS) { return 1; }
    if (vkCreateShaderModule(vulkan.device, &fragInfo, NULL, &frag) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkan.device, vert, NULL);
        return 1;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,   .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" }
    };

    VkPipelineVertexInputStateCreateInfo vertexInput = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,          /* visible from above and below */
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VANILLA_VK_msaaSamples()
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
    };
    VkPipelineColorBlendAttachmentState blend = {
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,     /* keep the destination alpha */
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo colorBlend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend
    };
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates
    };

    const VkFormat colorFormat = VANILLA_VK_pipelineColorFormat((uint16_t)VANILLA_VK_firstHdrPipeline());
    VkPipelineRenderingCreateInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT
    };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = gridPipelineLayout,
        .basePipelineIndex = -1
    };

    VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &gridPipeline);

    vkDestroyShaderModule(vulkan.device, vert, NULL);
    vkDestroyShaderModule(vulkan.device, frag, NULL);

    if (result != VK_SUCCESS) {
        gridPipeline = VK_NULL_HANDLE;
        return 1;
    }
    return 0;
}

void VANILLA_VK_destroyGridPipeline(void) {
    if (gridPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkan.device, gridPipeline, NULL);
        gridPipeline = VK_NULL_HANDLE;
    }
}

/* --------------------------------------------------------- */
/* Initialization / destruction                               */
/* --------------------------------------------------------- */

int VANILLA_VK_initGrid(void) {
    gridActive = (GET_DIMENSION(ANYCOREBuildID) == 3);
    if (!gridActive) { return 0; }

    if (!gridInfoSet) {
        gridInfo    = VANILLA_VK_defaultGrid();
        gridInfoSet = true;
    }

    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(VANILLA_GridPush)
    };

    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };

    if (vkCreatePipelineLayout(vulkan.device, &layoutInfo, NULL, &gridPipelineLayout) != VK_SUCCESS) {
        gridPipelineLayout = VK_NULL_HANDLE;
        goto fail;
    }

    if (VANILLA_VK_createGridPipeline()) { goto fail; }
    return 0;

fail:
    printf("[VANILLA WINDOW] grid init failed\n");
    VANILLA_VK_destroyGrid();
    return 1;
}

void VANILLA_VK_destroyGrid(void) {
    VANILLA_VK_destroyGridPipeline();
    if (gridPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkan.device, gridPipelineLayout, NULL);
        gridPipelineLayout = VK_NULL_HANDLE;
    }
    gridActive = false;
}

/* --------------------------------------------------------- */
/* Per frame                                                  */
/* --------------------------------------------------------- */

/* Inside the first HDR pipeline's rendering, after the sky (viewport already set). */
void VANILLA_VK_recordGrid(VkCommandBuffer commandBuffer, uint16_t pipelineIndex, const VANILLA_CameraPush* campush) {
    if (!gridActive || !gridVisible || gridPipeline == VK_NULL_HANDLE) { return; }
    if ((int32_t)pipelineIndex != VANILLA_VK_firstHdrPipeline()) { return; }

    VANILLA_GridPush push = {
        .viewProjection = campush->viewProjection,
        .cameraHeight = { campush->cameraPosition[0], campush->cameraPosition[1], campush->cameraPosition[2], gridInfo.height },
        .params = { gridInfo.cellSize, (float)gridInfo.majorEvery, gridInfo.fadeDistance, gridInfo.fadeDistance },
        .minorColor = { gridInfo.minorColor.x, gridInfo.minorColor.y, gridInfo.minorColor.z, gridInfo.minorColor.w },
        .majorColor = { gridInfo.majorColor.x, gridInfo.majorColor.y, gridInfo.majorColor.z, gridInfo.majorColor.w }
    };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gridPipeline);
    vkCmdPushConstants(commandBuffer, gridPipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);
}

/* --------------------------------------------------------- */
/* API                                                        */
/* --------------------------------------------------------- */

VANILLA_EXPORT VANILLA_GridInfo VANILLA_getDefaultGrid(void) {
    return VANILLA_VK_defaultGrid();
}

VANILLA_EXPORT void VANILLA_setGrid(const VANILLA_GridInfo* info) {
    if (info == NULL) { return; }

    VANILLA_GridInfo v = *info;
    if (!(v.cellSize > 1e-4f))     { v.cellSize = 1e-4f; }
    if (v.majorEvery == 0)         { v.majorEvery = 1; }
    if (!(v.fadeDistance > 1e-3f)) { v.fadeDistance = 1e-3f; }

    gridInfo    = v;
    gridInfoSet = true;
}

VANILLA_EXPORT VANILLA_GridInfo VANILLA_getGrid(void) {
    return gridInfoSet ? gridInfo : VANILLA_VK_defaultGrid();
}

VANILLA_EXPORT void VANILLA_setGridVisible(bool visible) { gridVisible = visible; }
VANILLA_EXPORT bool VANILLA_isGridVisible(void)          { return gridVisible; }
