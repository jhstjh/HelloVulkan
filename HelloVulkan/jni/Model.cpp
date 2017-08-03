#include <array>
#include <chrono>
#include <string>
#include "Model.h"
#include "mathfu/glsl_mappings.h"
#include "ShadowMap.h"
#include "VKRenderer.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

using namespace mathfu;

template<typename CharT, typename TraitsT = std::char_traits<CharT> >
class vectorwrapbuf : public std::basic_streambuf<CharT, TraitsT> {
public:
    vectorwrapbuf(std::vector<CharT> &vec) {
        this->setg(vec.data(), vec.data(), vec.data() + vec.size());
    }
};

struct UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 shadowTransform;
};

Model::Model(std::string name, struct engine* engine, float offsetZ)
    : mOffsetZ(offsetZ)
{
    auto assetManager = engine->app->activity->assetManager;

    // create texture image
    {
        std::string texturePath = "textures/" + name + ".jpg";
        int32_t texWidth, texHeight, texChannels;
        AAsset* texFile = AAssetManager_open(assetManager, texturePath.c_str(), AASSET_MODE_UNKNOWN);
        assert(texFile);
        auto size = AAsset_getLength(texFile);
        std::vector<uint8_t> texData(size);
        AAsset_read(texFile, texData.data(), size);
        AAsset_close(texFile);

        auto pixels = stbi_load_from_memory(texData.data(), size, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        VkDeviceSize imageSize = texWidth * texHeight * 4;

        assert(pixels);

        VKRenderer::getInstance().createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mStagingImage, mStagingImageMemory);

        void* data;
        vkMapMemory(VKRenderer::getInstance().getDevice(), mStagingImageMemory, 0, imageSize, 0, &data);
        assert(data);

        VkImageSubresource subresource = {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.mipLevel = 0;
        subresource.arrayLayer = 0;

        VkSubresourceLayout stagingImageLayout;
        vkGetImageSubresourceLayout(VKRenderer::getInstance().getDevice(), mStagingImage, &subresource, &stagingImageLayout);

        if (stagingImageLayout.rowPitch = texWidth * 4)
        {
            memcpy(data, pixels, imageSize);
        }
        else
        {
            uint8_t* dataBytes = reinterpret_cast<uint8_t*>(data);

            for (auto y = 0; y < texHeight; y++)
            {
                memcpy(
                    &dataBytes[y * stagingImageLayout.rowPitch],
                    &pixels[y * texWidth * 4],
                    texWidth * 4
                );
            }
        }

        vkUnmapMemory(VKRenderer::getInstance().getDevice(), mStagingImageMemory);
        stbi_image_free(pixels);

        VKRenderer::getInstance().createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mTextureImage, mTextureImageMemory);

        VKRenderer::getInstance().transitionImageLayout(mStagingImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VKRenderer::getInstance().transitionImageLayout(mTextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VKRenderer::getInstance().copyImage(mStagingImage, mTextureImage, texWidth, texHeight);

        VKRenderer::getInstance().transitionImageLayout(mTextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // create texture image view
    {
        VKRenderer::getInstance().createImageView(mTextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, mTextureImageView);
    }

    // create texture sampler
    {
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.mipLodBias = 0.f;
        samplerInfo.minLod = 0.f;
        samplerInfo.maxLod = 0.f;

        auto result = vkCreateSampler(VKRenderer::getInstance().getDevice(), &samplerInfo, nullptr, &mTextureSampler);
        ASSERT_VK_SUCCESS(result);
    }

    // Load Model
    {
        tinyobj::attrib_t attrib;
        std::vector<tinyobj::shape_t> shapes;
        std::vector<tinyobj::material_t> materials;
        std::string err;

        std::string modelPath = "models/" + name + ".obj";
        AAsset* obj = AAssetManager_open(assetManager, modelPath.c_str(), AASSET_MODE_UNKNOWN);
        assert(obj);
        auto size = AAsset_getLength(obj);
        std::vector<char> objData(size);
        AAsset_read(obj, objData.data(), size);
        AAsset_close(obj);

        vectorwrapbuf<char> databuf(objData);
        std::istream is(&databuf);

        if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &err, &is))
        {
            assert(false);
        }

        for (const auto& shape : shapes)
        {
            for (const auto& index : shape.mesh.indices)
            {
                Vertex vertex = {};

                vertex.pos = {
                    attrib.vertices[3 * index.vertex_index + 0],
                    attrib.vertices[3 * index.vertex_index + 1],
                    attrib.vertices[3 * index.vertex_index + 2]
                };

                vertex.texCoord = {
                    attrib.texcoords[2 * index.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * index.texcoord_index + 1]
                };

                vertex.color = { 1.0f, 1.0f, 1.0f };

                vertex.normal = {
                    attrib.normals[3 * index.normal_index + 0],
                    attrib.normals[3 * index.normal_index + 1],
                    attrib.normals[3 * index.normal_index + 2]
                };

                mVertices.push_back(vertex);
                mIndices.push_back(mIndices.size());
            }
        }
    }


    // create vertex buffer
    {
        VkDeviceSize bufferSize = sizeof(mVertices[0]) * mVertices.size();

        // staging buffer
        VkBuffer            stagingBuffer;
        VkDeviceMemory      stagingBufferMemory;

        VKRenderer::getInstance().createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(VKRenderer::getInstance().getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
        assert(data);
        memcpy(data, mVertices.data(), (size_t)bufferSize);
        vkUnmapMemory(VKRenderer::getInstance().getDevice(), stagingBufferMemory);

        // device local buffer
        VKRenderer::getInstance().createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mVertexBuffer, mVertexBufferMemory);

        // copy vertex buffer from host visible to device local
        VKRenderer::getInstance().copyBuffer(stagingBuffer, mVertexBuffer, bufferSize);
    }

    {
        // create index buffer
        VkDeviceSize bufferSize = sizeof(mIndices[0]) * mIndices.size();

        // staging buffer
        VkBuffer            stagingBuffer;
        VkDeviceMemory      stagingBufferMemory;

        VKRenderer::getInstance().createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(VKRenderer::getInstance().getDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
        assert(data);
        memcpy(data, mIndices.data(), (size_t)bufferSize);
        vkUnmapMemory(VKRenderer::getInstance().getDevice(), stagingBufferMemory);

        // device local buffer
        VKRenderer::getInstance().createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mIndexBuffer, mIndexBufferMemory);

        // copy index buffer from host visible to device local
        VKRenderer::getInstance().copyBuffer(stagingBuffer, mIndexBuffer, bufferSize);
    }

    // create uniform buffer
    {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        VKRenderer::getInstance().createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mUniformStagingBuffer, mUniformStagingBufferMemory);
        VKRenderer::getInstance().createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mUniformBuffer, mUniformBufferMemory);
    }

    // create shadow uniform buffer
    {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        VKRenderer::getInstance().createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mShadowUniformStagingBuffer, mShadowUniformStagingBufferMemory);
        VKRenderer::getInstance().createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mShadowUniformBuffer, mShadowUniformBufferMemory);
    }

    // create descriptor set layout
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

        VkDescriptorSetLayoutBinding samplerLayoutBinding = {};
        samplerLayoutBinding.binding = 1;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.pImmutableSamplers = nullptr;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutBinding shadowSamplerLayoutBinding = {};
        shadowSamplerLayoutBinding.binding = 2;
        shadowSamplerLayoutBinding.descriptorCount = 1;
        shadowSamplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowSamplerLayoutBinding.pImmutableSamplers = nullptr;
        shadowSamplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        std::array<VkDescriptorSetLayoutBinding, 3> bindings = { uboLayoutBinding, samplerLayoutBinding, shadowSamplerLayoutBinding };

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = bindings.size();
        layoutInfo.pBindings = bindings.data();

        auto result = vkCreateDescriptorSetLayout(VKRenderer::getInstance().getDevice(), &layoutInfo, nullptr, &mDescriptorSetLayout);
        assert(result == VK_SUCCESS);
    }

    // create descriptor pool
    {
        std::array<VkDescriptorPoolSize, 3> poolSizes = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 1;
        poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[2].descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        auto result = vkCreateDescriptorPool(VKRenderer::getInstance().getDevice(), &poolInfo, nullptr, &mDescriptorPool);
        assert(result == VK_SUCCESS);

        // create descriptor set
        VkDescriptorSetLayout uboLayouts[] = { mDescriptorSetLayout };
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = mDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = uboLayouts;

        result = vkAllocateDescriptorSets(VKRenderer::getInstance().getDevice(), &allocInfo, &mDescriptorSet);
        assert(result == VK_SUCCESS);

        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = mUniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = mTextureImageView;
        imageInfo.sampler = mTextureSampler;

        VkDescriptorImageInfo shadowImageInfo = {};
        shadowImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shadowImageInfo.imageView = VKRenderer::getInstance().getShadowMap()->getShadowMapView();
        shadowImageInfo.sampler = VKRenderer::getInstance().getShadowMap()->getShadowMapSampler();

        std::array<VkWriteDescriptorSet, 3> descriptorWrites = {};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = mDescriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = mDescriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[2].dstSet = mDescriptorSet;
        descriptorWrites[2].dstBinding = 2;
        descriptorWrites[2].dstArrayElement = 0;
        descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[2].descriptorCount = 1;
        descriptorWrites[2].pImageInfo = &shadowImageInfo;

        vkUpdateDescriptorSets(VKRenderer::getInstance().getDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }

    // create shadow descriptor set layout
    {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

        std::array<VkDescriptorSetLayoutBinding, 1> bindings = { uboLayoutBinding };

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = bindings.size();
        layoutInfo.pBindings = bindings.data();

        auto result = vkCreateDescriptorSetLayout(VKRenderer::getInstance().getDevice(), &layoutInfo, nullptr, &mShadowDescriptorSetLayout);
        assert(result == VK_SUCCESS);
    }

    // create shadow descriptor pool
    {
        std::array<VkDescriptorPoolSize, 1> poolSizes = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;

        auto result = vkCreateDescriptorPool(VKRenderer::getInstance().getDevice(), &poolInfo, nullptr, &mShadowDescriptorPool);
        assert(result == VK_SUCCESS);

        // create descriptor set
        VkDescriptorSetLayout uboLayouts[] = { mShadowDescriptorSetLayout };
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = mShadowDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = uboLayouts;

        result = vkAllocateDescriptorSets(VKRenderer::getInstance().getDevice(), &allocInfo, &mShadowDescriptorSet);
        assert(result == VK_SUCCESS);

        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = mShadowUniformBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        std::array<VkWriteDescriptorSet, 1> descriptorWrites = {};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = mShadowDescriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(VKRenderer::getInstance().getDevice(), descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }

    // create graphics pipeline
    {
        VkDescriptorSetLayout setLayouts[] = { mDescriptorSetLayout };
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.pNext = nullptr;
        pipelineLayoutCreateInfo.setLayoutCount = 1;
        pipelineLayoutCreateInfo.pSetLayouts = setLayouts;
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

        auto result = vkCreatePipelineLayout(VKRenderer::getInstance().getDevice(), &pipelineLayoutCreateInfo, nullptr, &mPLayout);
        assert(result == VK_SUCCESS);

        VkPipelineDynamicStateCreateInfo dynamicStateInfo;
        dynamicStateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dynamicStateInfo.pNext = nullptr;
        dynamicStateInfo.dynamicStateCount = 0;
        dynamicStateInfo.pDynamicStates = nullptr;

        AAsset* vs = AAssetManager_open(assetManager, "shader.vert.spv", AASSET_MODE_UNKNOWN);
        assert(vs);
        auto size = AAsset_getLength(vs);
        std::vector<uint8_t> vsData(size);
        AAsset_read(vs, vsData.data(), size);
        AAsset_close(vs);

        AAsset* fs = AAssetManager_open(assetManager, "shader.frag.spv", AASSET_MODE_UNKNOWN);
        assert(fs);
        size = AAsset_getLength(fs);
        std::vector<uint8_t> fsData(size);
        AAsset_read(fs, fsData.data(), size);
        AAsset_close(fs);

        VkShaderModule vertexShader, fragmentShader;

        VkShaderModuleCreateInfo shaderModuleCreateInfo;
        shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderModuleCreateInfo.pNext = nullptr;
        shaderModuleCreateInfo.codeSize = vsData.size();
        shaderModuleCreateInfo.pCode = (const uint32_t*)(vsData.data());
        shaderModuleCreateInfo.flags = 0;

        result = vkCreateShaderModule(
            VKRenderer::getInstance().getDevice(), &shaderModuleCreateInfo, nullptr, &vertexShader);
        assert(result == VK_SUCCESS);

        shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderModuleCreateInfo.pNext = nullptr;
        shaderModuleCreateInfo.codeSize = fsData.size();
        shaderModuleCreateInfo.pCode = (const uint32_t*)(fsData.data());
        shaderModuleCreateInfo.flags = 0;

        result = vkCreateShaderModule(
            VKRenderer::getInstance().getDevice(), &shaderModuleCreateInfo, nullptr, &fragmentShader);
        assert(result == VK_SUCCESS);

        VkPipelineShaderStageCreateInfo shaderStages[2];

        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].pNext = nullptr;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertexShader;
        shaderStages[0].pSpecializationInfo = nullptr;
        shaderStages[0].flags = 0;
        shaderStages[0].pName = "main";

        shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[1].pNext = nullptr;
        shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        shaderStages[1].module = fragmentShader;
        shaderStages[1].pSpecializationInfo = nullptr;
        shaderStages[1].flags = 0;
        shaderStages[1].pName = "main";

        auto displaySize = VKRenderer::getInstance().getDisplaySize();

        VkViewport viewports;
        viewports.minDepth = 0.0f;
        viewports.maxDepth = 1.0f;
        viewports.x = 0;
        viewports.y = 0;
        viewports.width = (float)displaySize.width;
        viewports.height = (float)displaySize.height;

        VkRect2D scissor;
        scissor.extent = displaySize;
        scissor.offset.x = 0;
        scissor.offset.y = 0;

        VkPipelineViewportStateCreateInfo viewportInfo;
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.pNext = nullptr;
        viewportInfo.viewportCount = 1;
        viewportInfo.pViewports = &viewports;
        viewportInfo.scissorCount = 1;
        viewportInfo.pScissors = &scissor;

        VkSampleMask sampleMask = ~0u;
        VkPipelineMultisampleStateCreateInfo multisampleInfo;
        multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleInfo.pNext = nullptr;
        multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampleInfo.sampleShadingEnable = VK_FALSE;
        multisampleInfo.minSampleShading = 0;
        multisampleInfo.pSampleMask = &sampleMask;
        multisampleInfo.alphaToCoverageEnable = VK_FALSE;
        multisampleInfo.alphaToOneEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState attachmentStates;
        attachmentStates.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachmentStates.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlendInfo;
        colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendInfo.pNext = nullptr;
        colorBlendInfo.logicOpEnable = VK_FALSE;
        colorBlendInfo.logicOp = VK_LOGIC_OP_COPY;
        colorBlendInfo.attachmentCount = 1;
        colorBlendInfo.pAttachments = &attachmentStates;

        VkPipelineRasterizationStateCreateInfo rasterInfo;
        rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterInfo.pNext = nullptr;
        rasterInfo.depthClampEnable = VK_FALSE;
        rasterInfo.rasterizerDiscardEnable = VK_FALSE;
        rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterInfo.cullMode = VK_CULL_MODE_NONE;
        rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterInfo.depthBiasEnable = VK_FALSE;
        rasterInfo.lineWidth = 1;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.pNext = nullptr;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo;
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.pNext = nullptr;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size();
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineCacheCreateInfo pipelineCacheInfo;
        pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        pipelineCacheInfo.pNext = nullptr;
        pipelineCacheInfo.initialDataSize = 0;
        pipelineCacheInfo.pInitialData = nullptr;
        pipelineCacheInfo.flags = 0;

        result = vkCreatePipelineCache(VKRenderer::getInstance().getDevice(), &pipelineCacheInfo, nullptr, &mPCache);
        assert(result == VK_SUCCESS);

        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.f;
        depthStencil.maxDepthBounds = 0.f;
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {};
        depthStencil.back = {};

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.pNext = nullptr;
        pipelineCreateInfo.flags = 0;
        pipelineCreateInfo.stageCount = 2;
        pipelineCreateInfo.pStages = shaderStages;
        pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineCreateInfo.pTessellationState = nullptr;
        pipelineCreateInfo.pViewportState = &viewportInfo;
        pipelineCreateInfo.pRasterizationState = &rasterInfo;
        pipelineCreateInfo.pMultisampleState = &multisampleInfo;
        pipelineCreateInfo.pDepthStencilState = &depthStencil;
        pipelineCreateInfo.pColorBlendState = &colorBlendInfo;
        pipelineCreateInfo.pDynamicState = &dynamicStateInfo;
        pipelineCreateInfo.layout = mPLayout;
        pipelineCreateInfo.renderPass = VKRenderer::getInstance().getRenderPass();
        pipelineCreateInfo.subpass = 0;
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex = 0;

        result = vkCreateGraphicsPipelines(VKRenderer::getInstance().getDevice(), mPCache, 1, &pipelineCreateInfo, nullptr, &mPipeline);
        assert(result == VK_SUCCESS);

        // create command buffer
        mCmdBufferLen = VKRenderer::getInstance().getSwapChainLength();
        mCmdBuffer.resize(mCmdBufferLen);

        for (uint32_t bufferIndex = 0; bufferIndex < mCmdBufferLen; bufferIndex++)
        {
            VkCommandBufferAllocateInfo cmdBufferAllocationInfo{};
            cmdBufferAllocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdBufferAllocationInfo.pNext = nullptr;
            cmdBufferAllocationInfo.commandPool = VKRenderer::getInstance().getCommandPool();
            cmdBufferAllocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdBufferAllocationInfo.commandBufferCount = 1;

            result = vkAllocateCommandBuffers(VKRenderer::getInstance().getDevice(), &cmdBufferAllocationInfo, &mCmdBuffer[bufferIndex]);
            assert(result == VK_SUCCESS);


            VkCommandBufferBeginInfo cmdBufferBeginInfo{};
            cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            cmdBufferBeginInfo.pNext = nullptr;
            cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            cmdBufferBeginInfo.pInheritanceInfo = nullptr;

            result = vkBeginCommandBuffer(mCmdBuffer[bufferIndex], &cmdBufferBeginInfo);
            assert(result == VK_SUCCESS);

            std::array<VkClearValue, 2> clearValues = {};
            clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
            clearValues[1].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo renderPassBeginInfo{};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.pNext = nullptr;
            renderPassBeginInfo.renderPass = VKRenderer::getInstance().getRenderPass();
            renderPassBeginInfo.framebuffer = VKRenderer::getInstance().getFramebuffer(bufferIndex);
            renderPassBeginInfo.renderArea.offset.x = 0;
            renderPassBeginInfo.renderArea.offset.y = 0;
            renderPassBeginInfo.renderArea.extent = displaySize;
            renderPassBeginInfo.clearValueCount = clearValues.size();
            renderPassBeginInfo.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(mCmdBuffer[bufferIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(mCmdBuffer[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                mPipeline);

            VkBuffer vertexBuffers[] = { mVertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(mCmdBuffer[bufferIndex], 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(mCmdBuffer[bufferIndex], mIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(mCmdBuffer[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mPLayout, 0, 1, &mDescriptorSet, 0, nullptr);

            vkCmdDrawIndexed(mCmdBuffer[bufferIndex], mIndices.size(), 1, 0, 0, 0);

            vkCmdEndRenderPass(mCmdBuffer[bufferIndex]);
            result = vkEndCommandBuffer(mCmdBuffer[bufferIndex]);
            assert(result == VK_SUCCESS);
        }
    }

    // create shadow graphics pipeline
    {
        VkDescriptorSetLayout setLayouts[] = { mShadowDescriptorSetLayout };
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.pNext = nullptr;
        pipelineLayoutCreateInfo.setLayoutCount = 1;
        pipelineLayoutCreateInfo.pSetLayouts = setLayouts;
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

        auto result = vkCreatePipelineLayout(VKRenderer::getInstance().getDevice(), &pipelineLayoutCreateInfo, nullptr, &mShadowPLayout);
        assert(result == VK_SUCCESS);

        VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
        dynamicStateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dynamicStateInfo.pNext = nullptr;
        dynamicStateInfo.dynamicStateCount = 0;
        dynamicStateInfo.pDynamicStates = nullptr;

        AAsset* vs = AAssetManager_open(assetManager, "shader.vert.spv", AASSET_MODE_UNKNOWN);
        assert(vs);
        auto size = AAsset_getLength(vs);
        std::vector<uint8_t> vsData(size);
        AAsset_read(vs, vsData.data(), size);
        AAsset_close(vs);

        VkShaderModule vertexShader;

        VkShaderModuleCreateInfo shaderModuleCreateInfo{};
        shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderModuleCreateInfo.pNext = nullptr;
        shaderModuleCreateInfo.codeSize = vsData.size();
        shaderModuleCreateInfo.pCode = (const uint32_t*)(vsData.data());
        shaderModuleCreateInfo.flags = 0;

        result = vkCreateShaderModule(
            VKRenderer::getInstance().getDevice(), &shaderModuleCreateInfo, nullptr, &vertexShader);
        assert(result == VK_SUCCESS);


        VkPipelineShaderStageCreateInfo shaderStages[1];

        shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStages[0].pNext = nullptr;
        shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        shaderStages[0].module = vertexShader;
        shaderStages[0].pSpecializationInfo = nullptr;
        shaderStages[0].flags = 0;
        shaderStages[0].pName = "main";

        auto displaySize = VKRenderer::getInstance().getDisplaySize();

        VkViewport viewports;
        viewports.minDepth = 0.0f;
        viewports.maxDepth = 1.0f;
        viewports.x = 0;
        viewports.y = 0;
        viewports.width = 2048; // TODO
        viewports.height = 2048;

        VkRect2D scissor;
        scissor.extent = displaySize;
        scissor.extent.width = 2048;
        scissor.extent.height = 2048;
        scissor.offset.x = 0;
        scissor.offset.y = 0;

        VkPipelineViewportStateCreateInfo viewportInfo{};
        viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportInfo.pNext = nullptr;
        viewportInfo.viewportCount = 1;
        viewportInfo.pViewports = &viewports;
        viewportInfo.scissorCount = 1;
        viewportInfo.pScissors = &scissor;

        VkSampleMask sampleMask = ~0u;
        VkPipelineMultisampleStateCreateInfo multisampleInfo{};
        multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampleInfo.pNext = nullptr;
        multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampleInfo.sampleShadingEnable = VK_FALSE;
        multisampleInfo.minSampleShading = 0;
        multisampleInfo.pSampleMask = &sampleMask;
        multisampleInfo.alphaToCoverageEnable = VK_FALSE;
        multisampleInfo.alphaToOneEnable = VK_FALSE;

        VkPipelineRasterizationStateCreateInfo rasterInfo{};
        rasterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterInfo.pNext = nullptr;
        rasterInfo.depthClampEnable = VK_FALSE;
        rasterInfo.rasterizerDiscardEnable = VK_FALSE;
        rasterInfo.polygonMode = VK_POLYGON_MODE_FILL;
        rasterInfo.cullMode = VK_CULL_MODE_NONE;
        rasterInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterInfo.depthBiasEnable = VK_FALSE;
        rasterInfo.lineWidth = 1;

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.pNext = nullptr;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

        auto bindingDescription = Vertex::getBindingDescription();
        auto attributeDescriptions = Vertex::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.pNext = nullptr;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
        vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size();
        vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

        VkPipelineCacheCreateInfo pipelineCacheInfo{};
        pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        pipelineCacheInfo.pNext = nullptr;
        pipelineCacheInfo.initialDataSize = 0;
        pipelineCacheInfo.pInitialData = nullptr;
        pipelineCacheInfo.flags = 0;

        result = vkCreatePipelineCache(VKRenderer::getInstance().getDevice(), &pipelineCacheInfo, nullptr, &mShadowPCache);
        assert(result == VK_SUCCESS);

        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.f;
        depthStencil.maxDepthBounds = 0.f;
        depthStencil.stencilTestEnable = VK_FALSE;
        depthStencil.front = {};
        depthStencil.back = {};

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.pNext = nullptr;
        pipelineCreateInfo.flags = 0;
        pipelineCreateInfo.stageCount = 1;
        pipelineCreateInfo.pStages = shaderStages;
        pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineCreateInfo.pTessellationState = nullptr;
        pipelineCreateInfo.pViewportState = &viewportInfo;
        pipelineCreateInfo.pRasterizationState = &rasterInfo;
        pipelineCreateInfo.pMultisampleState = &multisampleInfo;
        pipelineCreateInfo.pDepthStencilState = &depthStencil;
        pipelineCreateInfo.pColorBlendState = nullptr;
        pipelineCreateInfo.pDynamicState = &dynamicStateInfo;
        pipelineCreateInfo.layout = mShadowPLayout;
        pipelineCreateInfo.renderPass = VKRenderer::getInstance().getShadowMap()->getRenderPass();
        pipelineCreateInfo.subpass = 0;
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex = 0;

        result = vkCreateGraphicsPipelines(VKRenderer::getInstance().getDevice(), mShadowPCache, 1, &pipelineCreateInfo, nullptr, &mShadowPipeline);
        assert(result == VK_SUCCESS);

        // create command buffer
        mCmdBufferLen = VKRenderer::getInstance().getSwapChainLength();
        mShadowCmdBuffer.resize(mCmdBufferLen);

        for (uint32_t bufferIndex = 0; bufferIndex < mCmdBufferLen; bufferIndex++)
        {
            VkCommandBufferAllocateInfo cmdBufferAllocationInfo{};
            cmdBufferAllocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdBufferAllocationInfo.pNext = nullptr;
            cmdBufferAllocationInfo.commandPool = VKRenderer::getInstance().getCommandPool();
            cmdBufferAllocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdBufferAllocationInfo.commandBufferCount = 1;

            result = vkAllocateCommandBuffers(VKRenderer::getInstance().getDevice(), &cmdBufferAllocationInfo, &mShadowCmdBuffer[bufferIndex]);
            assert(result == VK_SUCCESS);


            VkCommandBufferBeginInfo cmdBufferBeginInfo{};
            cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            cmdBufferBeginInfo.pNext = nullptr;
            cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            cmdBufferBeginInfo.pInheritanceInfo = nullptr;

            result = vkBeginCommandBuffer(mShadowCmdBuffer[bufferIndex], &cmdBufferBeginInfo);
            assert(result == VK_SUCCESS);

            std::array<VkClearValue, 2> clearValues = {};
            clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
            clearValues[1].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo renderPassBeginInfo{};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.pNext = nullptr;
            renderPassBeginInfo.renderPass = VKRenderer::getInstance().getShadowMap()->getRenderPass();
            renderPassBeginInfo.framebuffer = VKRenderer::getInstance().getShadowMap()->getFramebuffer();
            renderPassBeginInfo.renderArea.offset.x = 0;
            renderPassBeginInfo.renderArea.offset.y = 0;
            renderPassBeginInfo.renderArea.extent.width = 2048;
            renderPassBeginInfo.renderArea.extent.height = 2048;
            renderPassBeginInfo.clearValueCount = clearValues.size();
            renderPassBeginInfo.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(mShadowCmdBuffer[bufferIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdBindPipeline(mShadowCmdBuffer[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                mShadowPipeline);

            VkBuffer vertexBuffers[] = { mVertexBuffer };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(mShadowCmdBuffer[bufferIndex], 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(mShadowCmdBuffer[bufferIndex], mIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdBindDescriptorSets(mShadowCmdBuffer[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mShadowPLayout, 0, 1, &mShadowDescriptorSet, 0, nullptr);

            vkCmdDrawIndexed(mShadowCmdBuffer[bufferIndex], mIndices.size(), 1, 0, 0, 0);

            vkCmdEndRenderPass(mShadowCmdBuffer[bufferIndex]);
            result = vkEndCommandBuffer(mShadowCmdBuffer[bufferIndex]);
            assert(result == VK_SUCCESS);
        }
    }
}

VkCommandBuffer &Model::getCommandBuffer(uint32_t nextIndex)
{
    return mCmdBuffer[nextIndex];
}

VkCommandBuffer &Model::getShadowCommandBuffer(uint32_t nextIndex)
{
    return mShadowCmdBuffer[nextIndex];
}

void Model::update()
{
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count() / 1000.0f;
    auto displaySize = VKRenderer::getInstance().getDisplaySize();

    UniformBufferObject ubo = {};
    mat4 transMat = Matrix<float, 4>::FromTranslationVector(Vector<float, 3>{ 0, 0, mOffsetZ });
    auto rotMat = Matrix<float, 3>::RotationY(time * 90.f / 180.f * 3.1415926);
    ubo.model = transMat * mat4::FromRotationMatrix(rotMat);

    // offscreen shadow ubo
    ubo.view = mat4::LookAt(vec3(0.0f, 0.0f, 0.0f), vec3(2.f, 2.f, -2.f), vec3(0.0f, 1.0f, 0.0f), 1.0f);
    vec3 sphereCenterRS;
    sphereCenterRS = ubo.view * vec3(0, 0, 0);
    float l = sphereCenterRS.x() - 10;
    float b = sphereCenterRS.y() - 10;
    float n = sphereCenterRS.z() - 10;
    float r = sphereCenterRS.x() + 10;
    float t = sphereCenterRS.y() + 10;
    float f = sphereCenterRS.z() + 10;
    ubo.proj = mat4::Ortho(l, r, b, t, n, f, 1.f);
    ubo.proj(1, 1) *= -1;

    void* data;
    vkMapMemory(VKRenderer::getInstance().getDevice(), mShadowUniformStagingBufferMemory, 0, sizeof(ubo), 0, &data);
    assert(data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(VKRenderer::getInstance().getDevice(), mShadowUniformStagingBufferMemory);

    VKRenderer::getInstance().copyBuffer(mShadowUniformStagingBuffer, mShadowUniformBuffer, sizeof(ubo));

    mat4 T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);

    // on screen ubo
    ubo.shadowTransform = T * ubo.proj * ubo.view * ubo.model;
    ubo.view = mat4::LookAt(vec3(0.0f, 0.0f, 0.0f), vec3(4.0f, 4.0f, 4.0f), vec3(0.0f, 1.0f, 0.0f), 1.0f);
    ubo.proj = mat4::Perspective((45.0f) / 180.f * 3.1415926, (float)displaySize.width / (float)displaySize.height, 0.1f, 10.0f);
    ubo.proj(1, 1) *= -1; 

    vkMapMemory(VKRenderer::getInstance().getDevice(), mUniformStagingBufferMemory, 0, sizeof(ubo), 0, &data);
    assert(data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(VKRenderer::getInstance().getDevice(), mUniformStagingBufferMemory);

    VKRenderer::getInstance().copyBuffer(mUniformStagingBuffer, mUniformBuffer, sizeof(ubo));
}