#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/glad.h>
#define GL_SILENCE_DEPRECATION

#include "PixelMapper.h"


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

    GLFWwindow* mainWindow = glfwCreateWindow(1280, 720, "PixelMapper", nullptr, nullptr);
    glfwMakeContextCurrent(mainWindow); //enable the opengl context
    glfwSwapInterval(1);    //enable vsync

    if(!gladLoadGL()) return 0;

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

    //import our flecs modules
    flecs::world world;
    world.import<flecs::stats>();
    world.set<flecs::Rest>({});    
    PixelMapper::import(world);

    //init our app
    auto pixelMapper = PixelMapper::get(world);
    auto patch1 = PixelMapper::createPatch(pixelMapper);
    PixelMapper::Patch::spawnLineFixture(patch1, glm::vec2(100.0, 200.0), glm::vec2(200.0, 100.0));
    PixelMapper::Patch::spawnCircleFixture(patch1, glm::vec2(100.0, 100.0), 50.0);
    auto patch2 = PixelMapper::createPatch(pixelMapper);
    PixelMapper::selectPatch(pixelMapper, patch2);
    int fixtureCount = 0;
    int pixelCount = 50;
    int channelCount = 4;
    for(int i = 0; i < 8; i++){
        for(int j = 0; j < 8; j++){
            flecs::entity fixture = PixelMapper::Patch::spawnCircleFixture(patch2, glm::vec2(i*100.0 + 50.0, j*100.0 + 50), 45.0, pixelCount, channelCount);
            int addresses = pixelCount * channelCount * fixtureCount;
            int universe = addresses / 512;
            int startAddress = addresses % 512;
            fixtureCount++;
            PixelMapper::Fixture::setDmxProperties(fixture, universe, startAddress);
        }
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

        if(ImGui::BeginMainMenuBar()){
            PixelMapper::menubar(world);
            ImGui::EndMainMenuBar();
        }
        ImGui::DockSpaceOverViewport();
        PixelMapper::gui(world);

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

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
	glfwTerminate();

    return 1;
}//main()
