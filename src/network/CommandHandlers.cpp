#include "CommandHandlers.h"
#include "AsioNetworkManager.h"
#include "SimpleJson.h"
#include "../Patch.h"
#include "../PatchSerializer.h"
#include "../App.h"
#include <glad/glad.h>
#include <sol/sol.hpp>
#include <iostream>

namespace PixelMapper::Network {

// ─────────────────────────────────────────────────────────────────────
// CLIENT SEND TRIGGERS
// ─────────────────────────────────────────────────────────────────────

void sendCommandToServer(CommandType type, const std::string& jsonPayload) {
    AsioNetworkManager::getInstance().sendCommandToServer(type, jsonPayload);
}

void sendApplyScript(const std::string& path, const std::string& type, const std::string& source) {
    std::unordered_map<std::string, std::string> kvs;
    kvs["entityPath"] = path;
    kvs["type"] = type;      // "GLSL" or "LUA"
    kvs["source"] = source;
    sendCommandToServer(CommandType::ApplyScript, SimpleJson::build(kvs));
}

void sendFileIORequest(const std::string& action, const std::string& path) {
    std::unordered_map<std::string, std::string> kvs;
    kvs["action"] = action;  // "SAVE" or "LOAD"
    kvs["path"] = path;
    sendCommandToServer(CommandType::FileIORequest, SimpleJson::build(kvs));
}

void sendTransportControl(int cueIndex) {
    std::unordered_map<std::string, std::string> kvs;
    kvs["cueIndex"] = std::to_string(cueIndex);
    sendCommandToServer(CommandType::TransportControl, SimpleJson::build(kvs));
}

// ─────────────────────────────────────────────────────────────────────
// SERVER RECEIVE ROUTER
// ─────────────────────────────────────────────────────────────────────

void handleServerCommand(CommandType type, const std::string& jsonPayload, flecs::world& world) {
    switch (type) {
        case CommandType::EntityMutation: {
            auto rpc = SimpleJson::parse(jsonPayload);
            std::string entityPath = rpc.getString("entityPath");
            std::string componentName = rpc.getString("componentName");
            std::string componentData = rpc.getString("componentData");

            flecs::entity entity = world.lookup(entityPath.c_str());
            if (!entity.is_valid()) {
                std::cerr << "[Server RPC] Entity not found: " << entityPath << std::endl;
                return;
            }

            flecs::entity compType = world.lookup(componentName.c_str());
            if (!compType.is_valid()) {
                std::cerr << "[Server RPC] Component type not found: " << componentName << std::endl;
                return;
            }

            // Apply mutation dynamically using Flecs JSON parser
            void* ptr = entity.get_mut(compType);
            ecs_ptr_from_json(world.c_ptr(), compType.id(), ptr, componentData.c_str(), nullptr);
            entity.modified(compType);

            // Re-broadcast mutation to all other connected clients
            AsioNetworkManager::getInstance().broadcastCommandToClients(CommandType::EntityMutation, jsonPayload);
            break;
        }
        case CommandType::ApplyScript: {
            auto rpc = SimpleJson::parse(jsonPayload);
            std::string entityPath = rpc.getString("entityPath");
            std::string scriptType = rpc.getString("type");
            std::string sourceStr = rpc.getString("source");

            flecs::entity entity = world.lookup(entityPath.c_str());
            if (!entity.is_valid()) return;

            std::string outLog;
            std::vector<ErrorMarker> errors;
            bool compileSuccess = false;

            if (scriptType == "GLSL") {
                compileSuccess = compileGLSLHeadless(sourceStr, outLog, errors);
            } else if (scriptType == "LUA") {
                compileSuccess = compileLuaHeadless(sourceStr, outLog, errors);
            }

            // Update authoritative entity components to trigger systems
            if (compileSuccess) {
                if (scriptType == "GLSL") {
                    if (entity.has<Patch::ScriptData>()) {
                        auto* sd = entity.try_get_mut<Patch::ScriptData>();
                        if (sd) {
                            sd->glslSource = sourceStr;
                            sd->compilerLog = "Compilation Successful";
                        }
                    }
                } else if (scriptType == "LUA") {
                    if (entity.has<Patch::ScriptData>()) {
                        auto* sd = entity.try_get_mut<Patch::ScriptData>();
                        if (sd) {
                            sd->luaSource = sourceStr;
                            sd->compilerLog = "Compilation Successful";
                        }
                    }
                }
                entity.add<Patch::ProgramDirty>(); // Trigger compilation and rtPatchRunner swap
            } else {
                if (entity.has<Patch::ScriptData>()) {
                    auto* sd = entity.try_get_mut<Patch::ScriptData>();
                    if (sd) {
                        sd->compilerLog = outLog;
                    }
                }
            }

            // Build compilation result RPC payload
            std::unordered_map<std::string, std::string> response;
            response["entityPath"] = entityPath;
            response["success"] = compileSuccess ? "true" : "false";
            response["log"] = outLog;
            
            // Serialize errors to simple format
            std::stringstream errorArray;
            errorArray << "[";
            for (size_t i = 0; i < errors.size(); ++i) {
                if (i > 0) errorArray << ",";
                errorArray << "{\"line\":" << errors[i].line << ",\"message\":\"" << errors[i].message << "\"}";
            }
            errorArray << "]";
            response["errors"] = errorArray.str();

            AsioNetworkManager::getInstance().broadcastCommandToClients(
                CommandType::CompilationResult, 
                SimpleJson::build(response)
            );
            break;
        }
        case CommandType::FileIORequest: {
            auto rpc = SimpleJson::parse(jsonPayload);
            std::string action = rpc.getString("action");
            std::string path = rpc.getString("path");

            auto app = world.target<PixelMapper::App::Is>();

            if (action == "SAVE") {
                PatchSerializer::save(app, path);
            } else if (action == "LOAD") {
                if (PatchSerializer::load(app, path)) {
                    // Send full synchronized state to clients
                    std::string worldJson = world.to_json().c_str();
                    AsioNetworkManager::getInstance().broadcastCommandToClients(
                        CommandType::SyncWorldState, 
                        worldJson
                    );
                }
            }
            break;
        }
        case CommandType::TransportControl: {
            auto rpc = SimpleJson::parse(jsonPayload);
            int cueIndex = rpc.getInt("cueIndex");

            auto app = world.target<PixelMapper::App::Is>();
            auto selectedPatch = Patch::getSelected(app);
            if (selectedPatch.is_valid()) {
                auto* program = PixelMapper::App::currentPatchProgram.get();
                if (program) {
                    program->activeCueIndex.store(cueIndex);
                    // Force rebuild/update on the real-time thread
                    selectedPatch.add<Patch::ProgramDirty>();
                }
            }
            break;
        }
        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────
// CLIENT RECEIVE ROUTER
// ─────────────────────────────────────────────────────────────────────

void handleClientCommand(CommandType type, const std::string& jsonPayload, flecs::world& world) {
    switch (type) {
        case CommandType::EntityMutation: {
            auto rpc = SimpleJson::parse(jsonPayload);
            std::string entityPath = rpc.getString("entityPath");
            std::string componentName = rpc.getString("componentName");
            std::string componentData = rpc.getString("componentData");

            flecs::entity entity = world.lookup(entityPath.c_str());
            if (!entity.is_valid()) return;

            flecs::entity compType = world.lookup(componentName.c_str());
            if (!compType.is_valid()) return;

            // Apply mirror component update locally
            void* ptr = entity.get_mut(compType);
            ecs_ptr_from_json(world.c_ptr(), compType.id(), ptr, componentData.c_str(), nullptr);
            entity.modified(compType);
            break;
        }
        case CommandType::CompilationResult: {
            // Client receives compilation feedback to update local ImGui text editor error markers
            // Handled inside GUI loops or static callback triggers
            break;
        }
        case CommandType::SyncWorldState: {
            // Rebuild the client's local world hierarchy
            world.from_json(jsonPayload.c_str());
            break;
        }
        default:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────
// HEADLESS VALIDATION COMPILERS
// ─────────────────────────────────────────────────────────────────────

bool compileGLSLHeadless(const std::string& source, std::string& outLog, std::vector<ErrorMarker>& outErrors) {
    // Requires active GL context (headless GLFW window bound to this thread)
    unsigned int shader = glCreateShader(GL_FRAGMENT_SHADER);
    const char* srcPtr = source.c_str();
    glShaderSource(shader, 1, &srcPtr, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

    char infoLog[2048] = {0};
    glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
    outLog = infoLog;

    glDeleteShader(shader);

    if (!success) {
        std::stringstream ss(outLog);
        std::string line;
        while (std::getline(ss, line)) {
            int lineNum = -1;
            // Parse error line number e.g. "ERROR: 0:15: ..."
            size_t firstDigit = line.find_first_of("0123456789");
            if (firstDigit != std::string::npos) {
                size_t secondDelim = line.find_first_of(":(", firstDigit);
                if (secondDelim != std::string::npos) {
                    size_t nextDigit = line.find_first_of("0123456789", secondDelim);
                    if (nextDigit != std::string::npos) {
                        try {
                            lineNum = std::stoi(line.substr(nextDigit));
                        } catch (...) {}
                    }
                }
            }
            if (lineNum > 0) {
                outErrors.push_back({lineNum, line});
            }
        }
        return false;
    }
    return true;
}

bool compileLuaHeadless(const std::string& source, std::string& outLog, std::vector<ErrorMarker>& outErrors) {
    sol::state lua;
    auto result = lua.load(source);
    if (!result.valid()) {
        sol::error err = result;
        outLog = err.what();

        int lineNum = -1;
        // Parse error line number e.g. "[string \"code\"]:15: ..."
        size_t colon1 = outLog.find("]:");
        if (colon1 != std::string::npos) {
            size_t colon2 = outLog.find(':', colon1 + 2);
            if (colon2 != std::string::npos) {
                std::string lineStr = outLog.substr(colon1 + 2, colon2 - (colon1 + 2));
                try {
                    lineNum = std::stoi(lineStr);
                } catch (...) {}
            }
        }
        if (lineNum > 0) {
            outErrors.push_back({lineNum, outLog});
        }
        return false;
    }
    return true;
}

} // namespace PixelMapper::Network
