#include "glfw.hpp"

static void glfw_error_callback(int error, const char* description){
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

glfw::glfw(flecs::world& w) {
    glfwSetErrorCallback(glfw_error_callback); // This also sets the working directory to .app/Resources on MacOs builds
    if (!glfwInit()) return; // FIXME: how do we handle error in constructor ???
    
    w.component<WindowHandle>();
    w.component<ScreenCoord>()
        .member<int>("x")
        .member<int>("y")
    ;
    w.component<ScreenSize>()
        .member<int>("width")
        .member<int>("height")
    ;
    w.component<FramebufferSize>()
        .member<int>("width")
        .member<int>("height")
    ;

    w.observer<WindowHandle, ScreenCoord>()
        .event(flecs::OnSet)
        .each([](flecs::entity e, const WindowHandle& win, const ScreenCoord& coord){
            glfwSetWindowPos(win.handle, coord.x, coord.y);
        })
    ;
    w.observer<WindowHandle, ScreenSize>()
        .event(flecs::OnSet)
        .each([](flecs::entity e, const WindowHandle& win, const ScreenSize& size){
            glfwSetWindowSize(win.handle, size.width, size.height);
        })
    ;

    //auto queryOwnObservers = w.query_builder<>()
    //    .with(flecs::Observer)
    //    .with<flecs::ChildOf, glfw>()
    //;

    w.system("Poll events")
        .kind(flecs::PreUpdate)
        .each([]() {
            glfwPollEvents();
        })
    ;
    w.system<const WindowHandle, ScreenCoord&, ScreenSize&, FramebufferSize&>("Swap buffers")
        .kind(flecs::OnStore)
        .each([](const WindowHandle win, ScreenCoord& coord, ScreenSize &size, FramebufferSize& fbSize) {
            glfwSwapBuffers(win.handle);
            glfwGetWindowPos(win.handle, &coord.x, &coord.y);
            glfwGetWindowSize(win.handle, &size.width, &size.height);
            glfwGetFramebufferSize(win.handle, &fbSize.width, &fbSize.height);
        })
    ;
}

flecs::entity glfw::createWindow(flecs::world& w, int width, int height, const char *title) {
    GLFWwindow *win = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!win) return w.entity(0); // FIXME: is 0 an invalid entity
    auto e = w.entity()
        .set_name(title)
        .set<WindowHandle>({ win })
        .set<ScreenSize>({ width, height })
        .set<ScreenCoord>({ 0, 0 })
        .set<FramebufferSize>({ 0, 0 })
    ;
    // Set the user pointer as the entity directly 
    glfwSetWindowUserPointer(win, w);
    return e;
}

