#pragma once
struct EntityHasher {
    size_t operator()(const flecs::entity& e) const {
        return std::hash<uint64_t>()(e.id());
    }
};