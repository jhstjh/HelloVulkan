#include <array>
#include <cassert>
#include <vector>

#include "Asset.h"
#include "DebugCoord.h"
#include "VKRenderer.h"

using namespace mathfu;

DebugCoord::DebugCoord()
{
    // create uniform buffer
    {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        VKRenderer::getInstance().createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, mUniformStagingBuffer, mUniformStagingBufferMemory);
        VKRenderer::getInstance().createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mUniformBuffer, mUniformBufferMemory);
    }

    // create descriptor set layout
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
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        auto result = vkCreateDescriptorSetLayout(VKRenderer::getInstance().getDevice(), &layoutInfo, nullptr, &mDescriptorSetLayout);
        assert(result == VK_SUCCESS);
    }

    // create descriptor pool
    {
        std::array<VkDescriptorPoolSize, 1> poolSizes = {};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[0].descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

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

        std::array<VkWriteDescriptorSet, 1> descriptorWrites = {};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = mDescriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(VKRenderer::getInstance().getDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
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

        Asset vs("debug.vert.spv", 0);
        auto size = vs.getLength();
        std::vector<uint8_t> vsData(size);
        vs.read(vsData.data(), size);
        vs.close();

        Asset fs("debug.frag.spv", 0);
        size = fs.getLength();
        std::vector<uint8_t> fsData(size);
        fs.read(fsData.data(), size);
        fs.close();

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

        VkPipelineColorBlendAttachmentState attachmentStates{};
        attachmentStates.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachmentStates.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
        colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendInfo.pNext = nullptr;
        colorBlendInfo.logicOpEnable = VK_FALSE;
        colorBlendInfo.logicOp = VK_LOGIC_OP_COPY;
        colorBlendInfo.attachmentCount = 1;
        colorBlendInfo.pAttachments = &attachmentStates;

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
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.pNext = nullptr;
        vertexInputInfo.vertexBindingDescriptionCount = 0;
        vertexInputInfo.pVertexBindingDescriptions = nullptr;
        vertexInputInfo.vertexAttributeDescriptionCount = 0;
        vertexInputInfo.pVertexAttributeDescriptions = nullptr;

        VkPipelineCacheCreateInfo pipelineCacheInfo{};
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
        depthStencil.depthWriteEnable = VK_FALSE; // don't write debug coord depth
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
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
        pipelineCreateInfo.pDynamicState = nullptr;
        pipelineCreateInfo.layout = mPLayout;
        pipelineCreateInfo.renderPass = VKRenderer::getInstance().getRenderPass();
        pipelineCreateInfo.subpass = 0;
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineCreateInfo.basePipelineIndex = 0;

        result = vkCreateGraphicsPipelines(VKRenderer::getInstance().getDevice(), mPCache, 1, &pipelineCreateInfo, nullptr, &mPipeline);
        assert(result == VK_SUCCESS);

        vkDestroyShaderModule(VKRenderer::getInstance().getDevice(), vertexShader, nullptr);
        vkDestroyShaderModule(VKRenderer::getInstance().getDevice(), fragmentShader, nullptr);
    }

    // create command buffer
    {
        auto len = VKRenderer::getInstance().getSwapChainLength();
        mCmdBuffer.resize(len);

        for (uint32_t bufferIndex = 0; bufferIndex < len; bufferIndex++)
        {
            VkCommandBufferAllocateInfo cmdBufferAllocationInfo{};
            cmdBufferAllocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdBufferAllocationInfo.pNext = nullptr;
            cmdBufferAllocationInfo.commandPool = VKRenderer::getInstance().getCommandPool();
            cmdBufferAllocationInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
            cmdBufferAllocationInfo.commandBufferCount = 1;

            auto result = vkAllocateCommandBuffers(VKRenderer::getInstance().getDevice(), &cmdBufferAllocationInfo, &mCmdBuffer[bufferIndex]);
            assert(result == VK_SUCCESS);

            VkCommandBufferInheritanceInfo cmdBufferInheritanceInfo = {};
            cmdBufferInheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
            cmdBufferInheritanceInfo.renderPass = VKRenderer::getInstance().getRenderPass();
            cmdBufferInheritanceInfo.subpass = 0;
            cmdBufferInheritanceInfo.framebuffer = VKRenderer::getInstance().getFramebuffer(bufferIndex);

            VkCommandBufferBeginInfo cmdBufferBeginInfo{};
            cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            cmdBufferBeginInfo.pNext = nullptr;
            cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
            cmdBufferBeginInfo.pInheritanceInfo = &cmdBufferInheritanceInfo;

            result = vkBeginCommandBuffer(mCmdBuffer[bufferIndex], &cmdBufferBeginInfo);
            assert(result == VK_SUCCESS);

            vkCmdBindPipeline(mCmdBuffer[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                mPipeline);

            vkCmdBindDescriptorSets(mCmdBuffer[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, mPLayout, 0, 1, &mDescriptorSet, 0, nullptr);
            vkCmdDraw(mCmdBuffer[bufferIndex], 50, 1, 0, 0);

            result = vkEndCommandBuffer(mCmdBuffer[bufferIndex]);
            assert(result == VK_SUCCESS);
        }
    }
}

DebugCoord::~DebugCoord()
{
    vkFreeCommandBuffers(VKRenderer::getInstance().getDevice(), VKRenderer::getInstance().getCommandPool(), static_cast<uint32_t>(mCmdBuffer.size()), mCmdBuffer.data());
    mCmdBuffer.clear();

    vkDestroyPipelineLayout(VKRenderer::getInstance().getDevice(), mPLayout, nullptr);
    vkDestroyPipeline(VKRenderer::getInstance().getDevice(), mPipeline, nullptr);
    vkDestroyPipelineCache(VKRenderer::getInstance().getDevice(), mPCache, nullptr);
    vkDestroyBuffer(VKRenderer::getInstance().getDevice(), mUniformBuffer, nullptr);
    vkDestroyBuffer(VKRenderer::getInstance().getDevice(), mUniformStagingBuffer, nullptr);
    vkFreeMemory(VKRenderer::getInstance().getDevice(), mUniformBufferMemory, nullptr);
    vkFreeMemory(VKRenderer::getInstance().getDevice(), mUniformStagingBufferMemory, nullptr);
    vkDestroyDescriptorSetLayout(VKRenderer::getInstance().getDevice(), mDescriptorSetLayout, nullptr);
    vkFreeDescriptorSets(VKRenderer::getInstance().getDevice(), mDescriptorPool, 1, &mDescriptorSet);
    vkDestroyDescriptorPool(VKRenderer::getInstance().getDevice(), mDescriptorPool, nullptr);
}

void DebugCoord::executeCommandBuffer(VkCommandBuffer primaryCmdBuffer, uint32_t nextIndex)
{
    vkCmdExecuteCommands(primaryCmdBuffer, 1, &mCmdBuffer[nextIndex]);
}

void DebugCoord::update()
{
    auto displaySize = VKRenderer::getInstance().getDisplaySize();
    UniformBufferObject ubo = {};
    
    mat4 view = mat4::LookAt(vec3(0.0f, 0.0f, 0.0f), vec3(4.0f, 4.0f, 4.0f), vec3(0.0f, 1.0f, 0.0f), 1.0f);
    mat4 proj = mat4::Perspective((45.0f) / 180.f * 3.1415926f, (float)displaySize.width / (float)displaySize.height, 0.1f, 10.0f);
    proj(1, 1) *= -1;

    ubo.ProjView = proj * view;

    void* data = nullptr;
    vkMapMemory(VKRenderer::getInstance().getDevice(), mUniformStagingBufferMemory, 0, sizeof(ubo), 0, &data);
    assert(data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(VKRenderer::getInstance().getDevice(), mUniformStagingBufferMemory);

    VKRenderer::getInstance().copyBuffer(mUniformStagingBuffer, mUniformBuffer, sizeof(ubo));
}