#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/glad.h>

#include "PixelMapper.h"
#include "flecs-modules/glfw.hpp"

#define GL_SILENCE_DEPRECATION

int main(){
    flecs::world world;
    world.import<flecs::stats>();
    world.set<flecs::Rest>({});

    world.import<glfw>();

#if defined(__APPLE__)
    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
#else
    // GL 3.0 + GLSL 130
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

    auto windowEntt = glfw::createWindow(world, 1280, 720, "PixelMapperWindow");
    GLFWwindow *mainWindow = windowEntt.get<WindowHandle>().handle;
    
    //activate the opengl context & activate vsync
    glfwMakeContextCurrent(mainWindow);
    glfwSwapInterval(1);

    if(!gladLoadGL()) {
        return 0;
    }

    //initialize gui contexts
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    //enable docking & viewports
    ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    
    //initialize glfw & opengl backends
    ImGui_ImplGlfw_InitForOpenGL(mainWindow, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    
    PixelMapper::init(world);

    // FIXME: Hack until i figure out how to handle that
    //glfwPollEvents();
    //int display_w, display_h;
    //glfwGetFramebufferSize(mainWindow, &display_w, &display_h);
    //glViewport(0, 0, display_w, display_h);
    //glClearColor(0,0,0,255);
    //glClear(GL_COLOR_BUFFER_BIT);
    // -------------------------------------------------


    world.system("temp update")
        .kind(flecs::OnUpdate)
        .each([&](){
            // With multiple viewports the context of the main window needs to be set on each frame
            glfwMakeContextCurrent(mainWindow);
            // Start the Dear ImGui frame
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            if(ImGui::BeginMainMenuBar()){
                if(ImGui::BeginMenu("PixelMapper")){
                    ImGui::EndMenu();
                }
                if(ImGui::BeginMenu("Edit")){
                    ImGui::EndMenu();
                }
                if(ImGui::BeginMenu("View")){
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }
            ImGui::DockSpaceOverViewport();

            PixelMapper::gui(world);

            // Rendering
            ImGui::Render();

            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        })
    ;

    while (!glfwWindowShouldClose(mainWindow) && world.progress());

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
	glfwTerminate();

    return 1;
}
