#if _ANDROID

#include <dlfcn.h>
#include <assert.h>
#include "VKFuncs.h"

static void* gVulkanValidation_so = nullptr;

void loadVKLibs()
{
    gVulkanValidation_so = dlopen("libVkLayer_core_validation.so", RTLD_NOW | RTLD_LOCAL);
}

void* loadFuncFromValidationLib(const char * name)
{
    void* ret = nullptr;
    if (gVulkanValidation_so)
    {
        ret = dlsym(gVulkanValidation_so, name);
    }
    return ret;
}

void unloadVKLibs()
{
    if (gVulkanValidation_so)
    {
        dlclose(gVulkanValidation_so);
        gVulkanValidation_so = nullptr;
    }
}
#endif