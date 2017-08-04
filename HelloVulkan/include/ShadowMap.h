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

    VkFramebuffer getFramebuffer()
    {
        return mShadowDepthFramebuffer;
    }

    static const uint32_t SHADOWMAP_DIM;

private:
    VkRenderPass    mShadowRenderPass;
    VkImage         mShadowDepthImage;
    VkImageView     mShadowDepthImageView;
    VkDeviceMemory  mShadowDepthImageMemory;
    VkSampler       mShadowDepthImageSampler;
    VkFramebuffer   mShadowDepthFramebuffer;
};