#include "shared/components/AppComponents.h"
#include "shared/components/PatchComponents.h"
#include "shared/components/CueListComponents.h"
#include "shared/EffectBank.h"
#include "shared/Fixture.h"
#include "shared/Artnet.h"

namespace PixelMapper {

App::AppMode App::g_appMode = App::AppMode::Standalone;

std::atomic<float> App::rtFps{0.0f};
std::atomic<float> App::rtBitrateMbps{0.0f};
char App::rtNetworkStatus[512] = "Not sending yet";
std::mutex App::rtNetworkStatusMutex;
float App::pendingCrossfadeDuration = 0.0f;

Generative::EngineStateUBO App::clientTelemetryUbo{};
std::vector<ColorRGBW> App::clientTelemetryPixels;
std::mutex App::clientTelemetryMutex;
bool App::clientTelemetryDataNew = false;

// Helpers:
flecs::entity App::get(const flecs::world& w) {
    return w.target<Is>();
}

const App::Queries& App::getQueries(const flecs::world& w) {
    return get(w).get<Queries>();
}

void App::importComponents(flecs::world& w) {
    // Register App components
    w.component<Is>();
    w.component<PatchFolder>();
    w.component<SelectedPatch>();
    w.component<UIConfig>();

    // Register Patch components
    w.component<Patch::Is>();
    w.component<Patch::FixtureFolder>();
    w.component<Patch::DmxUniverseFolder>();
    w.component<Patch::ArtnetDeviceFolder>();
    w.component<Patch::SelectedFixture>();
    w.component<Patch::SelectedDmxUniverse>();
    w.component<Patch::SelectedArtnetDevice>();
    w.component<Patch::DmxMapDirty>();
    w.component<Patch::RenderAreaDirty>();
    w.component<Patch::ProgramDirty>();
    w.component<Patch::MultiSelection>();
    w.component<Patch::Settings>();
    w.component<Patch::ScriptData>();
    w.component<Patch::FixtureSetupScript>();
    w.component<Patch::RenderArea>();
    w.component<Patch::GPUResources>();
    w.component<Patch::GPUProgram>();

    // Register CueList components (excluding CueAdvancer system)
    w.component<CueList::Is>();
    w.component<CueList::CueFolder>();
    w.component<CueList::Cue::Is>();
    w.component<CueList::Cue::HoldDuration>();
    w.component<CueList::Cue::FadeDuration>();
    w.component<CueList::Cue::IndexOrder>();
    w.component<CueList::Cue::TargetEffect>();
    w.component<CueList::SessionState>();

    // Register EffectBank components
    w.component<EffectBank::Is>();
    w.component<EffectBank::EffectFolder>();
    w.component<EffectBank::Effect::Is>();
    w.component<EffectBank::Effect::GlslSource>();
    w.component<EffectBank::SessionState>();

    // Register Fixture components
    Fixture::import(w); // Already in shared/Fixture.cpp!

    // Register Artnet components
    Artnet::Universe::import(w); // Already in shared/Artnet.cpp!
    Artnet::Device::import(w);   // Already in shared/Artnet.cpp!

    // Register Generative component types
    w.component<Generative::Is>();
    w.component<Generative::PaletteFolder>();
    w.component<Generative::MotiveFolder>();
    w.component<Generative::Palette::Is>();
    w.component<Generative::Palette::Stops>();
    w.component<Generative::Palette::IsModeB>();
    w.component<Generative::Motive::Is>();
    w.component<Generative::Motive::Params>();
    w.component<Generative::Settings>();

    // Register pair properties
    w.component<Fixture::WithShape>().add(flecs::Exclusive);
    w.component<Patch::SelectedFixture>().add(flecs::Exclusive);
    w.component<Patch::SelectedDmxUniverse>().add(flecs::Exclusive);
    w.component<Patch::SelectedArtnetDevice>().add(flecs::Exclusive);
    w.component<SelectedPatch>().add(flecs::Exclusive);

    // Tree Root (Only instantiate if we are initializing a fresh world and it doesn't exist)
    auto pixelMapper = w.lookup("::PixelMapperApp");
    if (!pixelMapper.is_valid()) {
        pixelMapper = w.entity("PixelMapperApp");
        w.add<Is>(pixelMapper);
        pixelMapper.set<UIConfig>({});
        auto patchFolder = w.entity("Patches").child_of(pixelMapper);
        pixelMapper.add<PatchFolder>(patchFolder);
    }

    // Queries setup
    w.set<Queries>({
        w.query<Patch::Is>(),
        w.query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress, Fixture::Order>(),
        w.query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress, Fixture::Order>(),
        w.query<Fixture::Is, Fixture::PixelData, Fixture::Order>(),
        w.query<Artnet::Universe::Is, Artnet::Universe::Properties>(),
        w.query<Artnet::Device::Is, Artnet::Device::Settings>()
    });
}

} // namespace PixelMapper
