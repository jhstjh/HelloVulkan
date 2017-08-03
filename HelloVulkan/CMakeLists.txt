cmake_minimum_required(VERSION 3.9)

#--------------------------------------------------
# macro to group files by directory for VS projects
#--------------------------------------------------
macro(source_group_by_dir source_files)
    set(sgbd_cur_dir ${CMAKE_CURRENT_SOURCE_DIR})
    foreach(sgbd_file ${${source_files}})
        string(REGEX REPLACE ${sgbd_cur_dir}/\(.*\) \\1 sgbd_fpath ${sgbd_file})
        string(REGEX REPLACE "\(.*\)/.*" \\1 sgbd_group_name ${sgbd_fpath})
        string(COMPARE EQUAL ${sgbd_fpath} ${sgbd_group_name} sgbd_nogroup)
        string(REPLACE "/" "\\" sgbd_group_name ${sgbd_group_name})
        if(sgbd_nogroup)
            set(sgbd_group_name "\\")
        endif(sgbd_nogroup)
        source_group(${sgbd_group_name} FILES ${sgbd_file})
    endforeach(sgbd_file)
endmacro()


#--------------------------------------------------
# build targets
#--------------------------------------------------
set(CMAKE_SYSTEM_NAME Android)
set(CMAKE_ANDROID_ARCH arm64-v8a)
set(CMAKE_ANDROID_API_MIN 23)
set(CMAKE_ANDROID_API 24)
set(CMAKE_ANDROID_GUI 1)

set(HV_ANDROID 1)

#--------------------------------------------------
# build configurations, global to all projects
#--------------------------------------------------
set(CMAKE_CONFIGURATION_TYPES debug release)

# used by target_link_libraries to correctly link in debug / optimized libraries
set_property(GLOBAL PROPERTY DEBUG_CONFIGURATIONS debug)

# enable projects to be grouped by folder in outputted solutions
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# add local modules path
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules)


if(HV_MSVC)
    set(CMAKE_CPP_FLAGS_INIT "/DWIN32 /D_WINDOWS /Wv:18 /W4 /bigobj")

    set(CMAKE_CPP_FLAGS_DEBUG_INIT "${CMAKE_CPP_FLAGS_INIT} /Od /MTd /Zi /D_ITERATOR_DEBUG_LEVEL=0 /D_DEBUG=1")
    set(CMAKE_CPP_FLAGS_RELEASE_INIT "${CMAKE_CPP_FLAGS_INIT} /MT /O2 /Ob2 /Zi /D_RELEASE=1")

    set(CMAKE_C_FLAGS_INIT "/wd4131")
    set(CMAKE_C_FLAGS_DEBUG_INIT "${CMAKE_CPP_FLAGS_DEBUG_INIT} ${CMAKE_C_FLAGS_INIT}")
    set(CMAKE_C_FLAGS_RELEASE_INIT "${CMAKE_CPP_FLAGS_RELEASE_INIT} ${CMAKE_C_FLAGS_INIT}")

    set(CMAKE_CXX_FLAGS_INIT "/EHsc")
    set(CMAKE_CXX_FLAGS_DEBUG_INIT "${CMAKE_CPP_FLAGS_DEBUG_INIT} ${CMAKE_CXX_FLAGS_INIT}")
    set(CMAKE_CXX_FLAGS_RELEASE_INIT "${CMAKE_CPP_FLAGS_RELEASE_INIT} ${CMAKE_CXX_FLAGS_INIT}")

    set(CMAKE_EXE_LINKER_FLAGS_INIT "/DEBUG /INCREMENTAL:NO")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT}")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "${CMAKE_EXE_LINKER_FLAGS_INIT}")

    set(CMAKE_SHARED_LINKER_FLAGS_INIT "/DEBUG /INCREMENTAL:NO")
    set(CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT "${CMAKE_SHARED_LINKER_FLAGS_INIT}")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE_INIT "${CMAKE_SHARED_LINKER_FLAGS_INIT}")

    set(CMAKE_MODULE_LINKER_FLAGS_INIT "/DEBUG /INCREMENTAL:NO")
    set(CMAKE_MODULE_LINKER_FLAGS_DEBUG_INIT "${CMAKE_MODULE_LINKER_FLAGS_INIT}")
    set(CMAKE_MODULE_LINKER_FLAGS_RELEASE_INIT "${CMAKE_MODULE_LINKER_FLAGS_INIT}")
else()
    set(CMAKE_CPP_FLAGS_INIT "")
    set(CMAKE_CPP_FLAGS_DEBUG_INIT "${CMAKE_CPP_FLAGS_INIT} -g -D_DEBUG=1")
    set(CMAKE_CPP_FLAGS_RELEASE_INIT "${CMAKE_CPP_FLAGS_INIT} -O3 -fvisibility=hidden -DNDEBUG -D_RELEASE=1")

    set(CMAKE_C_FLAGS_INIT "")
    set(CMAKE_C_FLAGS_DEBUG_INIT "${CMAKE_CPP_FLAGS_DEBUG_INIT} ${CMAKE_C_FLAGS_INIT}")
    set(CMAKE_C_FLAGS_RELEASE_INIT "${CMAKE_CPP_FLAGS_RELEASE_INIT} ${CMAKE_C_FLAGS_INIT}")

    set(CMAKE_CXX_FLAGS_INIT "-std=gnu++11")
    set(CMAKE_CXX_FLAGS_DEBUG_INIT "${CMAKE_CPP_FLAGS_DEBUG_INIT} ${CMAKE_CXX_FLAGS_INIT}")
    set(CMAKE_CXX_FLAGS_RELEASE_INIT "${CMAKE_CPP_FLAGS_RELEASE_INIT} ${CMAKE_CXX_FLAGS_INIT}")
endif()

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS_INIT}" CACHE STRING "Flags used by the compiler during all build types." FORCE)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG_INIT}" CACHE STRING "Flags used by the compiler during debug builds." FORCE)
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE_INIT}" CACHE STRING "Flags used by the compiler during release builds." FORCE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_INIT}" CACHE STRING "Flags used by the compiler during all build types." FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG_INIT}" CACHE STRING "Flags used by the compiler during debug builds." FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE_INIT}" CACHE STRING "Flags used by the compiler during release builds." FORCE)

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS_INIT}" CACHE STRING "Flags used by the linker." FORCE)
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG_INIT}" CACHE STRING "Flags used by the linker during debug builds." FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT}" CACHE STRING "Flags used by the linker during release builds." FORCE)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS_INIT}" CACHE STRING "Flags used by the linker during the creation of dll's." FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG_INIT}" CACHE STRING "Flags used by the linker during debug builds." FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE_INIT}" CACHE STRING "Flags used by the linker during release builds." FORCE)

set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS_INIT}" CACHE STRING "Flags used by the linker during the creation of modules." FORCE)
set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "${CMAKE_MODULE_LINKER_FLAGS_DEBUG_INIT}" CACHE STRING "Flags used by the linker during debug builds." FORCE)
set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE_INIT}" CACHE STRING "Flags used by the linker during release builds." FORCE)

set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS_INIT}" CACHE STRING "Flags used by the linker during the creation of static libraries." FORCE)
set(CMAKE_STATIC_LINKER_FLAGS_DEBUG "${CMAKE_STATIC_LINKER_FLAGS_DEBUG_INIT}" CACHE STRING "Flags used by the linker during debug builds." FORCE)
set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE_INIT}" CACHE STRING "Flags used by the linker during release builds." FORCE)

# note, this must happen AFTER all of the build configurations have been defined
project(HelloVulkan LANGUAGES C CXX)

if(HV_MSVC)
    list(APPEND HV_DEFS _CRT_SECURE_NO_WARNINGS NOMINMAX)
    list(APPEND HV_FLAGS /wd4221 /wd4351 /WX)
else()
    list(APPEND HV_DEFS __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS)
    list(APPEND HV_FLAGS -march=armv8-a+crc -fno-operator-names)
endif()

if(HV_ANDROID)
    list(APPEND HV_DEFS _ANDROID DISABLE_IMPORTGL VK_USE_PLATFORM_ANDROID_KHR)
    list(APPEND HV_LIBS android EGL GLESv1_CM)
	list(APPEND CMAKE_ANDROID_NATIVE_LIB_DEPENDENCIES VkLayer_core_validation.so VkLayer_image.so VkLayer_object_tracker.so VkLayer_parameter_validation.so VkLayer_swapchain.so VkLayer_threading.so VkLayer_unique_objects.so)
	set(CMAKE_ANDROID_NATIVE_LIB_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/lib/android/")
	set(CMAKE_ANDROID_ASSETS_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()

#--------------------------------------------------
# Grab source files
#--------------------------------------------------
file(GLOB HV_SOURCE
	src/*.cpp
	include/*.h
	include/ext/*.h
	include/ext/mathfu/*.h)
	
if (HV_ANDROID)
	file(GLOB HV_PLATFORM_SOURCES 
		include/nosdk/vulkan/*.h
		native_app_glue/*.c
		native_app_glue/*.h
	)
endif()

list(APPEND HV_SOURCE ${HV_PLATFORM_SOURCES})
source_group_by_dir(HV_SOURCE)

add_executable(HelloVulkan ${HV_SOURCE})

set(HV_INCLUDE_DIRS
	include
	include/ext)
target_include_directories(HelloVulkan PUBLIC ${HV_INCLUDE_DIRS})
target_compile_definitions(HelloVulkan PRIVATE ${HV_DEFS})
target_compile_options(HelloVulkan PRIVATE ${HV_FLAGS})
set_property(TARGET HelloVulkan PROPERTY CXX_STANDARD 14)
target_link_libraries(HelloVulkan ${HV_LIBS})
add_custom_command(TARGET HelloVulkan
    PRE_BUILD
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets
    COMMAND cmd /c ${CMAKE_CURRENT_SOURCE_DIR}/assets/compileShaders.cmd)