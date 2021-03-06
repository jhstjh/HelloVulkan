#include <array>
#include <cassert>
#include <vector>
#include "ShadowMap.h"
#include "VKRenderer.h"

const uint32_t ShadowMap::SHADOWMAP_DIM = 2048;

ShadowMap::ShadowMap()
{
    // create shadow render pass
    {
        VkAttachmentDescription attachmentDescription{};
        attachmentDescription.format = VK_FORMAT_D16_UNORM;
        attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthReference{};
        depthReference.attachment = 0;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 0;
        subpass.pDepthStencilAttachment = &depthReference;

        std::array<VkSubpassDependency, 2> dependencies;

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassCreateInfo{};
        renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassCreateInfo.attachmentCount = 1;
        renderPassCreateInfo.pAttachments = &attachmentDescription;
        renderPassCreateInfo.subpassCount = 1;
        renderPassCreateInfo.pSubpasses = &subpass;
        renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassCreateInfo.pDependencies = dependencies.data();

        auto result = vkCreateRenderPass(VKRenderer::getInstance().getDevice(), &renderPassCreateInfo,
            nullptr, &mShadowRenderPass);
        assert(result == VK_SUCCESS);
    }

    // create shadow depth texture
    {
        VKRenderer::getInstance().createImage(SHADOWMAP_DIM, SHADOWMAP_DIM, VK_FORMAT_D16_UNORM, VK_IMAGE_TILING_OPTIMAL, 
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mShadowDepthImage, mShadowDepthImageMemory);

        VKRenderer::getInstance().createImageView(mShadowDepthImage, VK_FORMAT_D16_UNORM, VK_IMAGE_ASPECT_DEPTH_BIT, mShadowDepthImageView);
    
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = samplerInfo.addressModeV = samplerInfo.addressModeU;
        samplerInfo.mipLodBias = 0.f;
        samplerInfo.maxAnisotropy = 1.f;
        samplerInfo.minLod = 0.f;
        samplerInfo.maxLod = 1.f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

        auto result = vkCreateSampler(VKRenderer::getInstance().getDevice(), &samplerInfo, nullptr, &mShadowDepthImageSampler);
        ASSERT_VK_SUCCESS(result);
    }

    // create framebuffer
    {
        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = mShadowRenderPass;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &mShadowDepthImageView;
        createInfo.width = SHADOWMAP_DIM;
        createInfo.height = SHADOWMAP_DIM;
        createInfo.layers = 1;

        auto result = vkCreateFramebuffer(VKRenderer::getInstance().getDevice(), &createInfo, nullptr, &mShadowDepthFramebuffer);
        assert(result == VK_SUCCESS);
    }
}

ShadowMap::~ShadowMap()
{
    vkDestroyRenderPass(VKRenderer::getInstance().getDevice(), mShadowRenderPass, nullptr);
    vkDestroyImageView(VKRenderer::getInstance().getDevice(), mShadowDepthImageView, nullptr);
    vkDestroyImage(VKRenderer::getInstance().getDevice(), mShadowDepthImage, nullptr);
    vkFreeMemory(VKRenderer::getInstance().getDevice(), mShadowDepthImageMemory, nullptr);
    vkDestroySampler(VKRenderer::getInstance().getDevice(), mShadowDepthImageSampler, nullptr);
    vkDestroyFramebuffer(VKRenderer::getInstance().getDevice(), mShadowDepthFramebuffer, nullptr);
}