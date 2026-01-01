#include <iostream>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/glad.h>

#include "PixelMapper.h"

#define GL_SILENCE_DEPRECATION

void glfw_error_callback(int error, const char* description){
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(){
    if(!glfwInit()) return 1; //this also sets the working directory to .app/Resources on MacOs builds

    glfwSetErrorCallback(glfw_error_callback);

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
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    
    //initialize glfw & opengl backends
    ImGui_ImplGlfw_InitForOpenGL(mainWindow, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    flecs::world world;
    world.import<flecs::stats>();
    world.set<flecs::Rest>({});
    
    PixelMapper::init(world);

    while(!glfwWindowShouldClose(mainWindow)){

		world.progress(); //call this on each frame for flecs::rest to work
		
        //with multiple viewports the context of the main window needs to be set on each frame
		glfwMakeContextCurrent(mainWindow);

        glfwPollEvents();

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

        int display_w, display_h;
        glfwGetFramebufferSize(mainWindow, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0,0,0,255);
        glClear(GL_COLOR_BUFFER_BIT);

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
}
