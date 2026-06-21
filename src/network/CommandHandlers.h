#pragma once
#include <flecs.h>
#include <string>
#include <vector>
#include "CommandProtocol.h"
#include "SimpleJson.h"

namespace PixelMapper::Network {

// Low-level helper forward declaration (implemented in AsioNetworkManager)
void sendCommandToServer(CommandType type, const std::string& jsonPayload);

// Client-side triggers
template <typename T>
void sendEntityMutation(flecs::entity entity) {
    if (!entity.is_valid()) return;
    flecs::world w = entity.world();
    const T* comp = entity.get<T>();
    if (!comp) return;

    // Use Flecs Reflection to convert the component data to JSON
    std::string compJson = w.to_json<T>(comp).c_str();

    std::unordered_map<std::string, std::string> kvs;
    kvs["entityPath"] = entity.path().c_str();
    kvs["componentName"] = w.entity<T>().name().c_str();
    kvs["componentData"] = compJson;

    // Dispatch via TCP
    sendCommandToServer(CommandType::EntityMutation, SimpleJson::build(kvs));
}

void sendApplyScript(const std::string& path, const std::string& type, const std::string& source);
void sendFileIORequest(const std::string& action, const std::string& path);
void sendTransportControl(int cueIndex);

// Server Command Route handler (Run on authoritative world)
void handleServerCommand(CommandType type, const std::string& jsonPayload, flecs::world& world);

// Client Command Route handler (Run on mirrored GUI world)
void handleClientCommand(CommandType type, const std::string& jsonPayload, flecs::world& world);

// Compiler error diagnostics helper
struct ErrorMarker {
    int line;
    std::string message;
};

// Headless compilation helpers (Server side)
bool compileGLSLHeadless(const std::string& source, std::string& outLog, std::vector<ErrorMarker>& outErrors);
bool compileLuaHeadless(const std::string& source, std::string& outLog, std::vector<ErrorMarker>& outErrors);

// Low-level helper (implemented in AsioNetworkManager)
void sendCommandToServer(CommandType type, const std::string& jsonPayload);

} // namespace PixelMapper::Network
