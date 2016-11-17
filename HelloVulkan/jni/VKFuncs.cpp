#include <dlfcn.h>
#include <assert.h>

#define VK_NO_PROTOTYPES
#include "nosdk/vulkan/vulkan.h"
#undef VK_NO_PROTOTYPES

#define VK_IMPORT(name) PFN_##name name;
#include "VKImport.h"

static void* gVulkan_so = nullptr;
static void* gVulkanValidation_so = nullptr;

#define VK_IMPORT(name) \
{ \
    name = reinterpret_cast<PFN_##name>(dlsym(gVulkan_so, #name)); \
    assert(name != 0); \
}

void loadVKLibs()
{
    gVulkan_so = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    gVulkanValidation_so = dlopen("libVkLayer_core_validation.so", RTLD_NOW | RTLD_LOCAL);
}

void loadVKFuncs()
{
#include "VKImport.h"
}

void* loadFuncFromValidationLib(const char * name)
{
    return dlsym(gVulkanValidation_so, name);
}

void unloadVKLibs()
{
    if (gVulkan_so) dlclose(gVulkan_so);
    if (gVulkanValidation_so) dlclose(gVulkanValidation_so);
}
