#pragma once

#define ASSERT_VK_SUCCESS(result) assert(result == VK_SUCCESS)

#ifdef _ANDROID
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>

void loadVKLibs();
void* loadFuncFromValidationLib(const char* name);
void unloadVKLibs();
#else
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

inline void loadVKLibs() {}
inline void* loadFuncFromValidationLib(const char* /*name*/) { return nullptr; }
inline void unloadVKLibs() {}
#endif