#include <iostream>
#include <shared_mutex>
#include <thread>
#include <chrono>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glad/glad.h>
#define GL_SILENCE_DEPRECATION

#include "shared/PixelMapper.h"
#include "shared/utils/Profiling.h"
#include "shared/network/AsioNetworkManager.h"
#include "server/network/ServerCommandHandlers.h"
#include "client/network/ClientCommandHandlers.h"
#include "server/App.h"
#include "server/PatchSerializer.h"


enum class AppMode {
    Standalone,
    Server,
    Client
};

AppMode g_appMode = AppMode::Standalone;
bool g_serverRunning = true;
std::thread g_serverWorldThread;
GLFWwindow* g_serverWorldContext = nullptr;
std::atomic<bool> g_syncRequested{false};

void runServerWorldLoop(flecs::world& world) {
    if (g_serverWorldContext) {
        glfwMakeContextCurrent(g_serverWorldContext);
    }
    while (g_serverRunning) {
        auto start = std::chrono::steady_clock::now();
        if (g_syncRequested.exchange(false)) {
            std::string worldJson = world.to_json().c_str();
            PixelMapper::Network::AsioNetworkManager::getInstance().broadcastCommandToClients(
                PixelMapper::Network::CommandType::SyncWorldState, 
                worldJson
            );
        }
        world.progress();
        auto end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        if (elapsed < 16) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16 - elapsed));
        }
    }
    if (g_serverWorldContext) {
        glfwMakeContextCurrent(nullptr);
    }
}

// Thread-safe GPU upload helper called on the main thread possessing the OpenGL context
void applyTelemetryToClientProgram() {
    std::lock_guard<std::mutex> lock(PixelMapper::App::clientTelemetryMutex);
    if (PixelMapper::App::clientTelemetryDataNew) {
        auto program = std::atomic_load(&PixelMapper::App::currentPatchProgram);
        if (program) {
            // 1. Canvas Confidence Monitor: Copy pixels directly to active program color array
            uint32_t toCopy = std::min(program->pixelCount, (uint32_t)PixelMapper::App::clientTelemetryPixels.size());
            if (toCopy > 0 && program->pixelColors) {
                std::memcpy(program->pixelColors, PixelMapper::App::clientTelemetryPixels.data(), toCopy * sizeof(PixelMapper::ColorRGBW));
            }
            
            // 2. 2D Previews: Upload motive parameters directly to GPU UBO
            if (program->glslUboId > 0) {
                glBindBuffer(GL_UNIFORM_BUFFER, program->glslUboId);
                glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PixelMapper::Generative::EngineStateUBO), &PixelMapper::App::clientTelemetryUbo);
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
            }
        }
        PixelMapper::App::clientTelemetryDataNew = false;
    }
}

int main(int argc, char* argv[]) {
    // 1. Parse command line modes
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") {
            g_appMode = AppMode::Server;
        } else if (arg == "--client") {
            g_appMode = AppMode::Client;
        } else if (arg == "--standalone") {
            g_appMode = AppMode::Standalone;
        }
    }

    // Set network/app mode global variable
    if (g_appMode == AppMode::Server) {
        PixelMapper::App::g_appMode = PixelMapper::App::AppMode::Server;
    } else if (g_appMode == AppMode::Client) {
        PixelMapper::App::g_appMode = PixelMapper::App::AppMode::Client;
    } else {
        PixelMapper::App::g_appMode = PixelMapper::App::AppMode::Standalone;
    }

    if (!glfwInit()) return 1;

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* mainWindow = nullptr;
    GLFWwindow* sharedContextWindow = nullptr;
    GLFWwindow* serverWorldContext = nullptr;

    // 2. Initialize GLFW Window and OpenGL Context based on mode
    if (g_appMode == AppMode::Server) {
        // Headless Server: Hidden GLFW window context for OpenGL operations
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        sharedContextWindow = glfwCreateWindow(1, 1, "PixelMapper Headless Server RT Context", nullptr, nullptr);
        if (!sharedContextWindow) {
            std::cerr << "Failed to create headless OpenGL RT context window" << std::endl;
            glfwTerminate();
            return 1;
        }

        serverWorldContext = glfwCreateWindow(1, 1, "PixelMapper Headless Server World Context", nullptr, sharedContextWindow);
        if (!serverWorldContext) {
            std::cerr << "Failed to create headless OpenGL World context window" << std::endl;
            glfwTerminate();
            return 1;
        }

        glfwMakeContextCurrent(serverWorldContext);
        if (!gladLoadGL()) return 0;
        PixelMapper::App::sharedContextWindow = sharedContextWindow;
        g_serverWorldContext = serverWorldContext;
    } 
    else {
        // Client or Standalone: Create visible GLFW Window
        GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
        int xpos = 0, ypos = 0, workWidth = 1280, workHeight = 720;
        if (primaryMonitor) {
            glfwGetMonitorWorkarea(primaryMonitor, &xpos, &ypos, &workWidth, &workHeight);
        }

        mainWindow = glfwCreateWindow(workWidth, workHeight, "PixelMapper", nullptr, nullptr);
        if (!mainWindow) {
            std::cerr << "Failed to create visible GLFW window" << std::endl;
            glfwTerminate();
            return 1;
        }
        if (primaryMonitor) {
            glfwSetWindowPos(mainWindow, xpos, ypos);
        }
        glfwMakeContextCurrent(mainWindow);
        glfwSwapInterval(1); // Enable vsync

        // Shared offscreen context window
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        sharedContextWindow = glfwCreateWindow(1, 1, "Shared Context Offscreen", nullptr, mainWindow);
        
        if (g_appMode == AppMode::Standalone) {
            serverWorldContext = glfwCreateWindow(1, 1, "Server World Context", nullptr, sharedContextWindow);
            g_serverWorldContext = serverWorldContext;
        }

        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

        if (!gladLoadGL()) return 0;
        PixelMapper::App::sharedContextWindow = sharedContextWindow;

        // Initialize UI context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
        
        ImGui_ImplGlfw_InitForOpenGL(mainWindow, true);
        ImGui_ImplOpenGL3_Init(glsl_version);
    }

    // 3. Initialize Flecs worlds and Networking based on AppMode
    flecs::world serverWorld;
    flecs::world clientWorld;

    if (g_appMode == AppMode::Server) {
        // Setup authoritative Server Flecs world
        serverWorld.import<flecs::stats>();
        serverWorld.set<flecs::Rest>({});   
        PixelMapper::App::import(serverWorld);

        // Start server network manager
        PixelMapper::Network::AsioNetworkManager::getInstance().setCommandCallback([&serverWorld](PixelMapper::Network::CommandType type, const std::string& payload) {
            PixelMapper::Network::handleServerCommand(type, payload, serverWorld);
        });
        PixelMapper::Network::AsioNetworkManager::getInstance().setClientConnectCallback([]() {
            g_syncRequested.store(true);
        });
        PixelMapper::Network::AsioNetworkManager::getInstance().startServer(7777, 7778);

        auto pixelMapper = PixelMapper::App::get(serverWorld);
        const std::string defaultPatchPath = "patches/default.xml";
        if (!PixelMapper::PatchSerializer::load(pixelMapper, defaultPatchPath)) {
            auto defaultPatch = PixelMapper::Patch::create(pixelMapper);
            defaultPatch.set_name("Patch 1");
            PixelMapper::PatchSerializer::save(pixelMapper, defaultPatchPath);
        }

        std::cout << "[Server] Headless server loop running..." << std::endl;
        while (g_serverRunning) {
            if (g_syncRequested.exchange(false)) {
                std::string worldJson = serverWorld.to_json().c_str();
                PixelMapper::Network::AsioNetworkManager::getInstance().broadcastCommandToClients(
                    PixelMapper::Network::CommandType::SyncWorldState, 
                    worldJson
                );
            }
            serverWorld.progress();
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        }

        PixelMapper::App::terminate();
        PixelMapper::Network::AsioNetworkManager::getInstance().stop();
    }
    else if (g_appMode == AppMode::Client) {
        // Setup Client mirrored Flecs world
        clientWorld.import<flecs::stats>();
        PixelMapper::App::importComponents(clientWorld); // Register components locally
        PixelMapper::Gui::import(clientWorld);

        // Start Client network manager
        PixelMapper::Network::AsioNetworkManager::getInstance().setCommandCallback([&clientWorld](PixelMapper::Network::CommandType type, const std::string& payload) {
            PixelMapper::Network::handleClientCommand(type, payload, clientWorld);
        });
        
        // Telemetry callback updates local preview variables
        PixelMapper::Network::AsioNetworkManager::getInstance().setTelemetryCallback([](const PixelMapper::Generative::EngineStateUBO& ubo, const PixelMapper::ColorRGBW* pixels, uint32_t count) {
            std::lock_guard<std::mutex> lock(PixelMapper::App::clientTelemetryMutex);
            PixelMapper::App::clientTelemetryUbo = ubo;
            PixelMapper::App::clientTelemetryPixels.assign(pixels, pixels + count);
            PixelMapper::App::clientTelemetryDataNew = true;
        });
        PixelMapper::Network::AsioNetworkManager::getInstance().startClient("127.0.0.1", 7777, 7778);

        while (!glfwWindowShouldClose(mainWindow)) {
            glfwMakeContextCurrent(mainWindow);
            glfwPollEvents();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Fetch and apply telemetry update prior to ECS updates
            applyTelemetryToClientProgram();

            clientWorld.progress();

            int display_w, display_h;
            glfwGetFramebufferSize(mainWindow, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0, 0, 0, 255);
            glClear(GL_COLOR_BUFFER_BIT);

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(mainWindow);

            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }

        PixelMapper::Network::AsioNetworkManager::getInstance().stop();
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }
    else if (g_appMode == AppMode::Standalone) {
        // Standalone Mode: Spawn both Server and Client in-process on loopback
        serverWorld.import<flecs::stats>();
        serverWorld.set<flecs::Rest>({});   
        PixelMapper::App::import(serverWorld);

        clientWorld.import<flecs::stats>();
        PixelMapper::App::importComponents(clientWorld); // Register components locally
        PixelMapper::Gui::import(clientWorld);

        // Server socket and callbacks
        PixelMapper::Network::AsioNetworkManager::getInstance().setCommandCallback([&serverWorld, &clientWorld](PixelMapper::Network::CommandType type, const std::string& payload) {
            if (PixelMapper::Network::AsioNetworkManager::getInstance().isServer()) {
                PixelMapper::Network::handleServerCommand(type, payload, serverWorld);
            } else {
                PixelMapper::Network::handleClientCommand(type, payload, clientWorld);
            }
        });
        PixelMapper::Network::AsioNetworkManager::getInstance().setClientConnectCallback([]() {
            g_syncRequested.store(true);
        });

        // Start server first
        PixelMapper::Network::AsioNetworkManager::getInstance().startServer(7777, 7778);

        // Load patches into server
        auto pixelMapper = PixelMapper::App::get(serverWorld);
        const std::string defaultPatchPath = "patches/default.xml";
        if (!PixelMapper::PatchSerializer::load(pixelMapper, defaultPatchPath)) {
            auto defaultPatch = PixelMapper::Patch::create(pixelMapper);
            defaultPatch.set_name("Patch 1");
            PixelMapper::PatchSerializer::save(pixelMapper, defaultPatchPath);
        }

        // Run server world update loop in a background thread
        g_serverRunning = true;
        g_serverWorldThread = std::thread(runServerWorldLoop, std::ref(serverWorld));

        // Start client and connect to server loopback
        PixelMapper::Network::AsioNetworkManager::getInstance().setTelemetryCallback([](const PixelMapper::Generative::EngineStateUBO& ubo, const PixelMapper::ColorRGBW* pixels, uint32_t count) {
            std::lock_guard<std::mutex> lock(PixelMapper::App::clientTelemetryMutex);
            PixelMapper::App::clientTelemetryUbo = ubo;
            PixelMapper::App::clientTelemetryPixels.assign(pixels, pixels + count);
            PixelMapper::App::clientTelemetryDataNew = true;
        });
        PixelMapper::Network::AsioNetworkManager::getInstance().startClient("127.0.0.1", 7777, 7778);

        while (!glfwWindowShouldClose(mainWindow)) {
            glfwMakeContextCurrent(mainWindow);
            glfwPollEvents();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            // Fetch and apply telemetry update prior to ECS updates
            applyTelemetryToClientProgram();

            clientWorld.progress();

            int display_w, display_h;
            glfwGetFramebufferSize(mainWindow, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0, 0, 0, 255);
            glClear(GL_COLOR_BUFFER_BIT);

            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            glfwSwapBuffers(mainWindow);

            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
        }

        // Clean shutdown
        g_serverRunning = false;
        if (g_serverWorldThread.joinable()) {
            g_serverWorldThread.join();
        }

        PixelMapper::App::terminate();
        PixelMapper::Network::AsioNetworkManager::getInstance().stop();

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    glfwTerminate();
    return 1;
}
