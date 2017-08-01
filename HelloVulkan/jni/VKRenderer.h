#pragma once
#include <memory>
#include "VKFuncs.h"

struct engine;

class VKRenderer
{
public:
    static void create();
    static VKRenderer &getInstance();

    virtual void init(struct engine* engine) = 0;

    virtual VkDevice &getDevice() = 0;
    virtual VkPhysicalDevice &getPhysicalDevice() = 0;
    virtual VkExtent2D &getDisplaySize() = 0;
    virtual VkFramebuffer &getFramebuffer(uint32_t index) = 0;
    virtual VkRenderPass &getRenderPass() = 0;
    virtual VkCommandPool &getCommandPool() = 0;

    virtual uint32_t getSwapChainLength() = 0;
    
    virtual void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiliting, VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties, VkImage &image, VkDeviceMemory & imageMemory) = 0;

    virtual void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) = 0;
    virtual void copyImage(VkImage srcImage, VkImage dstImage, uint32_t width, uint32_t height) = 0;
    virtual void createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView &imageView) = 0;
    virtual void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) = 0;
    virtual void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory) = 0;

    virtual void draw() = 0;
    virtual void update() = 0;

    virtual void release() = 0;
    
protected:
    static VKRenderer* _instance;

};