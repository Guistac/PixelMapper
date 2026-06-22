#include "shared/components/AppComponents.h"
#include "shared/components/PatchComponents.h"
#include "shared/components/CueListComponents.h"
#include "shared/EffectBank.h"
#include "shared/Fixture.h"
#include "shared/Artnet.h"
#include "shared/Shape.h"
#include <glm/glm.hpp>

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
    // ── glm::vec3 base type ─────────────────────────────────────────────────
    // Must be registered first so it can be used as a member type in RenderArea,
    // Shape::Line, and Shape::Circle.
    w.component<glm::vec3>()
        .member<float>("x")
        .member<float>("y")
        .member<float>("z");

    // ── Shape component types ───────────────────────────────────────────────
    w.component<Shape::Line>()
        .member<glm::vec3>("start")
        .member<glm::vec3>("end");

    w.component<Shape::Circle>()
        .member<glm::vec3>("center")
        .member<float>("radius");

    // ── App components ──────────────────────────────────────────────────────
    w.component<Is>();
    w.component<PatchFolder>();
    w.component<SelectedPatch>();
    // UIConfig is local GUI state — not synced to clients via ECS JSON. Opaque.
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
    // Settings contains char[] arrays — register opaque; mutated via EntityMutation RPC.
    w.component<Patch::Settings>();
    // ScriptData / FixtureSetupScript hold std::string source — synced via ApplyScript RPC.
    w.component<Patch::ScriptData>();
    w.component<Patch::FixtureSetupScript>();
    // RenderArea is a simple POD bbox — reflected so clients can render the canvas.
    w.component<Patch::RenderArea>()
        .member<glm::vec3>("min")
        .member<glm::vec3>("max");
    // GPU handles must never cross the network — registered opaque.
    w.component<Patch::GPUResources>();
    w.component<Patch::GPUProgram>();

    // Register CueList components
    w.component<CueList::Is>();
    w.component<CueList::CueFolder>();
    w.component<CueList::Cue::Is>();
    w.component<CueList::Cue::HoldDuration>().member<float>("value");
    w.component<CueList::Cue::FadeDuration>().member<float>("value");
    w.component<CueList::Cue::IndexOrder>()
        .member<int>("value");
    w.component<CueList::Cue::TargetEffect>();
    w.component<CueList::SessionState>();

    // Register EffectBank components
    w.component<EffectBank::Is>();
    w.component<EffectBank::EffectFolder>();
    w.component<EffectBank::Effect::Is>();
    // GlslSource holds std::string source — synced via ApplyScript RPC, not ECS JSON.
    w.component<EffectBank::Effect::GlslSource>();
    w.component<EffectBank::SessionState>();

    // Register Fixture components
    Fixture::import(w);

    // Register Artnet components
    Artnet::Universe::import(w);
    Artnet::Device::import(w);

    // Register Generative component types
    w.component<Generative::Is>();
    w.component<Generative::PaletteFolder>();
    w.component<Generative::MotiveFolder>();
    w.component<Generative::Palette::Is>();
    // Palette::Stops contains std::vector<ColorStop> — not ECS-JSON synced.
    w.component<Generative::Palette::Stops>();
    w.component<Generative::Palette::IsModeB>()
        .member<bool>("value");
    w.component<Generative::Motive::Is>();
    // Motive::Params: reflect the 6 scalar animation parameters.
    // wanderAmp[6] / wanderFreq[6] are raw C arrays — Flecs cannot reflect them
    // without flecs_meta macros; omit them (they have safe runtime defaults).
    w.component<Generative::Motive::Params>()
        .member<float>("velocity")
        .member<float>("complexity")
        .member<float>("scale")
        .member<float>("distortion")
        .member<float>("asymmetry")
        .member<float>("intensity");
    // Generative::Settings: many bool/float fields; opaque for now.
    w.component<Generative::Settings>();

    // Register pair properties
    w.component<Fixture::WithShape>().add(flecs::Exclusive);
    w.component<Patch::SelectedFixture>().add(flecs::Exclusive);
    w.component<Patch::SelectedDmxUniverse>().add(flecs::Exclusive);
    w.component<Patch::SelectedArtnetDevice>().add(flecs::Exclusive);
    w.component<SelectedPatch>().add(flecs::Exclusive);

    // Tree Root
    auto pixelMapper = w.lookup("::PixelMapperApp");
    if (!pixelMapper.is_valid()) {
        pixelMapper = w.entity("PixelMapperApp");
        w.add<Is>(pixelMapper);
        pixelMapper.set<UIConfig>({});
        auto patchFolder = w.entity("Patches").child_of(pixelMapper);
        pixelMapper.add<PatchFolder>(patchFolder);
    }

    initQueries(w);
}

static int compareOrder(flecs::entity_t e1, const Fixture::Order* o1,
                       flecs::entity_t e2, const Fixture::Order* o2) {
    if (!o1 && !o2) return 0;
    if (!o1) return 1;
    if (!o2) return -1;
    return (o1->index > o2->index) - (o1->index < o2->index);
}

static int compareUniverse(flecs::entity_t e1, const Artnet::Universe::Properties* p1,
                          flecs::entity_t e2, const Artnet::Universe::Properties* p2) {
    if (!p1 && !p2) return 0;
    if (!p1) return 1;
    if (!p2) return -1;
    return (p1->universeId > p2->universeId) - (p1->universeId < p2->universeId);
}

void App::initQueries(flecs::world& w) {
    auto pixelMapper = w.lookup("::PixelMapperApp");
    if (!pixelMapper.is_valid()) {
        pixelMapper = w.entity("PixelMapperApp");
        w.add<Is>(pixelMapper);
        pixelMapper.set<UIConfig>({});
        auto patchFolder = w.entity("Patches").child_of(pixelMapper);
        pixelMapper.add<PatchFolder>(patchFolder);
    }

    Queries queries{
        .patch = w.query_builder<Patch::Is>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .fixtureWithDmxInPatch = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress, Fixture::Order>()
            .term().first(flecs::ChildOf).second("$parent")
            .order_by<Fixture::Order>(compareOrder)
            .build(),
        .fixtureInDmxUniverse = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress, Fixture::Order>()
            .term().first(flecs::ChildOf).second("$parent")
            .with<Fixture::InUniverse>().second("$universe")
            .order_by<Fixture::Order>(compareOrder)
            .build(),
        .fixtureWithPixelDataInPatch = w.query_builder<Fixture::Is, Fixture::PixelData, Fixture::Order>()
            .term().first(flecs::ChildOf).second("$parent")
            .order_by<Fixture::Order>(compareOrder)
            .build(),
        .dmxUniverseInPatch = w.query_builder<Artnet::Universe::Is, Artnet::Universe::Properties>()
            .term().first(flecs::ChildOf).second("$parent")
            .order_by<Artnet::Universe::Properties>(compareUniverse)
            .build(),
        .artnetDeviceInPatch = w.query_builder<Artnet::Device::Is, Artnet::Device::Settings>()
            .term().first(flecs::ChildOf).second("$parent")
            .build()
    };

    w.set<Queries>(queries);
    pixelMapper.set<Queries>(queries);
}

} // namespace PixelMapper
