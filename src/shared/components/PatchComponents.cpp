#include "shared/components/PatchComponents.h"
#include "shared/components/AppComponents.h"
#include "shared/components/CueListComponents.h"
#include "shared/EffectBank.h"
#include "shared/Artnet.h"

namespace PixelMapper {
namespace Patch {

    flecs::entity getSelected(flecs::entity pixelMapper){
        return pixelMapper.target<App::SelectedPatch>();
    }
    void select(flecs::entity pixelMapper, flecs::entity patch){
        pixelMapper.add<App::SelectedPatch>(patch);
    }

    int getCount(flecs::entity pixelMapper){
        auto patchFolder = pixelMapper.target<App::PatchFolder>();
        if(!patchFolder.is_valid()) return 0;
        const auto& queries = pixelMapper.get<App::Queries>();
        return queries.patch.set_var("parent", patchFolder).count();
    }

    void iterate(flecs::entity pixelMapper, std::function<void(flecs::entity patch)> fn){
        auto patchFolder = pixelMapper.target<App::PatchFolder>();
        if(!patchFolder.is_valid()) return;
        App::getQueries(pixelMapper.world()).patch.set_var("parent", patchFolder)
        .each([fn](flecs::entity patch, Patch::Is){
            fn(patch);
        });
    }

    flecs::entity create(flecs::entity pixelMapper){
        std::string patchName = "Patch " + std::to_string(getCount(pixelMapper) + 1);

        auto patchFolder = pixelMapper.target<App::PatchFolder>();
        if(!patchFolder.is_valid()) return flecs::entity::null();
        const auto& world = pixelMapper.world();

        static const std::string defaultSetupScript =
            "-- Fixture Setup Script\n"
            "-- Exposes operations on the patch:\n"
            "--   patch:clear_fixtures()\n"
            "--   patch:create_line(name, startX, startY, startZ, endX, endY, endZ, numPixels, channels)\n"
            "--   patch:get_fixtures() -> array of Fixtures\n"
            "--\n"
            "-- Exposes operations on a Fixture:\n"
            "--   f:id() -> number\n"
            "--   f:name() -> string\n"
            "--   f:set_name(name)\n"
            "--   f:get_shape_type() -> string (\"Line\" / \"Circle\" / \"None\")\n"
            "--   f:get_line_properties() -> startX, startY, startZ, endX, endY, endZ\n"
            "--   f:set_line_properties(startX, startY, startZ, endX, endY, endZ)\n"
            "--   f:get_layout() -> pixelCount, channels\n"
            "--   f:set_layout(pixelCount, channels)\n"
            "--   f:get_dmx() -> universe, startAddress\n"
            "--   f:set_dmx(universe, startAddress)\n"
            "--   f:remove()\n"
            "\n"
            "-- Example: clear patch and create 4 parallel line fixtures\n"
            "patch:clear_fixtures()\n"
            "\n"
            "for i = 1, 4 do\n"
            "    local y = (i - 1) * 30\n"
            "    local name = \"Line \" .. i\n"
            "    local f = patch:create_line(name, 0, y, 0, 100, y, 0, 16, 4)\n"
            "    f:set_dmx(0, (i - 1) * 64)\n"
            "end\n";

        auto newPatch = world.entity()
            .add<Patch::Is>()
            .set<Patch::Settings>({})
            .set<Patch::ScriptData>({})
            .set<Patch::FixtureSetupScript>({defaultSetupScript, ""})
            .add<Patch::RenderArea>()
            .set<Patch::GPUResources>({})
            .set<Patch::GPUProgram>({})
            .child_of(patchFolder);

        newPatch.set_name(patchName.c_str());

        auto fixtureFolder = world.entity().child_of(newPatch).set_name("FixtureFolder");
        auto dmxOutputFolder = world.entity().child_of(newPatch).set_name("DmxOutputFolder");
        auto artnetDeviceFolder = world.entity().child_of(newPatch).set_name("ArtnetDeviceFolder");

        newPatch.add<Patch::FixtureFolder>(fixtureFolder);
        newPatch.add<Patch::DmxUniverseFolder>(dmxOutputFolder);
        newPatch.add<Patch::ArtnetDeviceFolder>(artnetDeviceFolder);

        auto cueListFolder = world.entity().child_of(newPatch).set_name("CueListFolder");
        cueListFolder.add<CueList::Is>();
        cueListFolder.set<CueList::SessionState>({});

        auto effectBankFolder = world.entity().child_of(newPatch).set_name("EffectBankFolder");
        effectBankFolder.add<EffectBank::Is>();
        effectBankFolder.set<EffectBank::SessionState>({});

        auto paletteFolder = world.entity().child_of(newPatch).set_name("PaletteFolder");
        paletteFolder.add<Generative::PaletteFolder>();

        auto motiveFolder = world.entity().child_of(newPatch).set_name("MotiveFolder");
        motiveFolder.add<Generative::MotiveFolder>();

        newPatch.add<CueList::CueFolder>(cueListFolder);
        newPatch.add<EffectBank::EffectFolder>(effectBankFolder);
        newPatch.add<Generative::PaletteFolder>(paletteFolder);
        newPatch.add<Generative::MotiveFolder>(motiveFolder);
        newPatch.set<Generative::Settings>({});

        // ── Seed Default Palettes ──
        {
            // 1. Rainbow Wave (Continuous wrap looping)
            std::vector<Generative::ColorStop> stops1 = {
                { {1.0f, 0.0f, 0.0f, 1.0f}, 0.00f, 0.5f, {0.0f, 0.0f} },
                { {1.0f, 1.0f, 0.0f, 1.0f}, 0.17f, 0.5f, {0.0f, 0.0f} },
                { {0.0f, 1.0f, 0.0f, 1.0f}, 0.33f, 0.5f, {0.0f, 0.0f} },
                { {0.0f, 1.0f, 1.0f, 1.0f}, 0.50f, 0.5f, {0.0f, 0.0f} },
                { {0.0f, 0.0f, 1.0f, 1.0f}, 0.67f, 0.5f, {0.0f, 0.0f} },
                { {1.0f, 0.0f, 1.0f, 1.0f}, 0.83f, 0.5f, {0.0f, 0.0f} },
                { {1.0f, 0.0f, 0.0f, 1.0f}, 1.00f, 0.5f, {0.0f, 0.0f} }
            };
            world.entity().child_of(paletteFolder)
                .add<Generative::Palette::Is>()
                .set_name("Rainbow")
                .set<Generative::Palette::Stops>({stops1})
                .set<Generative::Palette::IsModeB>({false});

            // 2. Sunset
            std::vector<Generative::ColorStop> stops2 = {
                { {0.95f, 0.20f, 0.08f, 1.0f}, 0.00f, 0.5f, {0.0f, 0.0f} },
                { {0.85f, 0.05f, 0.40f, 1.0f}, 0.35f, 0.5f, {0.0f, 0.0f} },
                { {0.35f, 0.02f, 0.55f, 1.0f}, 0.70f, 0.5f, {0.0f, 0.0f} },
                { {0.95f, 0.20f, 0.08f, 1.0f}, 1.00f, 0.5f, {0.0f, 0.0f} }
            };
            world.entity().child_of(paletteFolder)
                .add<Generative::Palette::Is>()
                .set_name("Sunset")
                .set<Generative::Palette::Stops>({stops2})
                .set<Generative::Palette::IsModeB>({false});

            // 3. Cyberpunk Neon
            std::vector<Generative::ColorStop> stops3 = {
                { {0.0f, 0.95f, 0.95f, 1.0f}, 0.00f, 0.5f, {0.0f, 0.0f} },
                { {0.95f, 0.0f, 0.85f, 1.0f}, 0.50f, 0.5f, {0.0f, 0.0f} },
                { {0.0f, 0.95f, 0.95f, 1.0f}, 1.00f, 0.5f, {0.0f, 0.0f} }
            };
            world.entity().child_of(paletteFolder)
                .add<Generative::Palette::Is>()
                .set_name("Cyberpunk")
                .set<Generative::Palette::Stops>({stops3})
                .set<Generative::Palette::IsModeB>({false});
        }

        // ── Seed Default Motives ──
        {
            // 1. Calm Ambient
            Generative::Motive::Params calm;
            calm.velocity = 0.15f; calm.complexity = 0.20f; calm.scale = 0.35f;
            calm.distortion = 0.10f; calm.asymmetry = 0.0f; calm.intensity = 0.80f;
            world.entity().child_of(motiveFolder)
                .add<Generative::Motive::Is>()
                .set_name("Calm Ambient")
                .set<Generative::Motive::Params>(calm);

            // 2. Dynamic Noise
            Generative::Motive::Params dyn;
            dyn.velocity = 0.60f; dyn.complexity = 0.75f; dyn.scale = 0.70f;
            dyn.distortion = 0.80f; dyn.asymmetry = 0.5f; dyn.intensity = 1.00f;
            world.entity().child_of(motiveFolder)
                .add<Generative::Motive::Is>()
                .set_name("Dynamic Noise")
                .set<Generative::Motive::Params>(dyn);
        }

        return newPatch;
    }

} // namespace Patch
} // namespace PixelMapper
