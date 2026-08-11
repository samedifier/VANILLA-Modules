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

int VANILLA_VK_recordCommandBuffer(uint32_t imageIndex, uint32_t activeEntityCount) {
    VkCommandBuffer commandBuffer = vulkan.commandBuffers[imageIndex];

    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };

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

    if ((memoryMode == VANILLA_MEMORY_HYBRID) || (memoryMode == VANILLA_MEMORY_DEVICE_LOCAL)) {
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

    VkBufferMemoryBarrier2 barriers[] = {
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
            .buffer = vulkan.globalVertexBuffer,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
            .dstAccessMask = VK_ACCESS_2_INDEX_READ_BIT,
            .buffer = vulkan.globalIndexBuffer,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .buffer = vulkan.globalTransformBuffer,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .buffer = vulkan.modelLookupBuffer,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = vulkan.materialBuffer,
            .size = VK_WHOLE_SIZE
        },
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
            .srcAccessMask = VK_ACCESS_2_HOST_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
            .buffer = vulkan.materialSlotBuffer,
            .size = VK_WHOLE_SIZE
        }
    };

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        .bufferMemoryBarrierCount = sizeof(barriers) / sizeof(barriers[0]),
        .pBufferMemoryBarriers = barriers
    };

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

    vulkan.vertexBufferCopy.size = 0;
    vulkan.indexBufferCopy.size = 0;
    vulkan.modelLookupBufferCopy.size = 0;
    vulkan.transformBufferCopy.size = 0;
    vulkan.materialCopy.size = 0;

    vulkan.maxmcslot = 0;

    VkRenderingAttachmentInfo colorAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = vulkan.imageViews[imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.color = { .float32 = { 0.7f, 0.7f, 0.7f, 1.0f } }
    };

    VkRenderingAttachmentInfo depthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = vulkan.depthImageView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.depthStencil = { 1.0f, 0 }
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
        .pDepthAttachment = &depthAttachment,
        .pStencilAttachment = NULL
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        vulkan.pipeline
    );

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

        case VANILLA_PROJECTION_ORTHOGRAPHIC:                
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

    VANILLA_MATH_multiply(&viewProj, &proj, &view);

    VANILLA_CameraPush campush = { viewProj, aspect, 0.0f, 0.0f, 0.0f };

    vkCmdPushConstants(
        commandBuffer,
        vulkan.pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(VANILLA_CameraPush),
        &campush
    );

    VkViewport viewport = {
        .x = 0.0f,
        .y = (float)vulkan.swapchainExtent.height,
        .width = (float)vulkan.swapchainExtent.width,
        .height = -(float)vulkan.swapchainExtent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = vulkan.swapchainExtent
    };

    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    VkBuffer vertexBuffers[] = { vulkan.globalVertexBuffer };
    VkDeviceSize offsets[] = {0};

    vkCmdBindVertexBuffers(
        commandBuffer,
        0,
        1,
        vertexBuffers,
        offsets
    );

    vkCmdBindIndexBuffer(
        commandBuffer,
        vulkan.globalIndexBuffer,
        0,
        VK_INDEX_TYPE_UINT32
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        vulkan.pipelineLayout,
        0,
        1,
        &vulkan.descriptorSet,
        0,
        NULL
    );

    if (vulkan.vramActiveCount > 0) {
        vkCmdDrawIndexedIndirect(
            commandBuffer,
            vulkan.indirectCommandBuffer,
            0,
            vulkan.vramActiveCount,
            sizeof(VkDrawIndexedIndirectCommand)
        );
    }

    vkCmdEndRendering(commandBuffer);

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