#pragma once
#include "VKFuncs.h"
#include "mathfu/glsl_mappings.h"

class DebugCoord
{
public:
    struct UniformBufferObject
    {
        mathfu::mat4 ProjView;
    };

    DebugCoord();
    ~DebugCoord();

    void executeCommandBuffer(VkCommandBuffer primaryCmdBuffer, uint32_t nextIndex);
    void update();

private:
    VkDescriptorPool mDescriptorPool;
    VkDescriptorSetLayout mDescriptorSetLayout;
    VkDescriptorSet mDescriptorSet;
    VkBuffer mUniformBuffer;
    VkBuffer mUniformStagingBuffer;
    VkDeviceMemory mUniformBufferMemory;
    VkDeviceMemory mUniformStagingBufferMemory;
    VkPipelineCache mPCache;
    VkPipeline mPipeline;
    VkPipelineLayout mPLayout;
    std::vector<VkCommandBuffer>    mCmdBuffer;
};