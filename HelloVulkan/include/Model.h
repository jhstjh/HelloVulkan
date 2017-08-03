#pragma once
#include <cstdint>
#include <vector>
#include "VKFuncs.h"
#include "ext/mathfu/glsl_mappings.h"

class Model
{
    struct Vertex
    {
        mathfu::vec3 pos;
        mathfu::vec3 color;
        mathfu::vec2 texCoord;
        mathfu::vec3 normal;

        static VkVertexInputBindingDescription getBindingDescription() {
            VkVertexInputBindingDescription bindingDescription = {};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(Vertex);
            bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            return bindingDescription;
        }

        static std::array<VkVertexInputAttributeDescription, 4> getAttributeDescriptions() {
            std::array<VkVertexInputAttributeDescription, 4> attributeDescriptions = {};
            attributeDescriptions[0].binding = 0;
            attributeDescriptions[0].location = 0;
            attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[0].offset = offsetof(Vertex, pos);

            attributeDescriptions[1].binding = 0;
            attributeDescriptions[1].location = 1;
            attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[1].offset = offsetof(Vertex, color);

            attributeDescriptions[2].binding = 0;
            attributeDescriptions[2].location = 2;
            attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
            attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

            attributeDescriptions[3].binding = 0;
            attributeDescriptions[3].location = 3;
            attributeDescriptions[3].format = VK_FORMAT_R32G32B32_SFLOAT;
            attributeDescriptions[3].offset = offsetof(Vertex, normal);

            return attributeDescriptions;
        }
    };

public:
    Model(std::string name, float offsetZ);
    VkCommandBuffer &getCommandBuffer(uint32_t nextIndex);
    VkCommandBuffer &getShadowCommandBuffer(uint32_t nextIndex);
    void update();

private:
    std::vector<VkCommandBuffer>    mCmdBuffer;
    std::vector<VkCommandBuffer>    mShadowCmdBuffer;
    VkBuffer            mVertexBuffer;
    VkDeviceMemory      mVertexBufferMemory;
    VkBuffer            mIndexBuffer;
    VkDeviceMemory      mIndexBufferMemory;
    VkBuffer            mUniformStagingBuffer;
    VkDeviceMemory      mUniformStagingBufferMemory;
    VkBuffer            mUniformBuffer;
    VkDeviceMemory      mUniformBufferMemory;
    VkBuffer            mShadowUniformStagingBuffer;
    VkDeviceMemory      mShadowUniformStagingBufferMemory;
    VkBuffer            mShadowUniformBuffer;
    VkDeviceMemory      mShadowUniformBufferMemory;
    VkDescriptorPool    mDescriptorPool;
    VkDescriptorSet     mDescriptorSet;
    VkDescriptorPool    mShadowDescriptorPool;
    VkDescriptorSet     mShadowDescriptorSet;
    VkImage             mStagingImage;
    VkDeviceMemory      mStagingImageMemory;
    VkImage             mTextureImage;
    VkDeviceMemory      mTextureImageMemory;
    VkImageView         mTextureImageView;
    VkSampler           mTextureSampler;
    uint32_t            mCmdBufferLen;
    VkDescriptorSetLayout mDescriptorSetLayout;
    VkDescriptorSetLayout mShadowDescriptorSetLayout;

    VkPipelineLayout    mPLayout;
    VkPipelineCache     mPCache;
    VkPipeline          mPipeline;

    VkPipelineLayout    mShadowPLayout;
    VkPipelineCache     mShadowPCache;
    VkPipeline          mShadowPipeline;

    float mOffsetZ{ 0.f };
    std::vector<Model::Vertex>  mVertices;
    std::vector<uint32_t> mIndices;
};