#pragma once

#define VK_NO_PROTOTYPES
#include "nosdk/vulkan/vulkan.h"
#undef VK_NO_PROTOTYPES

#define VK_IMPORT(name) extern PFN_##name name;
#include "VKImport.h"

void loadVKLibs();
void loadVKFuncs();

void* loadFuncFromValidationLib(const char* name);

void unloadVKLibs();