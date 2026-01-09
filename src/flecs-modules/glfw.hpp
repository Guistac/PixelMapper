#pragma once

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <flecs.h>

struct glfw {
    // Tag set when builder system fail
    struct Invalid {};

    // Window Archetype to create a window set:
    // - ScreenSize
    // - ScreenCoord
    // - optional WindowHintList
    // The window builder system will create the window and add WindowHandle and FramebufferSize
    // window title is set from entity name
    
    struct ScreenCoord { int x, y; };
    struct ScreenSize { int width, height; };
    // Hints see https://www.glfw.org/docs/latest/window_guide.html#window_hints
    // FIXME: support for string hint
    struct WindowHint { int name; int value; };
    
    struct WindowHintList {
        const WindowHint* hints; // null terminated array of hints
    };

    struct WindowHandle { GLFWwindow *handle; };
    struct FramebufferSize { int width, height; };

    glfw(flecs::world& w);
    ~glfw();
    //static flecs::entity createWindow(flecs::world& w, int width, int height, const char *title);

    // FIXME? Store the window builder system as we want to run it before the main loop
    // because we need a window to initialize imgui
    // and openGL maybe is not the case for webgpu or other modern gfx api ?
    static void buildWindow(flecs::entity e, const ScreenSize& size, const ScreenCoord& coord, const WindowHintList* hints);
};

