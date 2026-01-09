#include "glfw.hpp"

glfw::glfw(flecs::world& w) {
    glfwSetErrorCallback([](int error, const char* desc){
        fprintf(stderr, "GLFW::error %d: %s\n", error, desc);
    });
    // This also sets the working directory to .app/Resources on MacOs builds
    if (!glfwInit()) return; // FIXME: how do we handle error in constructor ???

    w.component<Invalid>();
    
    w.component<FramebufferSize>()
        .member<int>("width")
        .member<int>("height")
    ;

    w.component<WindowHandle>();
    w.component<ScreenCoord>()
        .member<int>("x")
        .member<int>("y")
    ;
    w.component<ScreenSize>()
        .member<int>("width")
        .member<int>("height")
    ;
    // TODO: make these enum
    w.component<WindowHint>()
        .member<int>("name")
        .member<int>("value")
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

    w.system("Poll events").kind(flecs::PreUpdate).each([]() { glfwPollEvents(); });

    w.system<const ScreenSize, const ScreenCoord, const WindowHintList*>("Window builder")
        .kind(flecs::PreUpdate)
        .without<Invalid>()
        .without<WindowHandle>()
        .without<FramebufferSize>()
        .each(buildWindow)
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

glfw::~glfw() {
    glfwTerminate();
}

void glfw::buildWindow(flecs::entity e, const ScreenSize& size, const ScreenCoord& coord, const WindowHintList* hints) {
    // TODO:
    // - Walk entity hierarchie to find parent window
    // - optional display ?
    if (hints) for (int i=0; hints->hints[i].name; i++)
        glfwWindowHint(hints->hints[i].name, hints->hints[i].value);
    glfwWindowHint(GLFW_POSITION_X, coord.x);
    glfwWindowHint(GLFW_POSITION_Y, coord.y);
    GLFWwindow *win = glfwCreateWindow(size.width, size.height, e.name(), nullptr, nullptr);
    if (!win) { e.add<Invalid>(); return; }
    e.set<WindowHandle>({ win });
    int fb_width = 0, fb_height = 0;
    glfwGetFramebufferSize(win, &fb_width, &fb_height);
    e.set<FramebufferSize>({ fb_width, fb_height });
}

//flecs::entity glfw::createWindow(flecs::world& w, int width, int height, const char *title) {
//    GLFWwindow *win = glfwCreateWindow(width, height, title, nullptr, nullptr);
//    if (!win) return w.entity(0); // FIXME: is 0 an invalid entity
//    auto e = w.entity()
//        .set_name(title)
//        .set<WindowHandle>({ win })
//        .set<ScreenSize>({ width, height })
//        .set<ScreenCoord>({ 0, 0 })
//        .set<FramebufferSize>({ 0, 0 })
//    ;
//    // Set the user pointer as the entity directly 
//    glfwSetWindowUserPointer(win, w);
//    return e;
//}

