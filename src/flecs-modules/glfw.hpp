#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <flecs.h>
        
struct ScreenCoord { int x, y; };
struct ScreenSize { int width, height; };
struct FramebufferSize { int width, height; };

struct WindowHandle { GLFWwindow *handle; };

struct glfw {
    glfw(flecs::world& w);
    static flecs::entity createWindow(flecs::world& w, int width, int height, const char *title);
};

