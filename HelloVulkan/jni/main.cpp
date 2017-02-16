/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

//BEGIN_INCLUDE(all)
#include <jni.h>
#include <errno.h>

#include <EGL/egl.h>
#include <GLES/gl.h>

#include <android/sensor.h>
#include <android/log.h>
#include "../native_app_glue/android_native_app_glue.h"

#include <dlfcn.h>
#include <assert.h>
#include <vector>
#include <cstdint>
#include <array>
#include <chrono>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

#include "VKFuncs.h"

#include "mathfu/glsl_mappings.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace mathfu;

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "native-activity", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "native-activity", __VA_ARGS__))

#define LOG_ACCELEROMETER false

#define ASSERT_VK_SUCCESS(result) assert(result == VK_SUCCESS)


typedef PFN_vkVoidFunction(VKAPI_CALL * PFN_vkGetProcAddressNV) (const char *name);
static PFN_vkGetInstanceProcAddr android_getVkProc = NULL;

static VkInstance getVkProcInstance = NULL;
static void* vulkan_so = NULL;

VkInstance          _instance;
VkSurfaceKHR        gSurface;
VkPhysicalDevice    gPhysicalDevice;
VkDevice            gDevice;
VkQueue             gQueue;

VkSwapchainKHR      gSwapchain;
uint32_t            gSwapchainLength;

VkExtent2D          gDisplaySize;
VkFormat            gDisplayFormat;

// array of frame buffers and views
std::vector<VkFramebuffer>      gFramebuffers;
std::vector<VkImageView>        gDisplayViews;

VkRenderPass        gRenderPass;
VkCommandPool       gCmdPool;
std::vector<VkCommandBuffer>    gCmdBuffer;
VkBuffer            gVertexBuffer;
VkDeviceMemory      gVertexBufferMemory;
VkBuffer            gIndexBuffer;
VkDeviceMemory      gIndexBufferMemory;
VkBuffer            gUniformStagingBuffer;
VkDeviceMemory      gUniformStagingBufferMemory;
VkBuffer            gUniformBuffer;
VkDeviceMemory      gUniformBufferMemory;
VkDescriptorPool    gDescriptorPool;
VkDescriptorSet     gDescriptorSet;
VkImage             gStagingImage;
VkDeviceMemory      gStagingImageMemory;
VkImage             gTextureImage;
VkDeviceMemory      gTextureImageMemory;
VkImageView         gTextureImageView;
VkSampler           gTextureSampler;

uint32_t            gCmdBufferLen;
VkSemaphore         gimageAvailableSemaphore;
VkSemaphore         grenderFinishedSemaphore;

VkDescriptorSetLayout gDescriptorSetLayout;
VkPipelineLayout  gPLayout;
VkPipelineCache   gPCache;
VkPipeline        gPipeline;

bool init = false;

struct Vertex
{
    vec2 pos;
    vec3 color;
    vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription = {};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions = {};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }
};

const std::vector<Vertex> vertices = {
    { { -0.5f, -0.5f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
    { {  0.5f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
    { {  0.5f,  0.5f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
    { { -0.5f,  0.5f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }
};

const std::vector<uint16_t> indices = {
    0, 1, 2, 2, 3, 0
};

struct UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 proj;
};


/**
 * Our saved state data.
 */
struct saved_state {
    float angle;
    int32_t x;
    int32_t y;
};

/**
 * Shared state for our app.
 */
struct engine {
    struct android_app* app;

    ASensorManager* sensorManager;
    const ASensor* accelerometerSensor;
    ASensorEventQueue* sensorEventQueue;

    int animating;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int32_t width;
    int32_t height;
    struct saved_state state;
};

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objType, uint64_t obj, size_t location, int32_t code, const char* layerPrefix, const char* msg, void* userData) {
    LOGW("validation layer: %s\n", msg);
    return VK_FALSE;
}

void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiliting, VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties, VkImage &image, VkDeviceMemory & imageMemory)
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

    auto result = vkCreateImage(gDevice, &imageInfo, nullptr, &image);
    ASSERT_VK_SUCCESS(result);

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(gDevice, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(gPhysicalDevice, &memProperties);

    uint32_t memoryTypeIndex;
    for (memoryTypeIndex = 0; memoryTypeIndex < memProperties.memoryTypeCount; memoryTypeIndex++) {
        if ((memRequirements.memoryTypeBits & (1 << memoryTypeIndex)) && (memProperties.memoryTypes[memoryTypeIndex].propertyFlags & properties) == properties)
        {
            break;
        }
    }
    assert(memoryTypeIndex != memProperties.memoryTypeCount);

    allocInfo.memoryTypeIndex = memoryTypeIndex;
    result = vkAllocateMemory(gDevice, &allocInfo, nullptr, &imageMemory);
    ASSERT_VK_SUCCESS(result);

    result = vkBindImageMemory(gDevice, image, imageMemory, 0);
    ASSERT_VK_SUCCESS(result);
}

VkCommandBuffer beginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = gCmdPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(gDevice, &allocInfo, &commandBuffer);

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

    vkQueueSubmit(gQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(gQueue);

    vkFreeCommandBuffers(gDevice, gCmdPool, 1, &commandBuffer);
}

void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion = {};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
}

void copyImage(VkImage srcImage, VkImage dstImage, uint32_t width, uint32_t height)
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

void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
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
    else {
        assert(false);
    }

    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    endSingleTimeCommands(commandBuffer);
}

void createImageView(VkImage image, VkFormat format, VkImageView &imageView) 
{
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    auto result = vkCreateImageView(gDevice, &viewInfo, nullptr, &imageView);
    ASSERT_VK_SUCCESS(result);
}

/**
 * Initialize an EGL context for the current display.
 */
static int engine_init_display(struct engine* engine) {

    AAssetManager* assetManager = engine->app->activity->assetManager;
    off_t size;

    loadVKLibs();
    loadVKFuncs();

    VkResult result = VK_ERROR_INITIALIZATION_FAILED;

    // Init instance
    VkApplicationInfo applicationInfo = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
    applicationInfo.pApplicationName = "JS";
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName = "GameWorks SDK Sample";
    applicationInfo.engineVersion = 1;

    // instance layers and extensions
    uint32_t count = 0;
    result = vkEnumerateInstanceLayerProperties(&count, NULL);
    assert(result == VK_SUCCESS);

    std::vector<VkLayerProperties> layers(count);
    result = vkEnumerateInstanceLayerProperties(&count, layers.data());
    assert(result == VK_SUCCESS);

    result = vkEnumerateInstanceExtensionProperties(NULL, &count, NULL);
    assert(result == VK_SUCCESS);

    std::vector<VkExtensionProperties> extensions(count);

    result = vkEnumerateInstanceExtensionProperties(NULL, &count, extensions.data());
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

    result = vkCreateInstance(&instanceCreateInfo, NULL, &_instance);
    assert(result == VK_SUCCESS);

    assert(_instance);
    getVkProcInstance = _instance;

    // Init device
    uint32_t gpuCount = 0;
    result = vkEnumeratePhysicalDevices(getVkProcInstance, &gpuCount, nullptr);
    assert(result == VK_SUCCESS);

    std::vector<VkPhysicalDevice> devices;
    devices.resize(gpuCount);

    result = vkEnumeratePhysicalDevices(getVkProcInstance, &gpuCount, devices.data());
    assert(result == VK_SUCCESS);

    gPhysicalDevice = devices[0];

    // Setup debug callback
    VkDebugReportCallbackCreateInfoEXT debugReportCallbackCreateInfo = {};
    debugReportCallbackCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    debugReportCallbackCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
    debugReportCallbackCreateInfo.pfnCallback = debugCallback;

    PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT =
        reinterpret_cast<PFN_vkCreateDebugReportCallbackEXT>(
            loadFuncFromValidationLib("vkCreateDebugReportCallbackEXT"));

    VkDebugReportCallbackEXT debugReportCallback;
    result = vkCreateDebugReportCallbackEXT(getVkProcInstance, &debugReportCallbackCreateInfo, nullptr, &debugReportCallback);
    assert(result == VK_SUCCESS);

    // physical device layers and extensions
    result = vkEnumerateDeviceLayerProperties(gPhysicalDevice, &count, NULL);
    assert(result == VK_SUCCESS);

    std::vector<VkLayerProperties> deviceLayers(count);

    result = vkEnumerateDeviceLayerProperties(gPhysicalDevice, &count, deviceLayers.data());
    assert(result == VK_SUCCESS);

    result = vkEnumerateDeviceExtensionProperties(gPhysicalDevice, NULL, &count, NULL);
    assert(result == VK_SUCCESS);

    std::vector<VkExtensionProperties> deviceExtensions(count);

    result = vkEnumerateDeviceExtensionProperties(gPhysicalDevice, NULL, &count, deviceExtensions.data());
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

    result = vkCreateAndroidSurfaceKHR(getVkProcInstance, &createInfo, nullptr, &gSurface);
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

    vkGetPhysicalDeviceQueueFamilyProperties(gPhysicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gPhysicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamilyIdx = i;
        }

        VkBool32 presentSupport = false;
        result = vkGetPhysicalDeviceSurfaceSupportKHR(gPhysicalDevice, i, gSurface, &presentSupport);
        assert(result == VK_SUCCESS);

        if (queueFamily.queueCount > 0 && presentSupport) {
            presentFamilyIdx = i;
        }

        i++;
    }

    uint32_t queueFamilyIndices[] = { graphicsFamilyIdx, presentFamilyIdx };

    // create device
    result = vkCreateDevice(gPhysicalDevice, &deviceCreateInfo, nullptr, &gDevice);
    assert(result == VK_SUCCESS);

    vkGetDeviceQueue(gDevice, 0, 0, &gQueue);

    // create swap chain
    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gPhysicalDevice, gSurface,
        &surfaceCapabilities);
    assert(result == VK_SUCCESS);

    uint32_t formatCount = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(gPhysicalDevice, gSurface, &formatCount, nullptr);
    assert(result == VK_SUCCESS);

    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(gPhysicalDevice, gSurface, &formatCount, formats.data());
    assert(result == VK_SUCCESS);

    uint32_t chosenFormat;
    for (chosenFormat = 0; chosenFormat < formatCount; chosenFormat++) {
        if (formats[chosenFormat].format == VK_FORMAT_R8G8B8A8_UNORM)
            break;
    }
    assert(chosenFormat < formatCount);

    gDisplaySize = surfaceCapabilities.currentExtent;
    gDisplayFormat = formats[chosenFormat].format;


    // create swapchain
    VkSwapchainCreateInfoKHR swapchainCreateInfo = {};
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.pNext = nullptr;
    swapchainCreateInfo.surface = gSurface;
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

    result = vkCreateSwapchainKHR(gDevice, &swapchainCreateInfo,
        nullptr, &gSwapchain);
    assert(result == VK_SUCCESS);

    result = vkGetSwapchainImagesKHR(gDevice, gSwapchain,
        &gSwapchainLength, nullptr);
    assert(result == VK_SUCCESS);
    
    // create render pass
    VkAttachmentDescription attachmentDescriptions;
    attachmentDescriptions.format = gDisplayFormat;
    attachmentDescriptions.samples = VK_SAMPLE_COUNT_1_BIT;
    attachmentDescriptions.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachmentDescriptions.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachmentDescriptions.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachmentDescriptions.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachmentDescriptions.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachmentDescriptions.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorReference;
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpassDescription;
    subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpassDescription.flags = 0;
    subpassDescription.inputAttachmentCount = 0;
    subpassDescription.pInputAttachments = nullptr;
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &colorReference;
    subpassDescription.pResolveAttachments = nullptr;
    subpassDescription.pDepthStencilAttachment = nullptr;
    subpassDescription.preserveAttachmentCount = 0;
    subpassDescription.pPreserveAttachments = nullptr;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassCreateInfo;
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.pNext = nullptr;
    renderPassCreateInfo.attachmentCount = 1;
    renderPassCreateInfo.pAttachments = &attachmentDescriptions;
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpassDescription;
    renderPassCreateInfo.dependencyCount = 1;
    renderPassCreateInfo.pDependencies = &dependency;

    result = vkCreateRenderPass(gDevice, &renderPassCreateInfo,
        nullptr, &gRenderPass);
    assert(result == VK_SUCCESS);

    // create framebuffer
    uint32_t SwapchainImagesCount = 0;
    result = vkGetSwapchainImagesKHR(gDevice, gSwapchain, &SwapchainImagesCount, nullptr);
    assert(result == VK_SUCCESS);

    std::vector<VkImage> displayImages(SwapchainImagesCount);
    result = vkGetSwapchainImagesKHR(gDevice, gSwapchain, &SwapchainImagesCount, displayImages.data());
    assert(result == VK_SUCCESS);

    gDisplayViews.resize(SwapchainImagesCount);
    for (uint32_t i = 0; i < SwapchainImagesCount; i++)
    {
        createImageView(displayImages[i], gDisplayFormat, gDisplayViews[i]);
    }

    // no depth buffer for now
    VkImageView depthView = VK_NULL_HANDLE;

    gFramebuffers.resize(gSwapchainLength);
    for (uint32_t i = 0; i < gSwapchainLength; i++)
    {
        VkImageView attachments[] =
        {
            gDisplayViews[i],
            // depthView,
        };

        VkFramebufferCreateInfo fbCreateInfo;
        fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCreateInfo.pNext = nullptr;
        fbCreateInfo.renderPass = gRenderPass;
        fbCreateInfo.layers = 1;
        fbCreateInfo.attachmentCount = 1;
        fbCreateInfo.pAttachments = attachments;
        fbCreateInfo.width = static_cast<uint32_t>(gDisplaySize.width);
        fbCreateInfo.height = static_cast<uint32_t>(gDisplaySize.height);

        result = vkCreateFramebuffer(gDevice, &fbCreateInfo, nullptr,
            &gFramebuffers[i]);
        assert(result == VK_SUCCESS);
    }

    // create buffer lambda
    auto createBuffer = [](VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer, VkDeviceMemory &bufferMemory)
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        auto result = vkCreateBuffer(gDevice, &bufferInfo, nullptr, &buffer);
        assert(result == VK_SUCCESS);

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(gDevice, buffer, &memRequirements);

        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(gPhysicalDevice, &memProperties);

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

        result = vkAllocateMemory(gDevice, &allocInfo, nullptr, &bufferMemory);
        assert(result == VK_SUCCESS);

        vkBindBufferMemory(gDevice, buffer, bufferMemory, 0);
    };

    // create descriptor set layout
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

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();

    result = vkCreateDescriptorSetLayout(gDevice, &layoutInfo, nullptr, &gDescriptorSetLayout);
    assert(result == VK_SUCCESS);

    // create command pool
    VkCommandPoolCreateInfo cmdPoolCreateInfo;
    cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolCreateInfo.pNext = nullptr;
    cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdPoolCreateInfo.queueFamilyIndex = 0;

    result = vkCreateCommandPool(gDevice, &cmdPoolCreateInfo, nullptr, &gCmdPool);
    assert(result == VK_SUCCESS);

    // create texture image
    {
        int32_t texWidth, texHeight, texChannels;
        AAsset* texFile = AAssetManager_open(assetManager, "texture.jpg", AASSET_MODE_UNKNOWN);
        assert(texFile);
        auto size = AAsset_getLength(texFile);
        std::vector<uint8_t> texData(size);
        AAsset_read(texFile, texData.data(), size);
        AAsset_close(texFile);

        auto pixels = stbi_load_from_memory(texData.data(), size, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        VkDeviceSize imageSize = texWidth * texHeight * 4;

        assert(pixels);

        createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, gStagingImage, gStagingImageMemory);

        void* data;
        vkMapMemory(gDevice, gStagingImageMemory, 0, imageSize, 0, &data);
        assert(data);

        VkImageSubresource subresource = {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.mipLevel = 0;
        subresource.arrayLayer = 0;

        VkSubresourceLayout stagingImageLayout;
        vkGetImageSubresourceLayout(gDevice, gStagingImage, &subresource, &stagingImageLayout);

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

        vkUnmapMemory(gDevice, gStagingImageMemory);
        stbi_image_free(pixels);

        createImage(texWidth, texHeight, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            gTextureImage, gTextureImageMemory);

        transitionImageLayout(gStagingImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        transitionImageLayout(gTextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_PREINITIALIZED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        copyImage(gStagingImage, gTextureImage, texWidth, texHeight);

        transitionImageLayout(gTextureImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    // create texture image view
    {
        createImageView(gTextureImage, VK_FORMAT_R8G8B8A8_UNORM, gTextureImageView);
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

        auto result = vkCreateSampler(gDevice, &samplerInfo, nullptr, &gTextureSampler);
        ASSERT_VK_SUCCESS(result);
    }

    // create vertex buffer
    {
        VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

        // staging buffer
        VkBuffer            stagingBuffer;
        VkDeviceMemory      stagingBufferMemory;

        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(gDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
        assert(data);
        memcpy(data, vertices.data(), (size_t)bufferSize);
        vkUnmapMemory(gDevice, stagingBufferMemory);

        // device local buffer
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gVertexBuffer, gVertexBufferMemory);

        // copy vertex buffer from host visible to device local
        copyBuffer(stagingBuffer, gVertexBuffer, bufferSize);
    }

    {
        // create index buffer
        VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

        // staging buffer
        VkBuffer            stagingBuffer;
        VkDeviceMemory      stagingBufferMemory;

        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

        void* data;
        vkMapMemory(gDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
        assert(data);
        memcpy(data, indices.data(), (size_t)bufferSize);
        vkUnmapMemory(gDevice, stagingBufferMemory);

        // device local buffer
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gIndexBuffer, gIndexBufferMemory);

        // copy index buffer from host visible to device local
        copyBuffer(stagingBuffer, gIndexBuffer, bufferSize);
    }

    // create uniform buffer
    {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, gUniformStagingBuffer, gUniformStagingBufferMemory);
        createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, gUniformBuffer, gUniformBufferMemory);
    }

    // create descriptor pool
    std::array<VkDescriptorPoolSize, 2> poolSizes = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    result = vkCreateDescriptorPool(gDevice, &poolInfo, nullptr, &gDescriptorPool);
    assert(result == VK_SUCCESS);

    // create descriptor set
    VkDescriptorSetLayout uboLayouts[] = { gDescriptorSetLayout };
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = gDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = uboLayouts;

    result = vkAllocateDescriptorSets(gDevice, &allocInfo, &gDescriptorSet);
    assert(result == VK_SUCCESS);

    VkDescriptorBufferInfo bufferInfo = {};
    bufferInfo.buffer = gUniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    VkDescriptorImageInfo imageInfo = {};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = gTextureImageView;
    imageInfo.sampler = gTextureSampler;

    std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = gDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = gDescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(gDevice, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);


    // create graphics pipeline
    VkDescriptorSetLayout setLayouts[] = { gDescriptorSetLayout };
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.pNext = nullptr;
    pipelineLayoutCreateInfo.setLayoutCount = 1;
    pipelineLayoutCreateInfo.pSetLayouts = setLayouts;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
    pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

    result = vkCreatePipelineLayout(gDevice, &pipelineLayoutCreateInfo, nullptr, &gPLayout);
    assert(result == VK_SUCCESS);

    VkPipelineDynamicStateCreateInfo dynamicStateInfo;
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dynamicStateInfo.pNext = nullptr;
    dynamicStateInfo.dynamicStateCount = 0;
    dynamicStateInfo.pDynamicStates = nullptr;

    AAsset* vs = AAssetManager_open(assetManager, "vert.spv", AASSET_MODE_UNKNOWN);
    assert(vs);
    size = AAsset_getLength(vs);
    std::vector<uint8_t> vsData(size);
    AAsset_read(vs, vsData.data(), size);
    AAsset_close(vs);

    AAsset* fs = AAssetManager_open(assetManager, "frag.spv", AASSET_MODE_UNKNOWN);
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
        gDevice, &shaderModuleCreateInfo, nullptr, &vertexShader);
    assert(result == VK_SUCCESS);

    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.pNext = nullptr;
    shaderModuleCreateInfo.codeSize = fsData.size();
    shaderModuleCreateInfo.pCode = (const uint32_t*)(fsData.data());
    shaderModuleCreateInfo.flags = 0;

    result = vkCreateShaderModule(
        gDevice, &shaderModuleCreateInfo, nullptr, &fragmentShader);
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

    VkViewport viewports;
    viewports.minDepth = 0.0f;
    viewports.maxDepth = 1.0f;
    viewports.x = 0;
    viewports.y = 0;
    viewports.width = (float)gDisplaySize.width;
    viewports.height = (float)gDisplaySize.height;

    VkRect2D scissor;
    scissor.extent = gDisplaySize;
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
    attachmentStates.colorWriteMask =   VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
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

    VkVertexInputBindingDescription vertexInputBindings;
    vertexInputBindings.binding = 0;
    vertexInputBindings.stride = 3 * sizeof(float);
    vertexInputBindings.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription vertexInputAttributes[1];
    vertexInputAttributes[0].binding = 0;
    vertexInputAttributes[0].location = 0;
    vertexInputAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    vertexInputAttributes[0].offset = 0;

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

    result = vkCreatePipelineCache(gDevice, &pipelineCacheInfo, nullptr, &gPCache);
    assert(result == VK_SUCCESS);

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
    pipelineCreateInfo.pDepthStencilState = nullptr;
    pipelineCreateInfo.pColorBlendState = &colorBlendInfo;
    pipelineCreateInfo.pDynamicState = &dynamicStateInfo;
    pipelineCreateInfo.layout = gPLayout;
    pipelineCreateInfo.renderPass = gRenderPass;
    pipelineCreateInfo.subpass = 0;
    pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineCreateInfo.basePipelineIndex = 0;

    result = vkCreateGraphicsPipelines(gDevice, gPCache, 1, &pipelineCreateInfo, nullptr, &gPipeline);
    assert(result == VK_SUCCESS);

    // create command buffer
    gCmdBufferLen = gSwapchainLength;
    gCmdBuffer.resize(gSwapchainLength);

    for (uint32_t bufferIndex = 0; bufferIndex < gSwapchainLength; bufferIndex++)
    {
        VkCommandBufferAllocateInfo cmdBufferAllocationInfo;
        cmdBufferAllocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufferAllocationInfo.pNext = nullptr;
        cmdBufferAllocationInfo.commandPool = gCmdPool;
        cmdBufferAllocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufferAllocationInfo.commandBufferCount = 1;

        result = vkAllocateCommandBuffers(gDevice, &cmdBufferAllocationInfo, &gCmdBuffer[bufferIndex]);
        assert(result == VK_SUCCESS);


        VkCommandBufferBeginInfo cmdBufferBeginInfo;
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferBeginInfo.pNext = nullptr;
        cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        cmdBufferBeginInfo.pInheritanceInfo = nullptr;

        result = vkBeginCommandBuffer(gCmdBuffer[bufferIndex], &cmdBufferBeginInfo);
        assert(result == VK_SUCCESS);

        VkClearValue clearVals;
        clearVals.color.float32[0] = 0.0f;
        clearVals.color.float32[1] = 0.34f;
        clearVals.color.float32[2] = 0.90f;
        clearVals.color.float32[3] = 1.0f;

        VkRenderPassBeginInfo renderPassBeginInfo;
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.pNext = nullptr;
        renderPassBeginInfo.renderPass = gRenderPass;
        renderPassBeginInfo.framebuffer = gFramebuffers[bufferIndex];
        renderPassBeginInfo.renderArea.offset.x = 0;
        renderPassBeginInfo.renderArea.offset.y = 0;
        renderPassBeginInfo.renderArea.extent = gDisplaySize;
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearVals;

        vkCmdBeginRenderPass(gCmdBuffer[bufferIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(gCmdBuffer[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
            gPipeline);

        VkBuffer vertexBuffers[] = { gVertexBuffer };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(gCmdBuffer[bufferIndex], 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(gCmdBuffer[bufferIndex], gIndexBuffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(gCmdBuffer[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, gPLayout, 0, 1, &gDescriptorSet, 0, nullptr);

        vkCmdDrawIndexed(gCmdBuffer[bufferIndex], indices.size(), 1, 0, 0, 0);

        vkCmdEndRenderPass(gCmdBuffer[bufferIndex]);
        result = vkEndCommandBuffer(gCmdBuffer[bufferIndex]);
        assert(result == VK_SUCCESS);
    }

    VkSemaphoreCreateInfo semaphoreCreateInfo;
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCreateInfo.pNext = nullptr;
    semaphoreCreateInfo.flags = 0;

    result = vkCreateSemaphore(gDevice, &semaphoreCreateInfo, nullptr, &gimageAvailableSemaphore);
    assert(result == VK_SUCCESS);

    result = vkCreateSemaphore(gDevice, &semaphoreCreateInfo, nullptr, &grenderFinishedSemaphore);
    assert(result == VK_SUCCESS);

    init = true;

    return 0;
}


/**
 * Just the current frame in the display.
 */
static void engine_draw_frame(struct engine* engine) {
    
    if (!init)
        return;

    VkResult result;

    uint32_t nextIndex;
    result = vkAcquireNextImageKHR(gDevice, gSwapchain, 0xFFFFFFFFFFFFFFFFull, gimageAvailableSemaphore, VK_NULL_HANDLE, &nextIndex);
    assert(result == VK_SUCCESS);

    VkSemaphore signalSemaphores[] = { grenderFinishedSemaphore };
    VkSemaphore waitSemaphores[] = { gimageAvailableSemaphore };

    VkSubmitInfo submitInfo;
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &gCmdBuffer[nextIndex];
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    result = vkQueueSubmit(gQueue, 1, &submitInfo, VK_NULL_HANDLE);
    assert(result == VK_SUCCESS);

    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &gSwapchain;
    presentInfo.pImageIndices = &nextIndex;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.pResults = &result;

    vkQueuePresentKHR(gQueue, &presentInfo);
}

/**
 * Tear down the EGL context currently associated with the display.
 */
static void engine_term_display(struct engine* engine) {
    vkDestroySurfaceKHR(getVkProcInstance, gSurface, nullptr);
    vkDestroyDevice(gDevice, nullptr);
    vkDestroyInstance(getVkProcInstance, nullptr);

    unloadVKLibs();
}

/**
 * Process the next input event.
 */
static int32_t engine_handle_input(struct android_app* app, AInputEvent* event) {
    struct engine* engine = (struct engine*)app->userData;
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        engine->animating = 1;
        engine->state.x = AMotionEvent_getX(event, 0);
        engine->state.y = AMotionEvent_getY(event, 0);
        return 1;
    }
    return 0;
}

/**
 * Process the next main command.
 */
static void engine_handle_cmd(struct android_app* app, int32_t cmd) {
    struct engine* engine = (struct engine*)app->userData;
    switch (cmd) {
        case APP_CMD_SAVE_STATE:
            // The system has asked us to save our current state.  Do so.
            engine->app->savedState = malloc(sizeof(struct saved_state));
            *((struct saved_state*)engine->app->savedState) = engine->state;
            engine->app->savedStateSize = sizeof(struct saved_state);
            break;
        case APP_CMD_INIT_WINDOW:
            // The window is being shown, get it ready.
            if (engine->app->window != NULL) {
                engine_init_display(engine);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            // The window is being hidden or closed, clean it up.
            engine_term_display(engine);
            break;
        case APP_CMD_GAINED_FOCUS:
            // When our app gains focus, we start monitoring the accelerometer.
            if (engine->accelerometerSensor != NULL) {
                ASensorEventQueue_enableSensor(engine->sensorEventQueue,
                        engine->accelerometerSensor);
                // We'd like to get 60 events per second (in us).
                ASensorEventQueue_setEventRate(engine->sensorEventQueue,
                        engine->accelerometerSensor, (1000L/60)*1000);  
            }
            engine->animating = 1;
            break;
        case APP_CMD_LOST_FOCUS:
            // When our app loses focus, we stop monitoring the accelerometer.
            // This is to avoid consuming battery while not being used.
            if (engine->accelerometerSensor != NULL) {
                ASensorEventQueue_disableSensor(engine->sensorEventQueue,
                        engine->accelerometerSensor);
            }
            // Also stop animating.
            engine->animating = 0;
            break;
    }
}

void update()
{
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count() / 1000.0f;

    UniformBufferObject ubo = {};
        //glm::rotate(glm::mat4(), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    auto rotMat = Matrix<float, 3>::RotationZ(time * 90.f / 180.f * 3.1415926);
    ubo.model = mat4::FromRotationMatrix(rotMat);
    //ubo.model = mat4::Identity();
    ubo.view = mat4::Identity();// mat4::LookAt(vec3(2.0f, 2.0f, 2.0f), vec3(0.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f));
    ubo.proj = mat4::Identity();// mat4::Perspective((45.0f) / 180.f * 3.1415926, (float)gDisplaySize.width / (float)gDisplaySize.height, 0.1f, 10.0f);
    //ubo.proj(1, 1) *= -1;

    void* data;
    vkMapMemory(gDevice, gUniformStagingBufferMemory, 0, sizeof(ubo), 0, &data);
    assert(data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(gDevice, gUniformStagingBufferMemory);

    copyBuffer(gUniformStagingBuffer, gUniformBuffer, sizeof(ubo));
}

/**
 * This is the main entry point of a native application that is using
 * android_native_app_glue.  It runs in its own thread, with its own
 * event loop for receiving input events and doing other things.
 */
void android_main(struct android_app* state) {
    
    struct engine engine;

    // Make sure glue isn't stripped.
    app_dummy();

    memset(&engine, 0, sizeof(engine));
    state->userData = &engine;
    state->onAppCmd = engine_handle_cmd;
    state->onInputEvent = engine_handle_input;
    engine.app = state;

    // Prepare to monitor accelerometer
    engine.sensorManager = ASensorManager_getInstance();
    engine.accelerometerSensor = ASensorManager_getDefaultSensor(engine.sensorManager,
        ASENSOR_TYPE_ACCELEROMETER);
    engine.sensorEventQueue = ASensorManager_createEventQueue(engine.sensorManager,
        state->looper, LOOPER_ID_USER, NULL, NULL);

    if (state->savedState != NULL) {
        // We are starting with a previous saved state; restore from it.
        engine.state = *(struct saved_state*)state->savedState;
    }

    // loop waiting for stuff to do.

    while (1) {
        // Read all pending events.
        int ident;
        int events;
        struct android_poll_source* source;

        // If not animating, we will block forever waiting for events.
        // If animating, we loop until all events are read, then continue
        // to draw the next frame of animation.
        while ((ident=ALooper_pollAll(engine.animating ? 0 : -1, NULL, &events,
                (void**)&source)) >= 0) {

            // Process this event.
            if (source != NULL) {
                source->process(state, source);
            }

            // If a sensor has data, process it now.
            if (ident == LOOPER_ID_USER) {
                if (engine.accelerometerSensor != NULL) {
                    ASensorEvent event;
                    while (ASensorEventQueue_getEvents(engine.sensorEventQueue,
                            &event, 1) > 0) {
                        if (LOG_ACCELEROMETER) {
                            LOGI("accelerometer: x=%f y=%f z=%f",
                                event.acceleration.x, event.acceleration.y,
                                event.acceleration.z);
                        }
                    }
                }
            }

            // Check if we are exiting.
            if (state->destroyRequested != 0) {
                engine_term_display(&engine);
                return;
            }
        }

        if (engine.animating) 
        {
            // Done with events; draw next animation frame.
            engine.state.angle += .01f;
            if (engine.state.angle > 1) {
                engine.state.angle = 0;
            }

            update();

            // Drawing is throttled to the screen update rate, so there
            // is no need to do timing here.
            engine_draw_frame(&engine);
        }
    }
}
//END_INCLUDE(all)
