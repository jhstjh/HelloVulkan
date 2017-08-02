#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include <cassert>
#include <memory>
#include <vector>
#include "engine.h"
#include "VKFuncs.h"
#include "VKRenderer.h"
#include "Model.h"
#include "ShadowMap.h"

namespace VK_RENDERER
{
class VKRendererImpl : public VKRenderer
{
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char* layerPrefix, const char* msg, void* userData) {
        LOGW("validation layer: %s\n", msg);
        return VK_FALSE;
    }

public:
    VKRendererImpl()
    {

    }

    virtual ~VKRendererImpl()
    {
        for (auto &model : mModels)
        {
            delete model;
        }
        mModels.clear();

        vkDestroySurfaceKHR(mInstance, mSurface, nullptr);
        vkDestroyDevice(mDevice, nullptr);
        vkDestroyInstance(mInstance, nullptr);

        unloadVKLibs();
    }

    void init(struct engine* engine) final
    {
        mEngine = engine;

        AAssetManager* assetManager = engine->app->activity->assetManager;
        off_t size;

        loadVKLibs();
        loadVKFuncs();

        VkResult result = VK_ERROR_INITIALIZATION_FAILED;

        // Init instance
        VkApplicationInfo applicationInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        applicationInfo.pApplicationName = "JS";
        applicationInfo.applicationVersion = 1;
        applicationInfo.pEngineName = "StenGineVK";
        applicationInfo.engineVersion = 1;

        // instance layers and extensions
        uint32_t count = 0;
        result = vkEnumerateInstanceLayerProperties(&count, nullptr);
        assert(result == VK_SUCCESS);

        std::vector<VkLayerProperties> layers(count);
        result = vkEnumerateInstanceLayerProperties(&count, layers.data());
        assert(result == VK_SUCCESS);

        result = vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        assert(result == VK_SUCCESS);

        std::vector<VkExtensionProperties> extensions(count);

        result = vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
        assert(result == VK_SUCCESS);

        std::vector<const char*> extNames;
        for (auto& extension : extensions)
        {
            extNames.push_back(extension.extensionName);
        }

        std::vector<const char*> layerNames;
        for (auto& layer : layers)
        {
            layerNames.push_back(layer.layerName);
        }

        const char *instance_layers[] = {
            "VK_LAYER_GOOGLE_threading",
            "VK_LAYER_LUNARG_parameter_validation",
            "VK_LAYER_LUNARG_object_tracker",
            "VK_LAYER_LUNARG_core_validation",
            "VK_LAYER_LUNARG_image",
            "VK_LAYER_LUNARG_swapchain",
            "VK_LAYER_GOOGLE_unique_objects"
        };

        uint32_t instance_layer_request_count =
            sizeof(instance_layers) / sizeof(instance_layers[0]);
        for (uint32_t i = 0; i < instance_layer_request_count; i++) {
            bool found = false;
            for (uint32_t j = 0; j < layers.size(); j++) {
                if (strcmp(instance_layers[i], layerNames[j]) == 0) {
                    found = true;
                }
            }
            if (!found) {
                assert(0);
            }
        }

        VkInstanceCreateInfo instanceCreateInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        instanceCreateInfo.pApplicationInfo = &applicationInfo;
        instanceCreateInfo.enabledExtensionCount = extNames.size();
        instanceCreateInfo.ppEnabledExtensionNames = extNames.data();
        instanceCreateInfo.enabledLayerCount = layerNames.size();
        instanceCreateInfo.ppEnabledLayerNames = layerNames.data();

        result = vkCreateInstance(&instanceCreateInfo, nullptr, &mInstance);
        assert(result == VK_SUCCESS);

        assert(mInstance);

        // Init device
        uint32_t gpuCount = 0;
        result = vkEnumeratePhysicalDevices(mInstance, &gpuCount, nullptr);
        assert(result == VK_SUCCESS);

        std::vector<VkPhysicalDevice> devices;
        devices.resize(gpuCount);

        result = vkEnumeratePhysicalDevices(mInstance, &gpuCount, devices.data());
        assert(result == VK_SUCCESS);

        mPhysicalDevice = devices[0];

        // Setup debug callback
        VkDebugReportCallbackCreateInfoEXT debugReportCallbackCreateInfo = {};
        debugReportCallbackCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debugReportCallbackCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
        debugReportCallbackCreateInfo.pfnCallback = debugCallback;

        PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT =
            reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(
                loadFuncFromValidationLib("vkCreateDebugReportCallbackEXT"));

        VkDebugReportCallbackEXT debugReportCallback;
        result = vkCreateDebugReportCallbackEXT(mInstance, &debugReportCallbackCreateInfo, nullptr, &debugReportCallback);
        assert(result == VK_SUCCESS);

        // physical device layers and extensions
        result = vkEnumerateDeviceLayerProperties(mPhysicalDevice, &count, nullptr);
        assert(result == VK_SUCCESS);

        std::vector<VkLayerProperties> deviceLayers(count);

        result = vkEnumerateDeviceLayerProperties(mPhysicalDevice, &count, deviceLayers.data());
        assert(result == VK_SUCCESS);

        result = vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &count, nullptr);
        assert(result == VK_SUCCESS);

        std::vector<VkExtensionProperties> deviceExtensions(count);

        result = vkEnumerateDeviceExtensionProperties(mPhysicalDevice, nullptr, &count, deviceExtensions.data());
        assert(result == VK_SUCCESS);

        std::vector<const char*> deviceExtNames;
        for (auto& dextension : deviceExtensions)
        {
            deviceExtNames.push_back(dextension.extensionName);
        }

        std::vector<const char*> deviceLayerNames;
        for (auto& dlayer : deviceLayers)
        {
            deviceLayerNames.push_back(dlayer.layerName);
        }

        // init surface
        VkAndroidSurfaceCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
        createInfo.window = engine->app->window;

        result = vkCreateAndroidSurfaceKHR(mInstance, &createInfo, nullptr, &mSurface);
        assert(result == VK_SUCCESS);

        VkDeviceQueueCreateInfo queueCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        queueCreateInfo.queueFamilyIndex = 0;
        queueCreateInfo.queueCount = 1;
        float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo deviceCreateInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = deviceExtNames.size();
        deviceCreateInfo.ppEnabledExtensionNames = deviceExtNames.data();
        deviceCreateInfo.enabledLayerCount = deviceLayerNames.size();
        deviceCreateInfo.ppEnabledLayerNames = deviceLayerNames.data();


        // find queue family
        uint32_t graphicsFamilyIdx = 0;
        uint32_t presentFamilyIdx = 0;

        uint32_t queueFamilyCount = 0;

        vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &queueFamilyCount, queueFamilies.data());

        uint32_t i = 0;
        for (const auto& queueFamily : queueFamilies) {
            if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsFamilyIdx = i;
            }

            VkBool32 presentSupport = false;
            result = vkGetPhysicalDeviceSurfaceSupportKHR(mPhysicalDevice, i, mSurface, &presentSupport);
            assert(result == VK_SUCCESS);

            if (queueFamily.queueCount > 0 && presentSupport) {
                presentFamilyIdx = i;
            }

            i++;
        }

        uint32_t queueFamilyIndices[] = { graphicsFamilyIdx, presentFamilyIdx };

        // create device
        result = vkCreateDevice(mPhysicalDevice, &deviceCreateInfo, nullptr, &mDevice);
        assert(result == VK_SUCCESS);

        vkGetDeviceQueue(mDevice, 0, 0, &mQueue);

        // create swap chain
        VkSurfaceCapabilitiesKHR surfaceCapabilities;
        result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(mPhysicalDevice, mSurface,
            &surfaceCapabilities);
        assert(result == VK_SUCCESS);

        uint32_t formatCount = 0;
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mSurface, &formatCount, nullptr);
        assert(result == VK_SUCCESS);

        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(mPhysicalDevice, mSurface, &formatCount, formats.data());
        assert(result == VK_SUCCESS);

        uint32_t chosenFormat;
        for (chosenFormat = 0; chosenFormat < formatCount; chosenFormat++) {
            if (formats[chosenFormat].format == VK_FORMAT_R8G8B8A8_UNORM)
                break;
        }
        assert(chosenFormat < formatCount);

        mDisplaySize = surfaceCapabilities.currentExtent;
        DisplayFormat = formats[chosenFormat].format;


        // create swapchain
        VkSwapchainCreateInfoKHR swapchainCreateInfo = {};
        swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCreateInfo.pNext = nullptr;
        swapchainCreateInfo.surface = mSurface;
        swapchainCreateInfo.minImageCount = surfaceCapabilities.minImageCount;
        swapchainCreateInfo.imageFormat = formats[chosenFormat].format;
        swapchainCreateInfo.imageColorSpace = formats[chosenFormat].colorSpace;
        swapchainCreateInfo.imageExtent = surfaceCapabilities.currentExtent;
        swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainCreateInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        swapchainCreateInfo.imageArrayLayers = 1;
        swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainCreateInfo.queueFamilyIndexCount = 2;
        swapchainCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
        swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
        swapchainCreateInfo.clipped = VK_FALSE;
        swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

        result = vkCreateSwapchainKHR(mDevice, &swapchainCreateInfo,
            nullptr, &mSwapchain);
        assert(result == VK_SUCCESS);

        result = vkGetSwapchainImagesKHR(mDevice, mSwapchain,
            &mSwapchainLength, nullptr);
        assert(result == VK_SUCCESS);

        // create render pass
        {
            VkAttachmentDescription attachmentDescriptions;
            attachmentDescriptions.format = DisplayFormat;
            attachmentDescriptions.samples = VK_SAMPLE_COUNT_1_BIT;
            attachmentDescriptions.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachmentDescriptions.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachmentDescriptions.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachmentDescriptions.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachmentDescriptions.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachmentDescriptions.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorReference;
            colorReference.attachment = 0;
            colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkAttachmentDescription depthAttachment = {};
            depthAttachment.format = findDepthFormat();
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthAttachmentRef = {};
            depthAttachmentRef.attachment = 1;
            depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpassDescription = {};
            subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpassDescription.colorAttachmentCount = 1;
            subpassDescription.pColorAttachments = &colorReference;
            subpassDescription.pDepthStencilAttachment = &depthAttachmentRef;

            VkSubpassDependency dependency = {};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;


            std::array<VkAttachmentDescription, 2> attachments = { attachmentDescriptions, depthAttachment };

            VkRenderPassCreateInfo renderPassCreateInfo;
            renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            renderPassCreateInfo.pNext = nullptr;
            renderPassCreateInfo.attachmentCount = attachments.size();
            renderPassCreateInfo.pAttachments = attachments.data();
            renderPassCreateInfo.subpassCount = 1;
            renderPassCreateInfo.pSubpasses = &subpassDescription;
            renderPassCreateInfo.dependencyCount = 1;
            renderPassCreateInfo.pDependencies = &dependency;

            result = vkCreateRenderPass(mDevice, &renderPassCreateInfo,
                nullptr, &mRenderPass);
            assert(result == VK_SUCCESS);

            attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;

            result = vkCreateRenderPass(mDevice, &renderPassCreateInfo,
                nullptr, &mRenderPassClear);
            assert(result == VK_SUCCESS);
        }

        // create command pool
        VkCommandPoolCreateInfo cmdPoolCreateInfo;
        cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolCreateInfo.pNext = nullptr;
        cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cmdPoolCreateInfo.queueFamilyIndex = 0;

        result = vkCreateCommandPool(mDevice, &cmdPoolCreateInfo, nullptr, &mCmdPool);
        assert(result == VK_SUCCESS);

        // create depth resource
        {
            VkFormat depthFormat = findDepthFormat();

            createImage(mDisplaySize.width, mDisplaySize.height, depthFormat, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mDepthImage, mDepthImageMemory);
            createImageView(mDepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, mDepthImageView);

            transitionImageLayout(mDepthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

        }

        // create framebuffer
        uint32_t SwapchainImagesCount = 0;
        result = vkGetSwapchainImagesKHR(mDevice, mSwapchain, &SwapchainImagesCount, nullptr);
        assert(result == VK_SUCCESS);

        std::vector<VkImage> displayImages(SwapchainImagesCount);
        result = vkGetSwapchainImagesKHR(mDevice, mSwapchain, &SwapchainImagesCount, displayImages.data());
        assert(result == VK_SUCCESS);

        mDisplayViews.resize(SwapchainImagesCount);
        for (uint32_t i = 0; i < SwapchainImagesCount; i++)
        {
            createImageView(displayImages[i], DisplayFormat, VK_IMAGE_ASPECT_COLOR_BIT, mDisplayViews[i]);
        }

        mFramebuffers.resize(mSwapchainLength);
        for (uint32_t i = 0; i < mSwapchainLength; i++)
        {
            std::array<VkImageView, 2> attachments =
            {
                mDisplayViews[i],
                mDepthImageView
            };

            VkFramebufferCreateInfo fbCreateInfo = {};
            fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbCreateInfo.renderPass = mRenderPass;
            fbCreateInfo.layers = 1;
            fbCreateInfo.attachmentCount = attachments.size();
            fbCreateInfo.pAttachments = attachments.data();
            fbCreateInfo.width = static_cast<uint32_t>(mDisplaySize.width);
            fbCreateInfo.height = static_cast<uint32_t>(mDisplaySize.height);

            result = vkCreateFramebuffer(mDevice, &fbCreateInfo, nullptr,
                &mFramebuffers[i]);
            assert(result == VK_SUCCESS);
        }

        VkSemaphoreCreateInfo semaphoreCreateInfo;
        semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreCreateInfo.pNext = nullptr;
        semaphoreCreateInfo.flags = 0;

        result = vkCreateSemaphore(VKRenderer::getInstance().getDevice(), &semaphoreCreateInfo, nullptr, &mImageAvailableSemaphore);
        assert(result == VK_SUCCESS);

        result = vkCreateSemaphore(VKRenderer::getInstance().getDevice(), &semaphoreCreateInfo, nullptr, &mRenderFinishedSemaphore);
        assert(result == VK_SUCCESS);

        mPrimaryCmdBuffer.resize(mSwapchainLength);
        for (uint32_t bufferIndex = 0; bufferIndex < mSwapchainLength; bufferIndex++)
        {
            VkCommandBufferAllocateInfo cmdBufferAllocationInfo{};
            cmdBufferAllocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdBufferAllocationInfo.pNext = nullptr;
            cmdBufferAllocationInfo.commandPool = mCmdPool;
            cmdBufferAllocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdBufferAllocationInfo.commandBufferCount = 1;

            result = vkAllocateCommandBuffers(mDevice, &cmdBufferAllocationInfo, &mPrimaryCmdBuffer[bufferIndex]);
            assert(result == VK_SUCCESS);
        }

        mShadowMap = std::make_unique<ShadowMap>();

        mModels.push_back(new Model("chalet", mEngine, 0.f));
        mModels.push_back(new Model("cube", mEngine, 2.f));
    }

    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiliting, VkImageUsageFlags usage,
        VkMemoryPropertyFlags properties, VkImage &image, VkDeviceMemory & imageMemory) final
    {
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = tiliting;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
        imageInfo.usage = usage;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.flags = 0;

        auto result = vkCreateImage(mDevice, &imageInfo, nullptr, &image);
        ASSERT_VK_SUCCESS(result);

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(mDevice, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;

        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &memProperties);

        uint32_t memoryTypeIndex;
        for (memoryTypeIndex = 0; memoryTypeIndex < memProperties.memoryTypeCount; memoryTypeIndex++) {
            if ((memRequirements.memoryTypeBits & (1 << memoryTypeIndex)) && (memProperties.memoryTypes[memoryTypeIndex].propertyFlags & properties) == properties)
            {
                break;
            }
        }
        assert(memoryTypeIndex != memProperties.memoryTypeCount);

        allocInfo.memoryTypeIndex = memoryTypeIndex;
        result = vkAllocateMemory(mDevice, &allocInfo, nullptr, &imageMemory);
        ASSERT_VK_SUCCESS(result);

        result = vkBindImageMemory(mDevice, image, imageMemory, 0);
        ASSERT_VK_SUCCESS(result);
    }

    VkCommandBuffer beginSingleTimeCommands()
    {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = mCmdPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(mDevice, &allocInfo, &commandBuffer);

        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        return commandBuffer;
    }

    void endSingleTimeCommands(VkCommandBuffer commandBuffer)
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(mQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(mQueue);

        vkFreeCommandBuffers(mDevice, mCmdPool, 1, &commandBuffer);
    }

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) final
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy copyRegion = {};
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        endSingleTimeCommands(commandBuffer);
    }

    void copyImage(VkImage srcImage, VkImage dstImage, uint32_t width, uint32_t height) final
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageSubresourceLayers subResource = {};
        subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subResource.baseArrayLayer = 0;
        subResource.mipLevel = 0;
        subResource.layerCount = 1;

        VkImageCopy region = {};
        region.srcSubresource = subResource;
        region.dstSubresource = subResource;
        region.srcOffset = { 0, 0, 0 };
        region.dstOffset = { 0, 0, 0 };
        region.extent.width = width;
        region.extent.height = height;
        region.extent.depth = 1;

        vkCmdCopyImage(commandBuffer, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            dstImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        endSingleTimeCommands(commandBuffer);
    }

    bool hasStencilComponent(VkFormat format)
    {
        return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
    }

    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) final
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;

        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            if (hasStencilComponent(format))
            {
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }
        else {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_PREINITIALIZED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }
        else {
            assert(false);
        }

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        endSingleTimeCommands(commandBuffer);
    }

    void createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, VkImageView &imageView) final
    {
        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectFlags;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        auto result = vkCreateImageView(mDevice, &viewInfo, nullptr, &imageView);
        ASSERT_VK_SUCCESS(result);
    }

    VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features)
    {
        for (VkFormat format : candidates)
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            {
                return format;
            }
            else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }

        assert(false);
    }

    VkFormat findDepthFormat()
    {
        return findSupportedFormat(
        { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }

    // create buffer lambda
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory) final
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        auto result = vkCreateBuffer(mDevice, &bufferInfo, nullptr, &buffer);
        assert(result == VK_SUCCESS);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(mDevice, buffer, &memRequirements);

        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &memProperties);

        uint32_t memoryTypeIndex;
        for (memoryTypeIndex = 0; memoryTypeIndex < memProperties.memoryTypeCount; memoryTypeIndex++) {
            if ((memRequirements.memoryTypeBits & (1 << memoryTypeIndex)) && (memProperties.memoryTypes[memoryTypeIndex].propertyFlags & properties) == properties)
            {
                break;
            }
        }
        assert(memoryTypeIndex != memProperties.memoryTypeCount);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        result = vkAllocateMemory(mDevice, &allocInfo, nullptr, &bufferMemory);
        assert(result == VK_SUCCESS);

        vkBindBufferMemory(mDevice, buffer, bufferMemory, 0);
    };

    void clearFrame(uint32_t nextIndex)
    {
        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0].color = { 0.3f, 0.3f, 0.3f, 1.0f };
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo renderPassBeginInfo{};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.pNext = nullptr;
        renderPassBeginInfo.renderPass = mRenderPassClear;
        renderPassBeginInfo.framebuffer = mFramebuffers[nextIndex];
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent = mDisplaySize;
        renderPassBeginInfo.clearValueCount = clearValues.size();
        renderPassBeginInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(mPrimaryCmdBuffer[nextIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(mPrimaryCmdBuffer[nextIndex]);
    }

    void draw() final
    {
        vkQueueWaitIdle(mQueue);

        uint32_t nextIndex;
        VkResult result = vkAcquireNextImageKHR(mDevice, mSwapchain, 0xFFFFFFFFFFFFFFFFull, mImageAvailableSemaphore, VK_NULL_HANDLE, &nextIndex);
        assert(result == VK_SUCCESS);

        VkCommandBufferBeginInfo cmdBufferBeginInfo{};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferBeginInfo.pNext = nullptr;
        cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        cmdBufferBeginInfo.pInheritanceInfo = nullptr;

        result = vkBeginCommandBuffer(mPrimaryCmdBuffer[nextIndex], &cmdBufferBeginInfo);
        assert(result == VK_SUCCESS);

        clearFrame(nextIndex);

        result = vkEndCommandBuffer(mPrimaryCmdBuffer[nextIndex]);
        assert(result == VK_SUCCESS);

        std::vector<VkCommandBuffer> commandbuffers;
        commandbuffers.push_back(mPrimaryCmdBuffer[nextIndex]);
        for (auto &model : mModels)
        {
            commandbuffers.push_back(model->getCommandBuffer(nextIndex));
        }

        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = nullptr;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &mImageAvailableSemaphore;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = commandbuffers.size();
        submitInfo.pCommandBuffers = commandbuffers.data();
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &mRenderFinishedSemaphore;

        result = vkQueueSubmit(mQueue, 1, &submitInfo, VK_NULL_HANDLE);
        assert(result == VK_SUCCESS);

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext = nullptr;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &mSwapchain;
        presentInfo.pImageIndices = &nextIndex;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &mRenderFinishedSemaphore;
        presentInfo.pResults = nullptr;

        vkQueuePresentKHR(mQueue, &presentInfo);
    }

    void update() final
    {
        for (auto &model : mModels)
        {
            model->update();
        }
    }

    ShadowMap* getShadowMap() final
    {
        return mShadowMap.get();
    }

    VkDevice &getDevice() final
    {
        return mDevice;
    }

    VkPhysicalDevice &getPhysicalDevice() final
    {
        return mPhysicalDevice;
    }

    VkExtent2D &getDisplaySize() final
    {
        return mDisplaySize;
    }

    uint32_t getSwapChainLength() final
    {
        return mSwapchainLength;
    }

    VkFramebuffer &getFramebuffer(uint32_t index) final
    {
        assert(index < mFramebuffers.size());
        return mFramebuffers[index];
    }

    VkRenderPass &getRenderPass()
    {
        return mRenderPass;
    }

    VkCommandPool &getCommandPool()
    {
        return mCmdPool;
    }

    void release() final
    {
        _instance = nullptr;
        delete this;
    }

public:
    VkInstance          mInstance;
    VkSurfaceKHR        mSurface;
    VkPhysicalDevice    mPhysicalDevice;
    VkDevice            mDevice;
    VkQueue             mQueue;

    VkSwapchainKHR      mSwapchain;
    uint32_t            mSwapchainLength;

    VkExtent2D          mDisplaySize;
    VkFormat            DisplayFormat;

    // array of frame buffers and views
    std::vector<VkFramebuffer>      mFramebuffers;
    std::vector<VkImageView>        mDisplayViews;

    VkRenderPass        mRenderPass;
    VkRenderPass        mRenderPassClear;
    VkCommandPool       mCmdPool;
    std::vector<VkCommandBuffer>    mPrimaryCmdBuffer;
    VkImage             mDepthImage;
    VkDeviceMemory      mDepthImageMemory;
    VkImageView         mDepthImageView;

    VkSemaphore         mImageAvailableSemaphore;
    VkSemaphore         mRenderFinishedSemaphore;

    engine*           mEngine{ nullptr };
    std::vector<Model*>            mModels;
    std::unique_ptr<ShadowMap>     mShadowMap;
};

}

VKRenderer* VKRenderer::_instance = nullptr;

void VKRenderer::create()
{
    assert(_instance == nullptr);
    _instance = static_cast<VKRenderer*>(new VK_RENDERER::VKRendererImpl());
}

VKRenderer &VKRenderer::getInstance()
{
    assert(_instance);
    return *_instance;
}