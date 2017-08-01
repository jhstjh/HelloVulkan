#pragma once
#include <EGL/egl.h>
#include <GLES/gl.h>
#include <android/sensor.h>
#include "../native_app_glue/android_native_app_glue.h"
#include <android/log.h>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "native-activity", __VA_ARGS__))
#define LOGW(...) ((void)__android_log_print(ANDROID_LOG_WARN, "native-activity", __VA_ARGS__))

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