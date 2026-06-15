#include "Fixture.h"
#include "Shape.h"
#include "Patch.h"
#include "App.h"
#include <algorithm>
#include <atomic>
#include <mutex>

namespace PixelMapper {
namespace Fixture {

    static std::atomic<int> g_fixtureNameSeq{1};

    void updateFixtureNameCounter(flecs::world w) {
        int maxSeq = 0;
        w.query_builder<Is>().build().each([&](flecs::entity e, Is) {
            const char* nameStr = e.name();
            if (!nameStr) return;
            std::string name = nameStr;
            if (name.rfind("Line Fixture ", 0) == 0) {
                try {
                    int val = std::stoi(name.substr(13));
                    if (val > maxSeq) maxSeq = val;
                } catch (...) {}
            } else if (name.rfind("Circle Fixture ", 0) == 0) {
                try {
                    int val = std::stoi(name.substr(15));
                    if (val > maxSeq) maxSeq = val;
                } catch (...) {}
            }
        });
        g_fixtureNameSeq = maxSeq + 1;
    }

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

        int currentCount = Fixture::getCountWithDmx(patch);
        newFixture.set<Fixture::Order>({currentCount});

        newFixture.set<Fixture::Layout>(layout)
        .set<Fixture::DmxAddress>({0,0});

        select(patch, newFixture);

        return newFixture;
    }

    flecs::entity createLine(flecs::entity patch, glm::vec3 start, glm::vec3 end, int numPixels, int channels) {
        static std::once_flag initFlag;
        std::call_once(initFlag, [&]() {
            updateFixtureNameCounter(patch.world());
        });
        std::string fixtureName = "Line Fixture " + std::to_string(g_fixtureNameSeq++);
        auto newFixture = create(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<WithShape, Shape::Line>({start, end});
        return newFixture;
    }

    flecs::entity createCircle(flecs::entity patch, glm::vec3 center, float radius, int numPixels, int channels){
        static std::once_flag initFlag;
        std::call_once(initFlag, [&]() {
            updateFixtureNameCounter(patch.world());
        });
        std::string fixtureName = "Circle Fixture " + std::to_string(g_fixtureNameSeq++);
        auto newFixture = create(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<WithShape, Shape::Circle>({center, radius});
        return newFixture;
    }

    flecs::entity duplicate(flecs::entity patch, flecs::entity src, glm::vec3 offset) {
        if (!src.is_valid() || !src.is_alive()) return flecs::entity::null();

        const Fixture::Layout& layout = src.get<Fixture::Layout>();
        const Fixture::DmxAddress& dmx = src.get<Fixture::DmxAddress>();

        flecs::entity shapeType = src.target<Fixture::WithShape>();
        flecs::entity newFixture;

        if (shapeType == src.world().id<Shape::Line>()) {
            const Shape::Line& l = src.get<Fixture::WithShape, Shape::Line>();
            newFixture = createLine(patch,
                l.start + offset, l.end + offset,
                layout.pixelCount, layout.channelsPerPixel);
        } else if (shapeType == src.world().id<Shape::Circle>()) {
            const Shape::Circle& c = src.get<Fixture::WithShape, Shape::Circle>();
            newFixture = createCircle(patch,
                c.center + offset, c.radius,
                layout.pixelCount, layout.channelsPerPixel);
        } else {
            return flecs::entity::null();
        }

        // Copy DMX address (user will adjust)
        Fixture::setDmxProperties(newFixture, dmx.universe, dmx.address);
        return newFixture;
    }

    void autoPackDmx(flecs::entity patch) {
        int byteOffset = 0;
        Fixture::iterateWithDmx(patch,
            [&](flecs::entity fixture, const Fixture::Layout& layout, const Fixture::DmxAddress&)
        {
            uint16_t universe = (uint16_t)(byteOffset / 512);
            uint16_t address  = (uint16_t)(byteOffset % 512);
            Fixture::setDmxProperties(fixture, universe, address);
            byteOffset += layout.pixelCount * layout.channelsPerPixel;
        });
        patch.add<Patch::DmxMapDirty>();
        patch.add<Patch::ProgramDirty>();
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
    void iterateWithDmx(flecs::entity patch, std::function<void(flecs::entity fixture, const Fixture::Layout&, const Fixture::DmxAddress&)> fn){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        App::getQueries(patch.world()).fixtureWithDmxInPatch.set_var("parent", fixtureFolder)
        .each([fn](flecs::entity fixture, Fixture::Is, const Fixture::Layout& layout, const Fixture::DmxAddress& dmxAddress, Fixture::Order){
            fn(fixture, layout, dmxAddress);
        });
    }

    void iterateInDmxUniverse(flecs::entity patch, flecs::entity universe, std::function<void(flecs::entity fixture, const Fixture::Layout&, const Fixture::DmxAddress&)> fn){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        App::getQueries(patch.world()).fixtureInDmxUniverse
        .set_var("parent", fixtureFolder)
        .set_var("universe", universe)
        .each([fn](flecs::entity fixture, Fixture::Is, const Fixture::Layout& layout, const Fixture::DmxAddress& dmxAddress, Fixture::Order){
            fn(fixture, layout, dmxAddress);
        });
    }

    void iterateWithPixelData(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::PixelData&)> fn){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        App::getQueries(patch.world()).fixtureWithPixelDataInPatch
        .set_var("parent", fixtureFolder)
        .each([fn](flecs::entity fixture, Fixture::Is, Fixture::PixelData& pixelData, Fixture::Order){
            fn(fixture, pixelData);
        });
    }

    // Read-only overload: does NOT mark PixelData as changed (avoids spurious dirty cascades)
    void iterateWithPixelData(flecs::entity patch, std::function<void(flecs::entity fixture, const Fixture::PixelData&)> fn){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        App::getQueries(patch.world()).fixtureWithPixelDataInPatch
        .set_var("parent", fixtureFolder)
        .each([fn](flecs::entity fixture, Fixture::Is, const Fixture::PixelData& pixelData, Fixture::Order){
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

    void reorder(flecs::entity patch, flecs::entity dragFixture, flecs::entity dropFixture) {
        if (!dragFixture.is_valid() || !dropFixture.is_valid() || dragFixture == dropFixture) return;

        // Collect all fixtures in the current order
        std::vector<flecs::entity> list;
        iterateWithDmx(patch, [&](flecs::entity f, const Layout&, const DmxAddress&){
            list.push_back(f);
        });

        auto dragIt = std::find(list.begin(), list.end(), dragFixture);
        auto dropIt = std::find(list.begin(), list.end(), dropFixture);
        if (dragIt == list.end() || dropIt == list.end()) return;

        // Erase dragFixture from old position and insert at new position
        list.erase(dragIt);
        // Find dropIt again because list size changed
        dropIt = std::find(list.begin(), list.end(), dropFixture);
        list.insert(dropIt, dragFixture);

        // Re-assign indices sequentially
        for (int i = 0; i < (int)list.size(); ++i) {
            list[i].set<Order>({i});
        }

        // Mark patch dirty
        patch.add<Patch::DmxMapDirty>();
        patch.add<Patch::ProgramDirty>();
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
        w.component<Order>();
    }

} // namespace Fixture
} // namespace PixelMapper
