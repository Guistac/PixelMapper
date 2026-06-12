#include "Fixture.h"
#include "Shape.h"
#include "Patch.h"
#include "App.h"
#include <algorithm>

namespace PixelMapper {
namespace Fixture {

    static flecs::entity create(flecs::entity patch, int numPixels, int channels){
        auto fixtureList = patch.target<Patch::FixtureFolder>();
        auto newFixture = patch.world().entity()
            .child_of(fixtureList)
            .add<Fixture::Is>()
            .add<Fixture::PixelData>();

        Fixture::Layout layout{
            .pixelCount = numPixels,
            .channelsPerPixel = channels
        };

        newFixture.set<Fixture::Layout>(layout)
        .set<Fixture::DmxAddress>({0,0});

        select(patch, newFixture);

        return newFixture;
    }

    flecs::entity createLine(flecs::entity patch, glm::vec3 start, glm::vec3 end, int numPixels, int channels) {
        std::string fixtureName = "Line Fixture " + std::to_string(Fixture::getCountWithDmx(patch) + 1);
        auto newFixture = create(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<WithShape, Shape::Line>({start, end});
        return newFixture;
    }

    flecs::entity createCircle(flecs::entity patch, glm::vec3 center, float radius, int numPixels, int channels){
        std::string fixtureName = "Circle Fixture " + std::to_string(Fixture::getCountWithDmx(patch) + 1);
        auto newFixture = create(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<WithShape, Shape::Circle>({center, radius});
        return newFixture;
    }

    void select(flecs::entity patch, flecs::entity fixture){
        patch.add<Patch::SelectedFixture>(fixture);
    }
    flecs::entity getSelected(flecs::entity patch){
        if(!patch.is_valid() || !patch.is_alive()) return flecs::entity::null();
        return patch.target<Patch::SelectedFixture>();
    }
    void clearSelection(flecs::entity patch){
        patch.remove<Patch::SelectedFixture>(flecs::Wildcard);
    }

    void setDmxProperties(flecs::entity fixture, uint16_t universe, uint16_t startAddress){
        if(!fixture.is_valid()) return;
        auto* dmx = fixture.try_get_mut<Fixture::DmxAddress>();
        if(!dmx) return;
        dmx->universe = universe;
        dmx->address = startAddress;
    }

    flecs::entity getPatch(flecs::entity fixture) {
        if (!fixture.is_valid() || !fixture.is_alive())
            return flecs::entity::null();
        flecs::entity fixtureList = fixture.parent();
        if (!fixtureList.is_valid())
            return flecs::entity::null();
        flecs::entity patch = fixtureList.parent();
        if (patch.is_valid() && patch.has<Patch::Is>())
            return patch;
        return flecs::entity::null();
    }

    int getCountWithDmx(flecs::entity patch){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return 0;
        return App::getQueries(patch.world()).fixtureWithDmxInPatch.set_var("parent", fixtureFolder).count();
    }
    void iterateWithDmx(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        App::getQueries(patch.world()).fixtureWithDmxInPatch.set_var("parent", fixtureFolder)
        .each([fn](flecs::entity fixture, Fixture::Is, Fixture::Layout& layout, Fixture::DmxAddress& dmxAddress){
            fn(fixture, layout, dmxAddress);
        });
    }

    void iterateInDmxUniverse(flecs::entity patch, flecs::entity universe, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        App::getQueries(patch.world()).fixtureInDmxUniverse
        .set_var("parent", fixtureFolder)
        .set_var("universe", universe)
        .each([fn](flecs::entity fixture, Fixture::Is, Fixture::Layout& layout, Fixture::DmxAddress& dmxAddress){
            fn(fixture, layout, dmxAddress);
        });
    }

    void iterateWithPixelData(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::PixelData&)> fn){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        App::getQueries(patch.world()).fixtureWithPixelDataInPatch
        .set_var("parent", fixtureFolder)
        .each([fn](flecs::entity fixture, Fixture::Is, Fixture::PixelData& pixelData){
            fn(fixture, pixelData);
        });
    }

    void setPixelPositions(PixelData& pd, std::function<glm::vec3(float range, int index, size_t count)> pos_func) {
        int count = pd.positions.size();
        for(int i = 0; i < count; i++){
            float range = count > 1 ? (float)i / (float)(count - 1) : 0.0f;
            pd.positions[i] = pos_func(range, i, count);
        }
    }

    void writeColorsToUniverse(
        const std::vector<ColorRGBW>& colors,
        uint8_t* dmxChannels,
        int universeId,
        int fixtureStartUniverse,
        int fixtureStartAddress,
        int channelsPerColor)
    {
        int universePixelStartChannel = (universeId - fixtureStartUniverse) * 512 - fixtureStartAddress;
        int universePixelEndChannel = universePixelStartChannel + 511;

        int firstPixel = std::max(0, universePixelStartChannel) / channelsPerColor;
        int lastPixel = std::min((int)colors.size() - 1, universePixelEndChannel / channelsPerColor);

        for (int i = firstPixel; i <= lastPixel; ++i) {
            const uint8_t* colorPtr = reinterpret_cast<const uint8_t*>(&colors[i]);
            for (int ch = 0; ch < channelsPerColor; ++ch) {
                int byteGlobalPos = fixtureStartAddress + (i * channelsPerColor) + ch;
                int byteUniverse = fixtureStartUniverse + (byteGlobalPos / 512);

                if (byteUniverse == universeId) {
                    int byteLocalPos = byteGlobalPos % 512;
                    dmxChannels[byteLocalPos] = colorPtr[ch];
                }
            }
        }
    }

    void import(flecs::world& w){
        w.component<Is>();
        w.component<WithShape>();
        w.component<InUniverse>();
        w.component<LayoutDirty>();
        w.component<PixelPositionsDirty>();
        w.component<Layout>();
        w.component<DmxAddress>();
        w.component<PixelData>();
    }

} // namespace Fixture
} // namespace PixelMapper
