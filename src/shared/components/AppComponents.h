#pragma once
#include <flecs.h>
#include <mutex>
#include <atomic>
#include <memory>
#include <vector>
#include "shared/Fixture.h"
#include "shared/Artnet.h"
#include "shared/components/PatchComponents.h"

namespace PixelMapper {

namespace App {
    enum class AppMode {
        Standalone,
        Server,
        Client
    };
    extern AppMode g_appMode;

    struct Is {};
    struct PatchFolder {};
    struct SelectedPatch {};

    struct UIConfig {
        int currentLayout = 0; // GuiLayout: PatchEditing = 0, EffectsControl = 1
        bool showFixturesWindow = true;
        bool showPatchEditor = true;
        bool showArtnetData = true;
        bool showNetworkSettings = true;
        bool showArtnetDevices = true;
        bool showScriptEditor = false;
        bool showCuesWindow = false;
        bool showOfflinePreviewWindow = false;
        bool showEffectBankWindow = false;
        bool showFixtureSetupScriptWindow = false;
        bool showPalettesWindow = false;
        bool showMotivesWindow = false;
        bool showGenerativeDashboardWindow = false;
        bool showGenerativeTelemetryWindow = false;
        bool patchLocked = false;
        float previewOpacity = 1.0f;
        bool showGrid = true;
        int editingCueIndex = -1;
        int editingBankIndex = -1;
        bool showFixtures = true;
        bool showPixels = true;
        bool showFrame = true;
        bool autoZoom = false;
        float pixelSize = 4.0f;
        bool canvas3dMode = false;
        float canvasRotationX = 0.0f;
        float canvasRotationY = 0.0f;
        bool editorPreviewOverrideActive = false;
        int editorPreviewOverridePaletteIdx = -1;
    };

    struct Queries {
        flecs::query<Patch::Is> patch;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress, Fixture::Order> fixtureWithDmxInPatch;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress, Fixture::Order> fixtureInDmxUniverse;
        flecs::query<Fixture::Is, Fixture::PixelData, Fixture::Order> fixtureWithPixelDataInPatch;
        flecs::query<Artnet::Universe::Is, Artnet::Universe::Properties> dmxUniverseInPatch;
        flecs::query<Artnet::Device::Is, Artnet::Device::Settings> artnetDeviceInPatch;
    };

    void importComponents(flecs::world& w);
    void initQueries(flecs::world& w);

    extern std::shared_ptr<PatchProgram> currentPatchProgram;

    flecs::entity get(const flecs::world& w);
    const Queries& getQueries(const flecs::world& w);

    /// Real-time thread stats (updated by the RT thread, read by the GUI)
    extern std::atomic<float> rtFps;         ///< RT render/send cycles per second
    extern std::atomic<float> rtBitrateMbps; ///< ArtNet bits per second (Mbit/s)
    extern char rtNetworkStatus[512];
    extern std::mutex rtNetworkStatusMutex;

    /// Set by the GUI/CueList before adding ProgramDirty to request a crossfade.
    /// Consumed (and reset to 0) by pushNewProgram(). Thread-safe: only written
    /// on the main thread (same thread that calls pushNewProgram via compile system).
    extern float pendingCrossfadeDuration;

    // Client-side Telemetry Cache (UDP reassembly targets)
    extern Generative::EngineStateUBO clientTelemetryUbo;
    extern std::vector<ColorRGBW> clientTelemetryPixels;
    extern std::mutex clientTelemetryMutex;
    extern bool clientTelemetryDataNew;
}

} // namespace PixelMapper
