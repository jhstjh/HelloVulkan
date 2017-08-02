#pragma once
#include <cstdint>
#include <vector>
#include "VKFuncs.h"
#include "engine.h"
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
    Model(std::string name, struct engine* engine, float offsetZ);
    VkCommandBuffer &getCommandBuffer(uint32_t nextIndex);
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
    uint32_t            mCmdBufferLen;
    VkDescriptorSetLayout mDescriptorSetLayout;
    VkPipelineLayout    mPLayout;
    VkPipelineCache     mPCache;
    VkPipeline          mPipeline;

    float mOffsetZ{ 0.f };
    std::vector<Model::Vertex>  mVertices;
    std::vector<uint32_t> mIndices;
};