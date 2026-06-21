#pragma once
#include <flecs.h>
#include <string>
#include <unordered_map>
#include "shared/network/CommandProtocol.h"
#include "shared/network/SimpleJson.h"

namespace PixelMapper::Network {

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

void handleClientCommand(CommandType type, const std::string& jsonPayload, flecs::world& world);

} // namespace PixelMapper::Network
