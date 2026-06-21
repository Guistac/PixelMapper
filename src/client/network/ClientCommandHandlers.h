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
    kvs["componentName"] = w.entity<T>().path().c_str(); // Use path() for absolute resolution
    kvs["componentData"] = compJson;

    // Dispatch via TCP
    sendCommandToServer(CommandType::EntityMutation, SimpleJson::build(kvs));
}

template <typename T>
void sendEntityMutation(flecs::entity entity, const T* componentPtr) {
    if (!entity.is_valid() || !componentPtr) return;
    flecs::world w = entity.world();
    std::string compJson = w.to_json<T>(componentPtr).c_str();

    std::unordered_map<std::string, std::string> kvs;
    kvs["entityPath"] = entity.path().c_str();
    kvs["componentName"] = w.entity<T>().path().c_str(); // Use path() for absolute resolution
    kvs["componentData"] = compJson;

    sendCommandToServer(CommandType::EntityMutation, SimpleJson::build(kvs));
}

template <typename Relation, typename Object>
void sendEntityMutationPair(flecs::entity entity, const Object* componentPtr) {
    if (!entity.is_valid() || !componentPtr) return;
    flecs::world w = entity.world();
    flecs::entity_t tid = w.id(w.entity<Relation>(), w.entity<Object>());
    std::string compJson = w.to_json(tid, componentPtr).c_str();

    std::unordered_map<std::string, std::string> kvs;
    kvs["entityPath"] = entity.path().c_str();
    std::string compName = std::string("(") + w.entity<Relation>().path().c_str() + "," + w.entity<Object>().path().c_str() + ")";
    kvs["componentName"] = compName;
    kvs["componentData"] = compJson;

    sendCommandToServer(CommandType::EntityMutation, SimpleJson::build(kvs));
}

inline void sendEntityMutationPairDynamic(flecs::entity entity, flecs::entity relation, flecs::entity object) {
    if (!entity.is_valid() || !relation.is_valid() || !object.is_valid()) return;
    flecs::world w = entity.world();
    flecs::entity_t tid = w.id(relation, object);
    std::string compJson = "{}";

    std::unordered_map<std::string, std::string> kvs;
    kvs["entityPath"] = entity.path().c_str();
    std::string compName = std::string("(") + relation.path().c_str() + "," + object.path().c_str() + ")";
    kvs["componentName"] = compName;
    kvs["componentData"] = compJson;

    sendCommandToServer(CommandType::EntityMutation, SimpleJson::build(kvs));
}

void sendApplyScript(const std::string& path, const std::string& type, const std::string& source);
void sendFileIORequest(const std::string& action, const std::string& path);
void sendTransportControl(int cueIndex);

void handleClientCommand(CommandType type, const std::string& jsonPayload, flecs::world& world);

} // namespace PixelMapper::Network
