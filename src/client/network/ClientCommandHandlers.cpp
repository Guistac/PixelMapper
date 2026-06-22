#include "client/network/ClientCommandHandlers.h"
#include "shared/network/AsioNetworkManager.h"
#include "shared/network/SimpleJson.h"

namespace PixelMapper::Network {

void sendCommandToServer(CommandType type, const std::string& jsonPayload) {
    AsioNetworkManager::getInstance().sendCommandToServer(type, jsonPayload);
}

void sendSpawnEntityRequest(const std::string& parentPath, const std::string& type) {
    std::unordered_map<std::string, std::string> kvs;
    kvs["parentPath"] = parentPath;
    kvs["type"] = type;
    sendCommandToServer(CommandType::SpawnEntityRequest, SimpleJson::build(kvs));
}

void sendDeleteEntityRequest(const std::string& entityPath) {
    std::unordered_map<std::string, std::string> kvs;
    kvs["entityPath"] = entityPath;
    sendCommandToServer(CommandType::DeleteEntityRequest, SimpleJson::build(kvs));
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

} // namespace PixelMapper::Network
