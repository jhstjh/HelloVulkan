#pragma once
#include <cstdint>
#include <vector>
#include "VKFuncs.h"
#include "engine.h"

class Model
{
public:
    Model(struct engine* engine);
    void draw(VkQueue &queue, VkSemaphore* waitSemaphores, uint32_t waitSemaphoreCount, VkSemaphore* signalSemaphores, uint32_t signalSemaphoreCount, uint32_t nextIndex);
    void update();

private:
    std::vector<VkCommandBuffer>    mCmdBuffer;
    VkBuffer            mVertexBuffer;
    VkDeviceMemory      mVertexBufferMemory;
    VkBuffer            mIndexBuffer;
    VkDeviceMemory      mIndexBufferMemory;
    VkBuffer            mUniformStagingBuffer;
    VkDeviceMemory      mUniformStagingBufferMemory;
    VkBuffer            mUniformBuffer;
    VkDeviceMemory      mUniformBufferMemory;
    VkDescriptorPool    mDescriptorPool;
    VkDescriptorSet     mDescriptorSet;
    VkImage             mStagingImage;
    VkDeviceMemory      mStagingImageMemory;
    VkImage             mTextureImage;
    VkDeviceMemory      mTextureImageMemory;
    VkImageView         mTextureImageView;
    VkSampler           mTextureSampler;
    uint32_t            gCmdBufferLen;
    VkDescriptorSetLayout mDescriptorSetLayout;
    VkPipelineLayout    mPLayout;
    VkPipelineCache     mPCache;
    VkPipeline          mPipeline;
};