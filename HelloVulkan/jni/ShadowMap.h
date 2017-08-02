#pragma once
#include <VKFuncs.h>
#include "ext/mathfu/glsl_mappings.h"

class ShadowMap
{
public:
    ShadowMap();

    VkImageView getShadowMapView()
    {
        return mShadowDepthImageView;
    }

    VkSampler getShadowMapSampler()
    {
        return mShadowDepthImageSampler;
    }

    VkRenderPass getRenderPass()
    {
        return mShadowRenderPass;
    }

    VkFramebuffer getFramebuffer()
    {
        return mShadowDepthFramebuffer;
    }

private:
    struct
    {
        mathfu::mat4 depthMVP;
    } UboShadow;

    VkRenderPass    mShadowRenderPass;
    VkRenderPass    mShadowRenderPassClear;
    VkImage         mShadowDepthImage;
    VkImageView     mShadowDepthImageView;
    VkDeviceMemory  mShadowDepthImageMemory;
    VkSampler       mShadowDepthImageSampler;
    VkFramebuffer   mShadowDepthFramebuffer;
    VkBuffer        mScreenQuadVertexBuffer;
    VkDeviceMemory  mScreenQuadVertexBufferMemory;
    VkBuffer        mScreenQuadIndexBuffer;
    VkDeviceMemory  mScreenQuadIndexBufferMemory;
    VkBuffer        mUniformBuffer;
    VkDeviceMemory  mUniformBufferMemory;
};