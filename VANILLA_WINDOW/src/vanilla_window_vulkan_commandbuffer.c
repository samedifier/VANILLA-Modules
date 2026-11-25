/*
 * VANILLA Modules
 * Copyright (C) 2026 Samed
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the LICENSE file for the full license text.
 */

#include "vanilla_window_vulkan_utils.h"
#include "vanilla_math.h"

#include "vanilla_window_camera.h"

int VANILLA_VK_createCommandBuffers(void) {
    vulkan.commandBuffers = VANILLA_mmap(sizeof(VkCommandBuffer) * vulkan.imageCount);

    if(vulkan.commandBuffers == NULL) { return 1; }

    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vulkan.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = vulkan.imageCount
    };

    VkResult result = vkAllocateCommandBuffers(vulkan.device, &allocInfo, vulkan.commandBuffers);

    if(result != VK_SUCCESS) {
        VANILLA_munmap(vulkan.commandBuffers, sizeof(VkCommandBuffer) * vulkan.imageCount);
        vulkan.commandBuffers = NULL;
        return 1;
    }

    return 0;
}

static void VANILLA_VK_bindSceneState(VkCommandBuffer commandBuffer, const VANILLA_CameraPush* campush, const VkViewport* viewport, const VkRect2D* scissor);

/* VANILLA_VK_drawList (cull.c): one alpha-mode list of a bound pipeline, culled when the last cullPass culled it. */

static bool VANILLA_VK_hasSolidDraws(uint16_t pipelineIndex) {
    return VANILLA_VK_indirectCount(pipelineIndex, VANILLA_ALPHAMODE_OPAQUE) > 0 ||
           VANILLA_VK_indirectCount(pipelineIndex, VANILLA_ALPHAMODE_MASK) > 0;
}

/* Opaque + mask lists with the pipeline's own depth write. */
static void VANILLA_VK_drawSolid(VkCommandBuffer commandBuffer, uint16_t pipelineIndex) {
    vkCmdSetDepthWriteEnable(commandBuffer, pipelineInfos[pipelineIndex].depthWriteEnable ? VK_TRUE : VK_FALSE);
    VANILLA_VK_drawList(commandBuffer, pipelineIndex, VANILLA_ALPHAMODE_OPAQUE);
    VANILLA_VK_drawList(commandBuffer, pipelineIndex, VANILLA_ALPHAMODE_MASK);
}

/* Blend list (sorted far -> near), depth tested but not written. */
static void VANILLA_VK_drawBlend(VkCommandBuffer commandBuffer, uint16_t pipelineIndex) {
    vkCmdSetDepthWriteEnable(commandBuffer, VK_FALSE);
    VANILLA_VK_drawList(commandBuffer, pipelineIndex, VANILLA_ALPHAMODE_BLEND);
}

/*
 * Post-processing path: one pipeline into its own image (pipelineImages[p]), which the
 * post-processing shaders sample afterwards.
 *   LDR, no MSAA : draw into pipelineImages[p]
 *   LDR, MSAA    : draw into the MSAA image, hardware resolve into pipelineImages[p]
 *   HDR          : draw into the HDR image (MSAA or not), tone map (+ FXAA) into pipelineImages[p]
 */
static void VANILLA_VK_recordPipelinePass(VkCommandBuffer commandBuffer, uint16_t pipelineIndex, const VkClearColorValue* clearColor, const VANILLA_CameraPush* campush, const VkViewport* viewport, const VkRect2D* scissor) {
    const bool hdr  = VANILLA_VK_isHdrPipeline(pipelineIndex);
    const bool msaa = VANILLA_VK_msaaSamples() > VK_SAMPLE_COUNT_1_BIT;
    const bool hzScene = hdr && (int32_t)pipelineIndex == VANILLA_VK_firstHdrPipeline() && VANILLA_VK_hiZWanted();
    const bool fxaa = hdr && VANILLA_VK_fxaaEnabled();

    VkImage     drawImage;
    VkImageView drawView;
    VkImageView resolveView = VK_NULL_HANDLE;

    if (hdr) {
        drawImage = VANILLA_VK_hdrImage(pipelineIndex);
        drawView  = VANILLA_VK_hdrView(pipelineIndex);
    }
    else if (msaa) {
        drawImage   = VANILLA_VK_msaaColorImage(pipelineIndex);
        drawView    = VANILLA_VK_msaaColorView(pipelineIndex);
        resolveView = vulkan.pipelineImageViews[pipelineIndex];
    }
    else {
        drawImage = vulkan.pipelineImages[pipelineIndex];
        drawView  = vulkan.pipelineImageViews[pipelineIndex];
    }

    /* Depth: previous pass finished with it; colors start undefined (cleared / resolved). */
    VkMemoryBarrier2 depthMemoryBarrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
    };
    VkDependencyInfo depthDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &depthMemoryBarrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &depthDependency);

    VANILLA_VK_colorBarrier(commandBuffer, drawImage, true);
    if (resolveView != VK_NULL_HANDLE) {
        VANILLA_VK_colorBarrier(commandBuffer, vulkan.pipelineImages[pipelineIndex], true);
    }

    VkRenderingAttachmentInfo colorAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = drawView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = resolveView != VK_NULL_HANDLE ? VK_RESOLVE_MODE_AVERAGE_BIT : VK_RESOLVE_MODE_NONE,
        .resolveImageView = resolveView,
        .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = resolveView != VK_NULL_HANDLE ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.color = *clearColor
    };

    VkRenderingAttachmentInfo depthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = msaa ? VANILLA_VK_msaaDepthView() : vulkan.depthImageView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        /* The scene's depth feeds the occlusion pyramid; otherwise nobody reads it after the pass. */
        .storeOp = (hzScene) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue.depthStencil = { 1.0f, 0 }
    };

    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = {0, 0}, .extent = vulkan.swapchainExtent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
        .pStencilAttachment = NULL
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    VANILLA_VK_bindSceneState(commandBuffer, campush, viewport, scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan.pipelines[pipelineIndex]);
    VANILLA_VK_drawSolid(commandBuffer, pipelineIndex);

    /*
     * Order: solid -> sky (pixels no entity covered, first HDR pipeline) -> transparent (far to near)
     * -> editor grid. The sky binds its own layout, so the scene state is bound again for the blend list.
     */
    if (hdr) { VANILLA_VK_recordSky(commandBuffer, pipelineIndex); }

    if (VANILLA_VK_indirectCount(pipelineIndex, VANILLA_ALPHAMODE_BLEND) > 0) {
        VANILLA_VK_bindSceneState(commandBuffer, campush, viewport, scissor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan.pipelines[pipelineIndex]);
        VANILLA_VK_drawBlend(commandBuffer, pipelineIndex);
    }

    if (hdr) { VANILLA_VK_recordGrid(commandBuffer, pipelineIndex, campush); }

    vkCmdEndRendering(commandBuffer);

    /* Occlusion: pyramid of this frame's scene depth for the next frame's cull. */
    if (hzScene) { VANILLA_VK_buildHiZ(commandBuffer, &campush->viewProjection); }

    if (hdr) {
        VANILLA_VK_colorBarrier(commandBuffer, drawImage, false);

        if (fxaa) {
            /* tone map -> FXAA input -> FXAA -> pipeline image */
            VANILLA_VK_colorBarrier(commandBuffer, VANILLA_VK_fxaaInputImage(pipelineIndex), true);
            VANILLA_VK_beginFullscreen(commandBuffer, VANILLA_VK_fxaaInputView(pipelineIndex));
            VANILLA_VK_recordToneMap(commandBuffer, pipelineIndex, false);
            vkCmdEndRendering(commandBuffer);
            VANILLA_VK_colorBarrier(commandBuffer, VANILLA_VK_fxaaInputImage(pipelineIndex), false);

            VANILLA_VK_colorBarrier(commandBuffer, vulkan.pipelineImages[pipelineIndex], true);
            VANILLA_VK_beginFullscreen(commandBuffer, vulkan.pipelineImageViews[pipelineIndex]);
            VANILLA_VK_recordFxaa(commandBuffer, pipelineIndex, false);
            vkCmdEndRendering(commandBuffer);
        }
        else {
            VANILLA_VK_colorBarrier(commandBuffer, vulkan.pipelineImages[pipelineIndex], true);
            VANILLA_VK_beginFullscreen(commandBuffer, vulkan.pipelineImageViews[pipelineIndex]);
            VANILLA_VK_recordToneMap(commandBuffer, pipelineIndex, false);
            vkCmdEndRendering(commandBuffer);
        }
    }

    /* The post-processing shaders sample the pipeline image. */
    VANILLA_VK_colorBarrier(commandBuffer, vulkan.pipelineImages[pipelineIndex], false);
}

/* Push constants, viewport, buffers and the scene descriptor set for the graphics pipelines. */
static void VANILLA_VK_bindSceneState(VkCommandBuffer commandBuffer, const VANILLA_CameraPush* campush, const VkViewport* viewport, const VkRect2D* scissor) {
    VkBuffer vertexBuffers[] = { vulkan.globalVertexBuffer };
    VkDeviceSize offsets[] = { 0 };

    vkCmdPushConstants(commandBuffer, vulkan.pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(VANILLA_CameraPush), campush);

    vkCmdSetViewport(commandBuffer, 0, 1, viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, scissor);

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, vulkan.globalIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        vulkan.pipelineLayout, 0, 1, &vulkan.descriptorSet, 0, NULL);
}

/* Solid (opaque + mask) lists of every HDR pipeline (hdr = true) or every non-HDR pipeline, in order. */
static void VANILLA_VK_drawPipelines(VkCommandBuffer commandBuffer, bool hdr) {
    for (uint16_t pipelineIndex = 0; pipelineIndex < vulkan.pipelineCount; pipelineIndex++) {
        if (VANILLA_VK_isHdrPipeline(pipelineIndex) != hdr) { continue; }
        if (!VANILLA_VK_hasSolidDraws(pipelineIndex)) { continue; }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan.pipelines[pipelineIndex]);
        VANILLA_VK_drawSolid(commandBuffer, pipelineIndex);
    }
}

/* Blend lists of the same pipelines, after the solids (and the sky). Scene state must be bound. */
static void VANILLA_VK_drawPipelinesBlend(VkCommandBuffer commandBuffer, bool hdr) {
    for (uint16_t pipelineIndex = 0; pipelineIndex < vulkan.pipelineCount; pipelineIndex++) {
        if (VANILLA_VK_isHdrPipeline(pipelineIndex) != hdr) { continue; }
        if (VANILLA_VK_indirectCount(pipelineIndex, VANILLA_ALPHAMODE_BLEND) == 0) { continue; }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan.pipelines[pipelineIndex]);
        VANILLA_VK_drawBlend(commandBuffer, pipelineIndex);
    }
}

/*
 * Swapchain layout transitions. The acquire semaphore is waited at COLOR_ATTACHMENT_OUTPUT,
 * so the first barrier starts from that stage; the second hands the image to present.
 */
static void VANILLA_VK_swapchainBarrier(VkCommandBuffer commandBuffer, VkImage image, bool toAttachment) {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    if (toAttachment) {
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else {
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask = VK_ACCESS_2_NONE;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

int VANILLA_VK_recordCommandBuffer(uint32_t imageIndex, uint32_t activeEntityCount) {
    VkCommandBuffer commandBuffer = vulkan.commandBuffers[imageIndex];

    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) { return 1; }

    if (vulkan.mappedUploadTransformBufferPtr != NULL) {
        if (vulkan.transformBufferCopy.size > 0) {
            vkCmdCopyBuffer(
                commandBuffer,
                vulkan.uploadGlobalTransformBuffer,
                vulkan.globalTransformBuffer,
                1,
                &vulkan.transformBufferCopy
            );
        }
    }

    if (vulkan.mappedUploadModelLookupPtr != NULL) {
        if (vulkan.modelLookupBufferCopy.size > 0) {
            vkCmdCopyBuffer(
                commandBuffer,
                vulkan.uploadModelLookupBuffer,
                vulkan.modelLookupBuffer,
                1,
                &vulkan.modelLookupBufferCopy
            );
        }
    }

    if (vulkan.mappedUploadVertexBufferPtr != NULL) {
        if (vulkan.vertexBufferCopy.size > 0) {
            vkCmdCopyBuffer(
                commandBuffer,
                vulkan.uploadGlobalVertexBuffer,
                vulkan.globalVertexBuffer,
                1,
                &vulkan.vertexBufferCopy
            );
        }
    }

    if (vulkan.mappedUploadIndexBufferPtr != NULL) {
        if (vulkan.indexBufferCopy.size > 0) {
            vkCmdCopyBuffer(
                commandBuffer,
                vulkan.uploadGlobalIndexBuffer,
                vulkan.globalIndexBuffer,
                1,
                &vulkan.indexBufferCopy
            );
        }
    }

    if (memoryMode != VANILLA_MEMORY_HOST_VISIBLE) {
        if (vulkan.materialCopy.size > 0) {
            vkCmdCopyBuffer(
                commandBuffer,
                vulkan.materialUploadBuffer,
                vulkan.materialBuffer,
                1,
                &vulkan.materialCopy
            );
        }
    }

    if (vulkan.mappedUploadAliveFlagsPtr != NULL) {
        if (vulkan.aliveFlagsCopy.size > 0) {
            vkCmdCopyBuffer(
                commandBuffer,
                vulkan.uploadAliveFlagsBuffer,
                vulkan.aliveFlagsBuffer,
                1,
                &vulkan.aliveFlagsCopy
            );
        }
    }

    VANILLA_VK_recordIndirectUpload(commandBuffer);
    VANILLA_VK_recordCullUploads(commandBuffer);
    VANILLA_VK_recordUserBufferWrites(commandBuffer);
    VANILLA_VK_recordLighting(commandBuffer);
    VANILLA_VK_recordEnvironment(commandBuffer);

    VkBufferMemoryBarrier2 barriers[] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vulkan.globalVertexBuffer,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vulkan.globalIndexBuffer,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vulkan.globalTransformBuffer,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vulkan.modelLookupBuffer,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vulkan.materialBuffer,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
            .srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vulkan.materialSlotBuffer,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
            .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vulkan.indirectCommandBuffer,
            .offset = 0,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vulkan.aliveFlagsBuffer,
            .offset = 0,
            .size = VK_WHOLE_SIZE
        }
    };

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        .bufferMemoryBarrierCount = sizeof(barriers) / sizeof(barriers[0]),
        .pBufferMemoryBarriers = barriers
    };

    vkCmdPipelineBarrier2(
        commandBuffer,
        &dependencyInfo
    );

    VANILLA_VK_recordSkinning(commandBuffer);   /* deformed meshes before anyone reads vertices */
    VANILLA_VK_recordComputeDispatches(commandBuffer);
    VANILLA_VK_recordShadowPass(commandBuffer);

    vulkan.vertexBufferCopy.size = 0;
    vulkan.indexBufferCopy.size = 0;
    vulkan.modelLookupBufferCopy.size = 0;
    vulkan.transformBufferCopy.size = 0;
    vulkan.materialCopy.size = 0;
    vulkan.aliveFlagsCopy.size = 0;

    vulkan.maxmcslot = 0;

    mat4 proj = {0};
    mat4 view;
    mat4 viewProj;

    float aspect = (float)vulkan.swapchainExtent.width / (float)vulkan.swapchainExtent.height;

    switch (projectionType) {
        case VANILLA_PROJECTION_PERSPECTIVE:
            VANILLA_MATH_perspective(
                &proj,
                camera.fov,
                aspect,
                camera.nearPlane,
                camera.farPlane
            );
            break;
        case VANILLA_PROJECTION_ORTHOGRAPHIC: {
            float height = orthographicSize;
            float width = height * aspect;

            VANILLA_MATH_orthographic(
                &proj,
                -width * 0.5f,
                width * 0.5f,
                -height * 0.5f,
                height * 0.5f,
                camera.nearPlane,
                camera.farPlane
            );
            break;
        }
        default:
            VANILLA_MATH_perspective(
                &proj,
                camera.fov,
                aspect,
                camera.nearPlane,
                camera.farPlane
            );
            break;
    }

    VANILLA_MATH_lookAt(
        &view,

        camera.posx,
        camera.posy,
        camera.posz,

        camera.posx + camera.targetx,
        camera.posy + camera.targety,
        camera.posz + camera.targetz,

        camera.upx,
        camera.upy,
        camera.upz
    );

    VANILLA_MATH_multiply(
        &viewProj,
        &proj,
        &view
    );

    VANILLA_CameraPush campush = {
        .viewProjection = viewProj,
        .aspectRatio = aspect,
        .uioffset = { camera.uioffsetx, camera.uioffsety },
        ._padding1 = 0,
        .cameraPosition = { camera.posx, camera.posy, camera.posz }
    };

    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)vulkan.swapchainExtent.height,
        .width = (float)vulkan.swapchainExtent.width,
        .height = -(float)vulkan.swapchainExtent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };

    VkViewport postProcessViewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)vulkan.swapchainExtent.width,
        .height = (float)vulkan.swapchainExtent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    VkRect2D scissor = { .offset = {0, 0}, .extent = vulkan.swapchainExtent };

    /* GPU frustum culling for the main camera (the shadow passes culled their own lists already). */
    VANILLA_VK_cullPass(commandBuffer, &campush.viewProjection, false);

    /* Swapchain image: (acquired, contents discarded) -> color attachment. */
    VANILLA_VK_swapchainBarrier(commandBuffer, vulkan.images[imageIndex], true);

    if (!enablePostProcessing) {
        /*
         * Without post-processing every pipeline draws straight to the swapchain.
         * HDR pipelines (if any) first draw into one shared HDR image; it is tone mapped
         * (+ FXAA) onto the swapchain, then the other pipelines draw on top and still
         * depth test against the scene (the depth buffer is kept). With MSAA the
         * swapchain pass renders into a multisampled image resolved into the swapchain.
         */
        const int32_t hdrTarget = VANILLA_VK_firstHdrPipeline();
        const bool    msaa      = VANILLA_VK_msaaSamples() > VK_SAMPLE_COUNT_1_BIT;
        const bool    fxaa      = hdrTarget >= 0 && VANILLA_VK_fxaaEnabled();
        const VkImageView depthView = msaa ? VANILLA_VK_msaaDepthView() : vulkan.depthImageView;
        const VkClearColorValue directClear = { .float32 = { 0.7f, 0.7f, 0.7f, 1.0f } };

        if (hdrTarget >= 0) {
            const uint16_t h = (uint16_t)hdrTarget;

            VANILLA_VK_hdrBarrier(commandBuffer, h, true);

            VkRenderingAttachmentInfo hdrColor = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = VANILLA_VK_hdrView(h),
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_NONE,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue.color = directClear
            };

            VkRenderingAttachmentInfo hdrDepth = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = depthView,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .resolveMode = VK_RESOLVE_MODE_NONE,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue.depthStencil = { 1.0f, 0 }
            };

            VkRenderingInfo hdrRendering = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = { .offset = {0, 0}, .extent = vulkan.swapchainExtent },
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &hdrColor,
                .pDepthAttachment = &hdrDepth
            };

            vkCmdBeginRendering(commandBuffer, &hdrRendering);
            VANILLA_VK_bindSceneState(commandBuffer, &campush, &viewport, &scissor);
            VANILLA_VK_drawPipelines(commandBuffer, true);
            VANILLA_VK_recordSky(commandBuffer, h);
            VANILLA_VK_bindSceneState(commandBuffer, &campush, &viewport, &scissor);   /* the sky bound its own layout */
            VANILLA_VK_drawPipelinesBlend(commandBuffer, true);
            VANILLA_VK_recordGrid(commandBuffer, h, &campush);
            vkCmdEndRendering(commandBuffer);

            /* Occlusion: pyramid of this frame's scene depth for the next frame's cull. */
            VANILLA_VK_buildHiZ(commandBuffer, &campush.viewProjection);

            VANILLA_VK_hdrBarrier(commandBuffer, h, false);

            VkMemoryBarrier2 depthBarrier = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
            };
            VkDependencyInfo depthDependency = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .memoryBarrierCount = 1,
                .pMemoryBarriers = &depthBarrier
            };
            vkCmdPipelineBarrier2(commandBuffer, &depthDependency);

            if (fxaa) {
                /* Tone map into the FXAA input first; FXAA itself runs in the swapchain pass. */
                VANILLA_VK_colorBarrier(commandBuffer, VANILLA_VK_fxaaInputImage(h), true);
                VANILLA_VK_beginFullscreen(commandBuffer, VANILLA_VK_fxaaInputView(h));
                VANILLA_VK_recordToneMap(commandBuffer, h, false);
                vkCmdEndRendering(commandBuffer);
                VANILLA_VK_colorBarrier(commandBuffer, VANILLA_VK_fxaaInputImage(h), false);
            }
        }

        if (msaa) {
            VANILLA_VK_colorBarrier(commandBuffer, VANILLA_VK_msaaSwapchainImage(), true);
        }

        VkRenderingAttachmentInfo colorAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = msaa ? VANILLA_VK_msaaSwapchainView() : vulkan.imageViews[imageIndex],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = msaa ? VK_RESOLVE_MODE_AVERAGE_BIT : VK_RESOLVE_MODE_NONE,
            .resolveImageView = msaa ? vulkan.imageViews[imageIndex] : VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue.color = directClear
        };

        VkRenderingAttachmentInfo depthAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = depthView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .loadOp = (hdrTarget >= 0) ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue.depthStencil = { 1.0f, 0 }
        };

        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = { .offset = {0, 0}, .extent = vulkan.swapchainExtent },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = &depthAttachment,
            .pStencilAttachment = NULL
        };

        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        if (hdrTarget >= 0) {
            if (fxaa) { VANILLA_VK_recordFxaa(commandBuffer, (uint16_t)hdrTarget, true); }
            else      { VANILLA_VK_recordToneMap(commandBuffer, (uint16_t)hdrTarget, true); }
        }

        VANILLA_VK_bindSceneState(commandBuffer, &campush, &viewport, &scissor);
        VANILLA_VK_drawPipelines(commandBuffer, false);
        VANILLA_VK_drawPipelinesBlend(commandBuffer, false);

        vkCmdEndRendering(commandBuffer);
    }
    else {
        const VkClearColorValue sceneClear = {
            .float32 = {
                skyColor.x,
                skyColor.y,
                skyColor.z,
                1.0f
            }
        };

        const VkClearColorValue layerClear = {
            .float32 = {
                0.0f,
                0.0f,
                0.0f,
                0.0f
            }
        };

        for (uint16_t pipelineIndex = 0; pipelineIndex < vulkan.pipelineCount; pipelineIndex++) {
            VANILLA_VK_recordPipelinePass(
                commandBuffer,
                pipelineIndex,
                (pipelineIndex == 0) ? &sceneClear : &layerClear,
                &campush,
                &viewport,
                &scissor
            );
        }

        if (vulkan.postProcessingCount > 0 && vulkan.postProcessingPipelines != NULL) {
            for (uint16_t passIndex = 0; passIndex < vulkan.postProcessingCount; passIndex++) {
                const int isFirstPass = (passIndex == 0);
                const int isLastPass  = (passIndex == vulkan.postProcessingCount - 1);

                uint32_t inputIndex = 0;
                uint32_t outputIndex = 0;

                if (!isFirstPass) { inputIndex = (passIndex - 1) % 2; }
                if (!isLastPass) {
                    outputIndex = passIndex % 2;

                    VkImageMemoryBarrier2 imageBarrierToAttachment = {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask = isFirstPass ? VK_PIPELINE_STAGE_2_NONE : VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        .srcAccessMask = isFirstPass ? VK_ACCESS_2_NONE : VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        .oldLayout = isFirstPass ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = vulkan.postProcessingImages[outputIndex],
                        .subresourceRange = {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1
                        }
                    };

                    VkDependencyInfo dependencyToAttachment = {
                        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                        .imageMemoryBarrierCount = 1,
                        .pImageMemoryBarriers = &imageBarrierToAttachment
                    };

                    vkCmdPipelineBarrier2(
                        commandBuffer,
                        &dependencyToAttachment
                    );
                }

                VkRenderingAttachmentInfo colorAttachment = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = isLastPass ? vulkan.imageViews[imageIndex] : vulkan.postProcessingImageViews[outputIndex],
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .resolveMode = VK_RESOLVE_MODE_NONE,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue.color = {
                        .float32 = {
                            0.0f,
                            0.0f,
                            0.0f,
                            1.0f
                        }
                    }
                };

                VkRenderingInfo renderingInfo = {
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = {
                        .offset = {0, 0},
                        .extent = vulkan.swapchainExtent
                    },
                    .layerCount = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = &colorAttachment,
                    .pDepthAttachment = NULL,
                    .pStencilAttachment = NULL
                };

                vkCmdBeginRendering(
                    commandBuffer,
                    &renderingInfo
                );

                vkCmdSetViewport(
                    commandBuffer,
                    0,
                    1,
                    &postProcessViewport
                );

                vkCmdSetScissor(
                    commandBuffer,
                    0,
                    1,
                    &scissor
                );

                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    vulkan.postProcessingPipelineLayout,
                    0,
                    1,
                    &vulkan.postProcessingDescriptorSet,
                    0,
                    NULL
                );

                vkCmdPushConstants(
                    commandBuffer,
                    vulkan.postProcessingPipelineLayout,
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(uint32_t),
                    &inputIndex
                );

                vkCmdBindPipeline(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    vulkan.postProcessingPipelines[passIndex]
                );

                vkCmdDraw(commandBuffer, 3, 1, 0, 0);

                vkCmdEndRendering(commandBuffer);

                if (!isLastPass) {
                    VkImageMemoryBarrier2 imageBarrierToShader = {
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = vulkan.postProcessingImages[outputIndex],
                        .subresourceRange = {
                            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                            .baseMipLevel = 0,
                            .levelCount = 1,
                            .baseArrayLayer = 0,
                            .layerCount = 1
                        }
                    };

                    VkDependencyInfo dependencyToShader = {
                        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                        .imageMemoryBarrierCount = 1,
                        .pImageMemoryBarriers = &imageBarrierToShader
                    };

                    vkCmdPipelineBarrier2(
                        commandBuffer,
                        &dependencyToShader
                    );
                }
            }
        }
    }

    /* Swapchain image: color attachment -> present. */
    VANILLA_VK_swapchainBarrier(commandBuffer, vulkan.images[imageIndex], false);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) { return 1; }
    return 0;
}

void VANILLA_VK_destroyCommandBuffers(void) {
    if(vulkan.commandBuffers == NULL) { return; }

    vkFreeCommandBuffers(vulkan.device, vulkan.commandPool, vulkan.imageCount, vulkan.commandBuffers);
    VANILLA_munmap(vulkan.commandBuffers, sizeof(VkCommandBuffer) * vulkan.imageCount);
    vulkan.commandBuffers = NULL;
}

VkCommandBuffer VANILLA_VK_beginSingleTimeCommands(void) {
    VkCommandBufferAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = vulkan.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    if (vkAllocateCommandBuffers(
            vulkan.device,
            &allocateInfo,
            &commandBuffer) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        vkFreeCommandBuffers(
            vulkan.device,
            vulkan.commandPool,
            1,
            &commandBuffer);

        return VK_NULL_HANDLE;
    }

    return commandBuffer;
}

int VANILLA_VK_endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(
            vulkan.device,
            vulkan.commandPool,
            1,
            &commandBuffer);

        return 1;
    }

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer
    };

    if (vkQueueSubmit(
            vulkan.graphicsQueue,
            1,
            &submitInfo,
            VK_NULL_HANDLE) != VK_SUCCESS) {

        vkFreeCommandBuffers(
            vulkan.device,
            vulkan.commandPool,
            1,
            &commandBuffer);

        return 1;
    }

    if (vkQueueWaitIdle(vulkan.graphicsQueue) != VK_SUCCESS) {
        vkFreeCommandBuffers(
            vulkan.device,
            vulkan.commandPool,
            1,
            &commandBuffer);

        return 1;
    }

    vkFreeCommandBuffers(
        vulkan.device,
        vulkan.commandPool,
        1,
        &commandBuffer);

    return 0;
}