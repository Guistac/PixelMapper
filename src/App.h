#pragma once
#include <flecs.h>
#include <mutex>
#include <atomic>
#include "Patch.h"
#include "Fixture.h"
#include "Artnet.h"

struct GLFWwindow;
struct PatchProgram;

namespace PixelMapper {

namespace Gui {
    void import(flecs::world& w);
}

namespace App {
    extern GLFWwindow* sharedContextWindow;
    extern PatchProgram* currentPatchProgram;
    extern std::mutex patchProgramLock;
    struct Is {};
    struct PatchFolder {};
    struct SelectedPatch {};

    struct Queries {
        flecs::query<Patch::Is> patch;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress, Fixture::Order> fixtureWithDmxInPatch;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress, Fixture::Order> fixtureInDmxUniverse;
        flecs::query<Fixture::Is, Fixture::PixelData, Fixture::Order> fixtureWithPixelDataInPatch;
        flecs::query<Artnet::Universe::Is, Artnet::Universe::Properties> dmxUniverseInPatch;
        flecs::query<Artnet::Device::Is, Artnet::Device::Settings> artnetDeviceInPatch;
    };

    void import(flecs::world& w);
    void terminate();
    flecs::entity get(const flecs::world& w);
    const Queries& getQueries(const flecs::world& w);

    /// Real-time thread stats (updated by the RT thread, read by the GUI)
    extern std::atomic<float> rtFps;         ///< RT render/send cycles per second
    extern std::atomic<float> rtBitrateMbps; ///< ArtNet bits per second (Mbit/s)

    /// Set by the GUI/CueList before adding ProgramDirty to request a crossfade.
    /// Consumed (and reset to 0) by pushNewProgram(). Thread-safe: only written
    /// on the main thread (same thread that calls pushNewProgram via compile system).
    extern float pendingCrossfadeDuration;
}

} // namespace PixelMapper
