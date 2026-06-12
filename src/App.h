#pragma once
#include <flecs.h>
#include "Patch.h"
#include "Fixture.h"
#include "Artnet.h"

namespace PixelMapper {

namespace Gui {
    void import(flecs::world& w);
}

namespace App {
    struct Is {};
    struct PatchFolder {};
    struct SelectedPatch {};

    struct Queries {
        flecs::query<Patch::Is> patch;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress> fixtureWithDmxInPatch;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress> fixtureInDmxUniverse;
        flecs::query<Fixture::Is, Fixture::PixelData> fixtureWithPixelDataInPatch;
        flecs::query<Artnet::Universe::Is, Artnet::Universe::Properties> dmxUniverseInPatch;
        flecs::query<Artnet::Device::Is, Artnet::Device::Settings> artnetDeviceInPatch;
    };

    void import(flecs::world& w);
    void terminate();
    flecs::entity get(const flecs::world& w);
    const Queries& getQueries(const flecs::world& w);
}

} // namespace PixelMapper
