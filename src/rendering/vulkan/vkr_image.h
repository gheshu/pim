#pragma once

#include "rendering/vulkan/vkr.h"

PIM_C_BEGIN

// ----------------------------------------------------------------------------

bool vkrImage_New(
    vkrImage* image,
    const VkImageCreateInfo* info,
    vkrMemUsage memUsage);
void vkrImage_Del(vkrImage* image);
void vkrImage_Release(vkrImage* image);
void* vkrImage_Map(const vkrImage* image);
void vkrImage_Unmap(const vkrImage* image);
void vkrImage_Flush(const vkrImage* image);

void vkrImage_Barrier(
    vkrImage* image,
    VkCommandBuffer cmd,
    VkImageLayout newLayout,
    VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
    VkPipelineStageFlags srcStages, VkPipelineStageFlags dstStages);

void vkrImage_Transfer(
    vkrImage* image,
    vkrQueueId srcQueueId, vkrQueueId dstQueueId,
    VkCommandBuffer srcCmd, VkCommandBuffer dstCmd,
    VkImageLayout newLayout,
    VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
    VkPipelineStageFlags srcStages, VkPipelineStageFlags dstStages);

// ----------------------------------------------------------------------------

VkImageView vkrImageView_New(
    VkImage image,
    VkImageViewType type,
    VkFormat format,
    VkImageAspectFlags aspect,
    i32 baseMip, i32 mipCount,
    i32 baseLayer, i32 layerCount);
void vkrImageView_Del(VkImageView view);
void vkrImageView_Release(VkImageView view);

// ----------------------------------------------------------------------------

PIM_C_END
