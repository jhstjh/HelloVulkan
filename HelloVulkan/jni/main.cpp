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

#include "nosdk/vulkan/vulkan.h"
#include <dlfcn.h>
#include <assert.h>
#include <vector>
#include <cstdint>

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>


#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "native-activity", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "native-activity", __VA_ARGS__))

#define LOG_ACCELEROMETER false


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
uint32_t            gCmdBufferLen;
VkSemaphore         gSemaphore;
VkFence             gFence;

VkPipelineLayout  gPLayout;
VkPipelineCache   gPCache;
VkPipeline        gPipeline;

bool init = false;

void* NvAndroidGetVKProcAddr(const char* name) {
    return (void*)android_getVkProc(getVkProcInstance, name);
}

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

/**
 * Initialize an EGL context for the current display.
 */
static int engine_init_display(struct engine* engine) {
    VkFormat m_cbFormat = VK_FORMAT_UNDEFINED;
    VkFormat m_dsFormat = VK_FORMAT_UNDEFINED;

    VkResult result = VK_ERROR_INITIALIZATION_FAILED;


    PFN_vkGetProcAddressNV driver_getProc = NULL;

    vulkan_so = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    assert(vulkan_so);

    // load so for validation layers
    auto so0 = dlopen("libVkLayer_core_validation.so", RTLD_NOW | RTLD_LOCAL);
    auto so1 = dlopen("libVkLayer_image.so", RTLD_NOW | RTLD_LOCAL);
    auto so2 = dlopen("libVkLayer_object_tracker.so", RTLD_NOW | RTLD_LOCAL);
    auto so3 = dlopen("libVkLayer_parameter_validation.so", RTLD_NOW | RTLD_LOCAL);
    auto so4 = dlopen("libVkLayer_swapchain.so", RTLD_NOW | RTLD_LOCAL);
    auto so5 = dlopen("libVkLayer_threading.so", RTLD_NOW | RTLD_LOCAL);
    auto so6 = dlopen("libVkLayer_unique_objects.so", RTLD_NOW | RTLD_LOCAL);

    android_getVkProc = (PFN_vkGetInstanceProcAddr)dlsym(vulkan_so, "vkGetInstanceProcAddr");
    assert(android_getVkProc);

    driver_getProc = (PFN_vkGetProcAddressNV)NvAndroidGetVKProcAddr;


    // Init instance
    PFN_vkEnumerateInstanceExtensionProperties
        vkEnumerateInstanceExtensionProperties =
        reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
            dlsym(vulkan_so, "vkEnumerateInstanceExtensionProperties"));
    PFN_vkEnumerateInstanceLayerProperties vkEnumerateInstanceLayerProperties =
        reinterpret_cast<PFN_vkEnumerateInstanceLayerProperties>(
            dlsym(vulkan_so, "vkEnumerateInstanceLayerProperties"));
    PFN_vkCreateInstance vkCreateInstance =
        reinterpret_cast<PFN_vkCreateInstance>(
            dlsym(vulkan_so, "vkCreateInstance"));
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            dlsym(vulkan_so, "vkGetInstanceProcAddr"));

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
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices =
        reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
            dlsym(vulkan_so, "vkEnumeratePhysicalDevices"));

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
            dlsym(so0, "vkCreateDebugReportCallbackEXT"));

    VkDebugReportCallbackEXT debugReportCallback;
    result = vkCreateDebugReportCallbackEXT(getVkProcInstance, &debugReportCallbackCreateInfo, nullptr, &debugReportCallback);
    assert(result == VK_SUCCESS);

    // physical device layers and extensions
    PFN_vkEnumerateDeviceLayerProperties vkEnumerateDeviceLayerProperties =
        reinterpret_cast<PFN_vkEnumerateDeviceLayerProperties>(
            dlsym(vulkan_so, "vkEnumerateDeviceLayerProperties"));

    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties =
        reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
            dlsym(vulkan_so, "vkEnumerateDeviceExtensionProperties"));

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

    PFN_vkCreateAndroidSurfaceKHR vkCreateAndroidSurfaceKHR =
        reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
            dlsym(vulkan_so, "vkCreateAndroidSurfaceKHR"));

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

    PFN_vkCreateDevice vkCreateDevice =
        reinterpret_cast<PFN_vkCreateDevice>(
            dlsym(vulkan_so, "vkCreateDevice"));

    result = vkCreateDevice(gPhysicalDevice, &deviceCreateInfo, nullptr, &gDevice);
    assert(result == VK_SUCCESS);

    PFN_vkGetDeviceQueue vkGetDeviceQueue =
        reinterpret_cast<PFN_vkGetDeviceQueue>(
            dlsym(vulkan_so, "vkGetDeviceQueue"));

    vkGetDeviceQueue(gDevice, 0, 0, &gQueue);

    // create swap chain
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
            dlsym(vulkan_so, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));

    VkSurfaceCapabilitiesKHR surfaceCapabilities;
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gPhysicalDevice, gSurface,
        &surfaceCapabilities);
    assert(result == VK_SUCCESS);

    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR =
        reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
            dlsym(vulkan_so, "vkGetPhysicalDeviceSurfaceFormatsKHR"));

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

    uint32_t queueFamily = 0;

    VkSwapchainCreateInfoKHR swapchainCreateInfo;
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
    swapchainCreateInfo.queueFamilyIndexCount = 1;
    swapchainCreateInfo.pQueueFamilyIndices = &queueFamily;
    swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE;
    swapchainCreateInfo.clipped = VK_FALSE;

    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR =
        reinterpret_cast<PFN_vkCreateSwapchainKHR>(
            dlsym(vulkan_so, "vkCreateSwapchainKHR"));

    result = vkCreateSwapchainKHR(gDevice, &swapchainCreateInfo,
        nullptr, &gSwapchain);
    assert(result == VK_SUCCESS);

    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR =
        reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
            dlsym(vulkan_so, "vkGetSwapchainImagesKHR"));

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
    attachmentDescriptions.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachmentDescriptions.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

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

    VkRenderPassCreateInfo renderPassCreateInfo;
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.pNext = nullptr;
    renderPassCreateInfo.attachmentCount = 1;
    renderPassCreateInfo.pAttachments = &attachmentDescriptions;
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpassDescription;
    renderPassCreateInfo.dependencyCount = 0;
    renderPassCreateInfo.pDependencies = nullptr;

    PFN_vkCreateRenderPass vkCreateRenderPass =
        reinterpret_cast<PFN_vkCreateRenderPass>(
            dlsym(vulkan_so, "vkCreateRenderPass"));

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

    PFN_vkCreateImageView vkCreateImageView =
        reinterpret_cast<PFN_vkCreateImageView>(
            dlsym(vulkan_so, "vkCreateImageView"));

    gDisplayViews.resize(SwapchainImagesCount);
    for (uint32_t i = 0; i < SwapchainImagesCount; i++)
    {
        VkImageViewCreateInfo viewCreateInfo;
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.pNext = nullptr;
        viewCreateInfo.image = displayImages[i];
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCreateInfo.format = gDisplayFormat;
        viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCreateInfo.subresourceRange.baseMipLevel = 0;
        viewCreateInfo.subresourceRange.levelCount = 1;
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = 1;
        viewCreateInfo.flags = 0;

        result = vkCreateImageView(gDevice, &viewCreateInfo, nullptr, &gDisplayViews[i]);
        assert(result == VK_SUCCESS);
    }

    // no depth buffer for now
    VkImageView depthView = VK_NULL_HANDLE;

    PFN_vkCreateFramebuffer vkCreateFramebuffer =
        reinterpret_cast<PFN_vkCreateFramebuffer>(
            dlsym(vulkan_so, "vkCreateFramebuffer"));

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

    // create graphics pipeline
    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo;
    pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.pNext = nullptr;
    pipelineLayoutCreateInfo.setLayoutCount = 0;
    pipelineLayoutCreateInfo.pSetLayouts = nullptr;
    pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
    pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

    PFN_vkCreatePipelineLayout vkCreatePipelineLayout =
        reinterpret_cast<PFN_vkCreatePipelineLayout>(
            dlsym(vulkan_so, "vkCreatePipelineLayout"));

    result = vkCreatePipelineLayout(gDevice, &pipelineLayoutCreateInfo, nullptr, &gPLayout);
    assert(result == VK_SUCCESS);

    VkPipelineDynamicStateCreateInfo dynamicStateInfo;
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dynamicStateInfo.pNext = nullptr;
    dynamicStateInfo.dynamicStateCount = 0;
    dynamicStateInfo.pDynamicStates = nullptr;

    AAssetManager* assetManager = engine->app->activity->assetManager;
    AAsset* vs = AAssetManager_open(assetManager, "vert.spv", AASSET_MODE_UNKNOWN);
    assert(vs);
    auto size = AAsset_getLength(vs);
    std::vector<uint8_t> vsData(size);
    AAsset_read(vs, vsData.data(), size);
    AAsset_close(vs);

    AAsset* fs = AAssetManager_open(assetManager, "frag.spv", AASSET_MODE_UNKNOWN);
    assert(fs);
    size = AAsset_getLength(fs);
    std::vector<uint8_t> fsData(size);
    AAsset_read(fs, fsData.data(), size);
    AAsset_close(fs);

#if 0
    static const char vs[] =
        "#version 450 core\
        layout(location = 0) in vec2 aVertex;\
        \
        \
        void main()\
        {\
            gl_Position = vec4(aVertex, 0, 1);\
        }"
    ;

    static const char fs[] =
        "#version 450 core                       \
        #extension GL_KHR_vulkan_glsl : enable   \
        layout(location = 0) out vec4 oFrag;     \
                                                 \
        void main()                              \
        {                                        \
            oFrag = vColor;                      \
        }"
    ;
#endif

    VkShaderModule vertexShader, fragmentShader;

    VkShaderModuleCreateInfo shaderModuleCreateInfo;
    shaderModuleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderModuleCreateInfo.pNext = nullptr;
    shaderModuleCreateInfo.codeSize = vsData.size();
    shaderModuleCreateInfo.pCode = (const uint32_t*)(vsData.data());
    shaderModuleCreateInfo.flags = 0;

    PFN_vkCreateShaderModule vkCreateShaderModule =
        reinterpret_cast<PFN_vkCreateShaderModule>(
            dlsym(vulkan_so, "vkCreateShaderModule"));

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
    rasterInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
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

    // VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    // vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    // vertexInputInfo.pNext = nullptr;
    // vertexInputInfo.vertexBindingDescriptionCount = 1;
    // vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindings;
    // vertexInputInfo.vertexAttributeDescriptionCount = 1;
    // vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributes;

    VkPipelineVertexInputStateCreateInfo vertexInputInfo;
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.pNext = nullptr;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.pVertexBindingDescriptions = nullptr;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;
    vertexInputInfo.pVertexAttributeDescriptions = nullptr;

    VkPipelineCacheCreateInfo pipelineCacheInfo;
    pipelineCacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    pipelineCacheInfo.pNext = nullptr;
    pipelineCacheInfo.initialDataSize = 0;
    pipelineCacheInfo.pInitialData = nullptr;
    pipelineCacheInfo.flags = 0;


    PFN_vkCreatePipelineCache vkCreatePipelineCache =
        reinterpret_cast<PFN_vkCreatePipelineCache>(
            dlsym(vulkan_so, "vkCreatePipelineCache"));

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

    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines =
        reinterpret_cast<PFN_vkCreateGraphicsPipelines>(
            dlsym(vulkan_so, "vkCreateGraphicsPipelines"));

    result = vkCreateGraphicsPipelines(gDevice, gPCache, 1, &pipelineCreateInfo, nullptr, &gPipeline);
    assert(result == VK_SUCCESS);



    // create command pool
    VkCommandPoolCreateInfo cmdPoolCreateInfo;
    cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolCreateInfo.pNext = nullptr;
    cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdPoolCreateInfo.queueFamilyIndex = 0;

    PFN_vkCreateCommandPool vkCreateCommandPool =
        reinterpret_cast<PFN_vkCreateCommandPool>(
            dlsym(vulkan_so, "vkCreateCommandPool"));

    result = vkCreateCommandPool(gDevice, &cmdPoolCreateInfo, nullptr, &gCmdPool);
    assert(result == VK_SUCCESS);

    gCmdBufferLen = gSwapchainLength;
    gCmdBuffer.resize(gSwapchainLength);


    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers =
        reinterpret_cast<PFN_vkAllocateCommandBuffers>(
            dlsym(vulkan_so, "vkAllocateCommandBuffers"));

    PFN_vkBeginCommandBuffer vkBeginCommandBuffer =
        reinterpret_cast<PFN_vkBeginCommandBuffer>(
            dlsym(vulkan_so, "vkBeginCommandBuffer"));

    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass =
        reinterpret_cast<PFN_vkCmdBeginRenderPass>(
            dlsym(vulkan_so, "vkCmdBeginRenderPass"));

    PFN_vkCmdEndRenderPass vkCmdEndRenderPass =
        reinterpret_cast<PFN_vkCmdEndRenderPass>(
            dlsym(vulkan_so, "vkCmdEndRenderPass"));

    PFN_vkEndCommandBuffer vkEndCommandBuffer =
        reinterpret_cast<PFN_vkEndCommandBuffer>(
            dlsym(vulkan_so, "vkEndCommandBuffer"));

    PFN_vkCmdBindPipeline vkCmdBindPipeline =
        reinterpret_cast<PFN_vkCmdBindPipeline>(
            dlsym(vulkan_so, "vkCmdBindPipeline"));

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

        PFN_vkCmdDraw vkCmdDraw =
            reinterpret_cast<PFN_vkCmdDraw>(
                dlsym(vulkan_so, "vkCmdDraw"));

        vkCmdBeginRenderPass(gCmdBuffer[bufferIndex], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(gCmdBuffer[bufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
            gPipeline);

        vkCmdDraw(gCmdBuffer[bufferIndex], 3, 1, 0, 0);

        vkCmdEndRenderPass(gCmdBuffer[bufferIndex]);
        result = vkEndCommandBuffer(gCmdBuffer[bufferIndex]);
        assert(result == VK_SUCCESS);
    }

    VkFenceCreateInfo fenceCreateInfo;
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.pNext = nullptr;
    fenceCreateInfo.flags = 0;

    PFN_vkCreateFence vkCreateFence =
        reinterpret_cast<PFN_vkCreateFence>(
            dlsym(vulkan_so, "vkCreateFence"));

    result = vkCreateFence(gDevice, &fenceCreateInfo, nullptr, &gFence);
    assert(result == VK_SUCCESS);

    VkSemaphoreCreateInfo semaphoreCreateInfo;
    semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreCreateInfo.pNext = nullptr;
    semaphoreCreateInfo.flags = 0;

    PFN_vkCreateSemaphore vkCreateSemaphore =
        reinterpret_cast<PFN_vkCreateSemaphore>(
            dlsym(vulkan_so, "vkCreateSemaphore"));

    result = vkCreateSemaphore(gDevice, &semaphoreCreateInfo, nullptr, &gSemaphore);
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

    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR =
        reinterpret_cast<PFN_vkAcquireNextImageKHR>(
            dlsym(vulkan_so, "vkAcquireNextImageKHR"));

    uint32_t nextIndex;
    result = vkAcquireNextImageKHR(gDevice, gSwapchain, 0xFFFFFFFFFFFFFFFFull, gSemaphore, gFence, &nextIndex);
    assert(result == VK_SUCCESS);

    PFN_vkResetFences vkResetFences =
        reinterpret_cast<PFN_vkResetFences>(
            dlsym(vulkan_so, "vkResetFences"));

    result = vkResetFences(gDevice, 1, &gFence);
    assert(result == VK_SUCCESS);

    VkSubmitInfo submitInfo;
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = nullptr;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &gSemaphore;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &gCmdBuffer[nextIndex];
    submitInfo.signalSemaphoreCount = 0;
    submitInfo.pSignalSemaphores = nullptr;

    PFN_vkQueueSubmit vkQueueSubmit =
        reinterpret_cast<PFN_vkQueueSubmit>(
            dlsym(vulkan_so, "vkQueueSubmit"));

    result = vkQueueSubmit(gQueue, 1, &submitInfo, gFence);
    assert(result == VK_SUCCESS);

    PFN_vkWaitForFences vkWaitForFences =
        reinterpret_cast<PFN_vkWaitForFences>(
            dlsym(vulkan_so, "vkWaitForFences"));

    result = vkWaitForFences(gDevice, 1, &gFence, VK_TRUE, 0xFFFFFFFFFFFFFFFFull);
    assert(result == VK_SUCCESS);

    VkPresentInfoKHR presentInfo;
    presentInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &gSwapchain;
    presentInfo.pImageIndices = &nextIndex;
    presentInfo.waitSemaphoreCount = 0;
    presentInfo.pWaitSemaphores = nullptr;
    presentInfo.pResults = &result;

    PFN_vkQueuePresentKHR vkQueuePresentKHR =
        reinterpret_cast<PFN_vkQueuePresentKHR>(
            dlsym(vulkan_so, "vkQueuePresentKHR"));

    vkQueuePresentKHR(gQueue, &presentInfo);
}

/**
 * Tear down the EGL context currently associated with the display.
 */
static void engine_term_display(struct engine* engine) {
    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR =
        reinterpret_cast<PFN_vkDestroySurfaceKHR>(
            dlsym(vulkan_so, "vkDestroySurfaceKHR"));

    PFN_vkDestroyDevice vkDestroyDevice =
        reinterpret_cast<PFN_vkDestroyDevice>(
            dlsym(vulkan_so, "vkDestroyDevice"));

    PFN_vkDestroyInstance vkDestroyInstance =
        reinterpret_cast<PFN_vkDestroyInstance>(
            dlsym(vulkan_so, "vkDestroyInstance"));

    vkDestroySurfaceKHR(getVkProcInstance, gSurface, nullptr);
    vkDestroyDevice(gDevice, nullptr);
    vkDestroyInstance(getVkProcInstance, nullptr);
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

            // Drawing is throttled to the screen update rate, so there
            // is no need to do timing here.
            engine_draw_frame(&engine);
        }
    }
}
//END_INCLUDE(all)
