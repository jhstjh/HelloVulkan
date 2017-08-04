#pragma once
#include <VKFuncs.h>
#include "ext/mathfu/glsl_mappings.h"

class ShadowMap
{
public:
    ShadowMap();
    ~ShadowMap();

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

    VkRenderPass getRenderPassClear()
    {
        return mShadowRenderPassClear;
    }

    VkFramebuffer getFramebuffer()
    {
        return mShadowDepthFramebuffer;
    }

private:
    VkRenderPass    mShadowRenderPass;
    VkRenderPass    mShadowRenderPassClear;
    VkImage         mShadowDepthImage;
    VkImageView     mShadowDepthImageView;
    VkDeviceMemory  mShadowDepthImageMemory;
    VkSampler       mShadowDepthImageSampler;
    VkFramebuffer   mShadowDepthFramebuffer;
};