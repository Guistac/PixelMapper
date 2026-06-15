#include <iostream>

#include <shared_mutex>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/glad.h>
#define GL_SILENCE_DEPRECATION

#include "PixelMapper.h"
#include "utils/Profiling.h"

int main(){
    if(!glfwInit()) return 1; //this also sets the working directory to .app/Resources on MacOs builds

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

    GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
    int xpos = 0, ypos = 0, workWidth = 1280, workHeight = 720;
    if (primaryMonitor) {
        glfwGetMonitorWorkarea(primaryMonitor, &xpos, &ypos, &workWidth, &workHeight);
    }

    GLFWwindow* mainWindow = glfwCreateWindow(workWidth, workHeight, "PixelMapper", nullptr, nullptr);
    if (primaryMonitor) {
        glfwSetWindowPos(mainWindow, xpos, ypos);
    }
    glfwMakeContextCurrent(mainWindow); //enable the opengl context
    glfwSwapInterval(1);    //enable vsync

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* sharedContextWindow = glfwCreateWindow(1, 1, "Shared Context Offscreen", nullptr, mainWindow);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    if(!gladLoadGL()) return 0;

    PixelMapper::App::sharedContextWindow = sharedContextWindow;

    //initialize gui contexts
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    //enable docking & viewports
    ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    
    //initialize glfw & opengl backends
    ImGui_ImplGlfw_InitForOpenGL(mainWindow, true);
    ImGui_ImplOpenGL3_Init(glsl_version);


    flecs::world world;
    world.import<flecs::stats>();
    world.set<flecs::Rest>({});   
    PixelMapper::App::import(world);
    PixelMapper::Gui::import(world);

    //init our app
    auto pixelMapper = PixelMapper::App::get(world);

    // ── Clean startup: load from disk, or create one empty patch on first run ──
    const std::string defaultPatchPath = "patches/default.xml";
    if (!PixelMapper::PatchSerializer::load(pixelMapper, defaultPatchPath)) {
        // First run — create an empty default patch and save it
        auto defaultPatch = PixelMapper::Patch::create(pixelMapper);
        defaultPatch.set_name("Patch 1");
        PixelMapper::PatchSerializer::save(pixelMapper, defaultPatchPath);
    }

    while(!glfwWindowShouldClose(mainWindow)){
        //with multiple viewports the context of the main window needs to be set on each frame
		glfwMakeContextCurrent(mainWindow);
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        world.progress();

        int display_w, display_h;
        glfwGetFramebufferSize(mainWindow, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0,0,0,255);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(mainWindow);

        //Update and Render additional Viewports
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
    }

    PixelMapper::App::terminate();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
	glfwTerminate();

    return 1;
}//main()
