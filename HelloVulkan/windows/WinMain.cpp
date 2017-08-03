#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include "VKRenderer.h"

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Hello Vulkan", nullptr, nullptr);

    VKRenderer::create();
    VKRenderer::getInstance().init(window);    

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();
        VKRenderer::getInstance().update();
        VKRenderer::getInstance().draw();
    }

    VKRenderer::getInstance().release();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}