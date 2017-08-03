#pragma once

#define ASSERT_VK_SUCCESS(result) assert(result == VK_SUCCESS)

#ifdef _ANDROID
#define VK_NO_PROTOTYPES
#include "nosdk/vulkan/vulkan.h"
#undef VK_NO_PROTOTYPES

#define VK_IMPORT(name) extern PFN_##name name;
#include "VKImport.h"

void loadVKLibs();
void loadVKFuncs();

void* loadFuncFromValidationLib(const char* name);

void unloadVKLibs();
#else
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

inline void loadVKLibs() {}
inline void loadVKFuncs() {}
inline void* loadFuncFromValidationLib(const char* /*name*/) { return nullptr; }
inline void unloadVKLibs() {}
#endif