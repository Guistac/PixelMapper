#pragma once
#include <flecs.h>
#include <stdint.h>
#include <functional>

namespace PixelMapper {

namespace Artnet::Universe {
    struct Is {};
    struct SendTo {};
    struct Properties {
        uint16_t universeId;
        uint16_t usedSize;
    };
    struct Channels {
        uint8_t channels[512];
    };

    flecs::entity getSelected(flecs::entity patch);
    void select(flecs::entity patch, flecs::entity universe);
    void iterate(flecs::entity patch, std::function<void(flecs::entity dmxUniverse, Properties&)> fn);
    int getCount(flecs::entity patch);
    flecs::entity create(flecs::entity patch, uint16_t universeId);
    void import(flecs::world& w);
}

namespace Artnet::Device {
    struct Is {};
    struct SendsUniverse {};
    struct Settings {
        uint32_t ipAddress;
        uint16_t startUniverse;
        uint16_t universeCount;
    };

    flecs::entity create(flecs::entity patch);
    void iterateInPatch(flecs::entity patch, std::function<void(flecs::entity device, Settings&)> fn);
    void select(flecs::entity patch, flecs::entity device);
    flecs::entity getSelected(flecs::entity patch);
    void import(flecs::world& w);
}

} // namespace PixelMapper
