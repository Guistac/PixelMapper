#include "shared/Artnet.h"
#include "shared/components/PatchComponents.h"
#include "shared/components/AppComponents.h"
#include <string>

namespace PixelMapper {

namespace Artnet::Universe {

    void iterate(flecs::entity patch, std::function<void(flecs::entity dmxUniverse, Properties&)> fn){
        auto dmxUniverseFolder = patch.target<Patch::DmxUniverseFolder>();
        if(!dmxUniverseFolder.is_valid()) return;
        App::getQueries(patch.world()).dmxUniverseInPatch.set_var("parent", dmxUniverseFolder)
        .each([fn](flecs::entity universe, Artnet::Universe::Is, Artnet::Universe::Properties& properties){
            fn(universe, properties);
        });
    }

    int getCount(flecs::entity patch){
        auto dmxUniverseFolder = patch.target<Patch::DmxUniverseFolder>();
        if(!dmxUniverseFolder.is_valid()) return 0;
        return App::getQueries(patch.world()).dmxUniverseInPatch.set_var("parent", dmxUniverseFolder).count();
    }

    flecs::entity getSelected(flecs::entity patch){
        if(!patch.is_valid() || !patch.is_alive()) return flecs::entity::null();
        return patch.target<Patch::SelectedDmxUniverse>();
    }

    void select(flecs::entity patch, flecs::entity universe){
        patch.add<Patch::SelectedDmxUniverse>(universe);
    }

    flecs::entity create(flecs::entity patch, uint16_t universeId){
        auto dmxUniverseFolder = patch.target<Patch::DmxUniverseFolder>();
        if(!dmxUniverseFolder.is_valid()) return flecs::entity::null();
        std::string univName = "Universe " + std::to_string(universeId);
        flecs::entity newUniv = patch.world().entity()
            .child_of(dmxUniverseFolder)
            .set_name(univName.c_str())
            .add<Is>()
            .add<Channels>()
            .set<Properties>({universeId, 0});
        select(patch, newUniv);
        return newUniv;
    }

    void import(flecs::world& w){
        w.component<Is>();
        w.component<SendTo>();

        w.component<Properties>()
            .member<uint16_t>("universeId")
            .member<uint16_t>("usedSize");

        // Channels holds a raw 512-byte DMX buffer — transmitted via ArtNet UDP,
        // not ECS JSON sync. Register as opaque so Flecs skips serialization.
        w.component<Channels>().add(flecs::Sparse);
    }

} // namespace Universe

namespace Artnet::Device {

    flecs::entity create(flecs::entity patch){
        flecs::entity deviceFolder = patch.target<Patch::ArtnetDeviceFolder>();
        flecs::entity newDevice = patch.world().entity()
        .child_of(deviceFolder)
        .add<Is>()
        .add<Settings>();

        select(patch, newDevice);

        return newDevice;
    }

    void iterateInPatch(flecs::entity patch, std::function<void(flecs::entity device, Settings&)> fn){
        flecs::entity deviceFolder = patch.target<Patch::ArtnetDeviceFolder>();
        App::getQueries(patch.world()).artnetDeviceInPatch.set_var("parent", deviceFolder)
        .each([fn](flecs::entity device, Is, Settings& settings){
            fn(device, settings);
        });
    }

    void select(flecs::entity patch, flecs::entity device){
        patch.add<Patch::SelectedArtnetDevice>(device);
    }
    
    flecs::entity getSelected(flecs::entity patch){
        if(!patch.is_valid() || !patch.is_alive()) return flecs::entity::null();
        return patch.target<Patch::SelectedArtnetDevice>();
    }

    void import(flecs::world& w){
        w.component<Is>();
        w.component<SendsUniverse>();

        w.component<Settings>()
            .member<uint32_t>("ipAddress")
            .member<uint16_t>("startUniverse")
            .member<uint16_t>("universeCount");
    }

} // namespace Device

} // namespace PixelMapper
