#include "PixelMapper.h"

#include <algorithm>
#include <iostream>
#include <iomanip>

#include "utils/FlecsUtils.h"

#include <imgui.h>


namespace PixelMapper{


namespace App{
    flecs::entity get(const flecs::world& w){
        return w.target<Is>();
    };
    struct Queries{
        flecs::query<Patch::Is> patch;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress> fixtureWithDmxInPatch;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress> fixtureInDmxUniverse;
        flecs::query<Fixture::Is, Fixture::PixelData> fixtureWithPixelDataInPatch;
        flecs::query<Artnet::Universe::Is, Artnet::Universe::Properties> dmxUniverseInPatch;
    };
    const Queries& getQueries(const flecs::world& w){
        return get(w).get<Queries>();
    }
}//namespace App


namespace Patch{

    flecs::entity create(flecs::entity pixelMapper){
        std::string patchName = "Patch " + std::to_string(getCount(pixelMapper) + 1);

        auto patchFolder = pixelMapper.target<App::PatchFolder>();
        if(!patchFolder.is_valid()) return flecs::entity::null();
        const auto& world = pixelMapper.world();

        auto newPatch = world.entity()
            .add<Patch::Is>()
            .add<Patch::Settings>()
            .add<Patch::RenderArea>()
            .child_of(patchFolder);

        newPatch.set_name(patchName.c_str());

        auto fixtureFolder = world.entity("FixtureFolder").child_of(newPatch);
        auto dmxOutputFolder = world.entity("DmxOutputFolder").child_of(newPatch);

        newPatch.add<Patch::FixtureFolder>(fixtureFolder);
        newPatch.add<Patch::DmxUniverseFolder>(dmxOutputFolder);
        
        return newPatch;
    }

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

}//namespace Patch


namespace Fixture{

    flecs::entity create(flecs::entity patch, int numPixels, int channels){
        auto fixtureList = patch.target<Patch::FixtureFolder>();
        auto newFixture = patch.world().entity()
            .child_of(fixtureList)
            .add<Fixture::Is>()
            .add<Fixture::PixelData>();

        Fixture::Layout layout{
            .pixelCount = numPixels,
            .channelsPerPixel = channels
        };

        newFixture.set<Fixture::Layout>(layout) //triggers pixel resize observer
        .set<Fixture::DmxAddress>({0,0});

        return newFixture;
    }

    flecs::entity createLine(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels, int channels) {
        std::string fixtureName = "Line Fixture " + std::to_string(Fixture::getCountWithDmx(patch) + 1);
        auto newFixture = create(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<WithShape, Shape::Line>({start, end});
        return newFixture;
    }

    flecs::entity createCircle(flecs::entity patch, glm::vec2 center, float radius, int numPixels, int channels){
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
        //inside the current universe, get the possible channels relative to the fixture start
        int universePixelStartChannel = (universeId - fixtureStartUniverse) * 512 - fixtureStartAddress;
        int universePixelEndChannel = universePixelStartChannel + 511;

        //divide by channelsPerColor to get the actual start and end pixels in this universe
        int firstPixel = std::max(0, universePixelStartChannel) / channelsPerColor;
        int lastPixel = std::min((int)colors.size() - 1, universePixelEndChannel / channelsPerColor);

        //only iterate on the pixels that concern this universe
        for (int i = firstPixel; i <= lastPixel; ++i) {
            const uint8_t* colorPtr = reinterpret_cast<const uint8_t*>(&colors[i]);

            //for each pixel, interate on channelcount
            for (int ch = 0; ch < channelsPerColor; ++ch) {

                // Get the position of this byte relative to the very first channel (0) of the fixture's start universe.
                int byteGlobalPos = fixtureStartAddress + (i * channelsPerColor) + ch;
                //then use this to get the bytes actual universe
                int byteUniverse = fixtureStartUniverse + (byteGlobalPos / 512);

                //if the byte is in the current universe, write it to the corresponding dmx channel
                if (byteUniverse == universeId) {
                    int byteLocalPos = byteGlobalPos % 512;
                    dmxChannels[byteLocalPos] = colorPtr[ch];
                }
            }
        }
    }

}//namespace Fixture



namespace Artnet::Universe{
    void iterate(flecs::entity patch, std::function<void(flecs::entity dmxUniverse, Artnet::Universe::Properties&)> fn){
        auto dmxUniverseFolder = patch.target<Patch::DmxUniverseFolder>();
        if(!dmxUniverseFolder.is_valid()) return;
        App::getQueries(patch.world()).dmxUniverseInPatch.set_var("parent", dmxUniverseFolder)
        .each([fn](flecs::entity universe, Artnet::Universe::Is, Artnet::Universe::Properties& properties){
            fn(universe, properties);
        });
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
        return patch.world().entity()
            .child_of(dmxUniverseFolder)
            .set_name(univName.c_str())
            .add<Is>()
            .add<Channels>()
            .set<Properties>({universeId, 0});
    }

};//namespace Artnet::Universe


namespace Patch{
    void import(flecs::world& w){
        w.component<Is>();
        w.component<FixtureFolder>();
        w.component<DmxUniverseFolder>();
        w.component<SelectedFixture>();
        w.component<SelectedDmxUniverse>();
        w.component<DmxMapDirty>();
        w.component<RenderAreaDirty>();
        w.component<Settings>();
        w.component<RenderArea>();
    }
}
namespace Fixture{
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
}
namespace Artnet::Universe{
    void import(flecs::world& w){
        w.component<Is>();
        w.component<SendTo>();
        w.component<Properties>().add(flecs::Sparse);
        w.component<Channels>().add(flecs::Sparse);
    }
}
namespace Shape{
    void import(flecs::world& w){
        w.component<Line>();
        w.component<Circle>();
    }
}


void App::import(flecs::world& w){

    //——————————————————— COMPONENTS ——————————————————————

    w.component<Is>();
    w.component<PatchFolder>();
    w.component<SelectedPatch>();
    Patch::import(w);
    Fixture::import(w);
    Artnet::Universe::import(w);
    Shape::import(w);

    //————————————————— PAIR PROPERTIES ———————————————————

    w.component<Fixture::WithShape>().add(flecs::Exclusive);
    w.component<Patch::SelectedFixture>().add(flecs::Exclusive);
    w.component<Patch::SelectedDmxUniverse>().add(flecs::Exclusive);
    w.component<SelectedPatch>().add(flecs::Exclusive);

    //———————————————————— TREE ROOT ——————————————————————

    auto pixelMapper = w.entity("PixelMapperApp");
    w.add<Is>(pixelMapper);
    auto patchFolder = w.entity("Patches").child_of(pixelMapper);
    pixelMapper.add<PatchFolder>(patchFolder);

    //————————————————————— QUERIES ———————————————————————

    Queries queries{
        .patch = w.query_builder<Patch::Is>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .fixtureWithDmxInPatch = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .fixtureInDmxUniverse = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress>()
            .term().first(flecs::ChildOf).second("$parent")
            .with<Fixture::InUniverse>().second("$universe")
            .build(),
        .fixtureWithPixelDataInPatch = w.query_builder<Fixture::Is, Fixture::PixelData>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .dmxUniverseInPatch = w.query_builder<Artnet::Universe::Is, Artnet::Universe::Properties>()
            .term().first(flecs::ChildOf).second("$parent")
            .build()
    };
    pixelMapper.set<Queries>(queries);

    //———————————————————— OBSERVERS ——————————————————————

    w.observer<Fixture::Layout>("ObserveFixtureLayout").event(flecs::OnSet)
    .with<Fixture::Is>()
    .each([](flecs::entity e, Fixture::Layout& l) {
        l.pixelCount = std::clamp<int>(l.pixelCount, 1, INT_MAX);
        l.channelsPerPixel = std::clamp<int>(l.channelsPerPixel, 1, 4);
        e.add<Fixture::LayoutDirty>();
    });
    
    w.observer<Fixture::DmxAddress>("ObserveFixtureDmxAddress").event(flecs::OnSet)
    .with<Fixture::Is>()
    .each([](flecs::entity fixture, Fixture::DmxAddress& dmx){
        dmx.address = std::clamp<uint16_t>(dmx.address, 0, 511);
        dmx.universe = std::clamp<uint16_t>(dmx.universe, 0, 32767);
        Fixture::getPatch(fixture).add<Patch::DmxMapDirty>();
    });

    //————————————————————— SYSTEMS ———————————————————————
    
    w.system<Fixture::Layout, Fixture::PixelData>("UpdateFixtureLayout").with<Fixture::LayoutDirty>()
    .kind(flecs::OnLoad)
    .with<Fixture::Is>()
    .immediate()
    .each([](flecs::entity fixture, Fixture::Layout& l, Fixture::PixelData& pd){
        pd.positions.resize(l.pixelCount);
        pd.colors.resize(l.pixelCount);
        fixture.remove<Fixture::LayoutDirty>();
        fixture.add<Fixture::PixelPositionsDirty>(); //recalculate pixel positions
        Fixture::getPatch(fixture).add<Patch::DmxMapDirty>(); //adjust the dmx output map
    });


    w.system<Fixture::PixelData>("UpdateLinePixelPositions").with<Fixture::WithShape, Shape::Line>()
    .kind(flecs::PostLoad)
    .with<Fixture::PixelPositionsDirty>()
    .with<Fixture::Is>()
    .immediate()
    .each([](flecs::entity fixture, Fixture::PixelData& pd) {
        const Shape::Line& line = fixture.get<Fixture::WithShape,Shape::Line>();
        Fixture::setPixelPositions(pd,
            [&](float range, int index, size_t count) -> glm::vec3{
                glm::vec2 out = line.start + range * (line.end - line.start);
                return glm::vec3(out.x, out.y, 0.0);
        });
        fixture.remove<Fixture::PixelPositionsDirty>();
        Fixture::getPatch(fixture).add<Patch::RenderAreaDirty>();
    });


    w.system<Fixture::PixelData>("UpdateCirclePixelPositions").with<Fixture::WithShape, Shape::Circle>()
    .kind(flecs::PostLoad)
    .with<Fixture::PixelPositionsDirty>()
    .with<Fixture::Is>()
    .immediate()
    .each([](flecs::entity fixture, Fixture::PixelData& pd) {
        const Shape::Circle& circle = fixture.get<Fixture::WithShape,Shape::Circle>();
        Fixture::setPixelPositions(pd,
            [&](float range, int index, size_t count) -> glm::vec3{
                float angle = float(index) / float(count) * M_PI * 2.0;
                glm::vec2 out{
                    circle.center.x + cosf(angle) * circle.radius,
                    circle.center.y + sinf(angle) * circle.radius
                };
                return glm::vec3(out.x, out.y, 0.0);
        });
        fixture.remove<Fixture::PixelPositionsDirty>();
        Fixture::getPatch(fixture).add<Patch::RenderAreaDirty>();
    });


    w.system<Patch::RenderArea>("UpdateRenderArea").with<Patch::RenderAreaDirty>()
    .kind(flecs::PreUpdate)
    .with<Patch::Is>()
    .immediate()
    .each([](flecs::entity patch, Patch::RenderArea& ra){
        glm::vec3 min(FLT_MAX);
        glm::vec3 max(-FLT_MAX);
        bool b_hasPixels = false;
        Fixture::iterateWithPixelData(patch,
            [&](flecs::entity fixture, Fixture::PixelData& pd){
                for(auto& p : pd.positions){
                    min = glm::min(min, p);
                    max = glm::max(max, p);
                    b_hasPixels = true;
                }
        });
        if(b_hasPixels){
            ra.min = min;
            ra.max = max;
        }
        patch.remove<Patch::RenderAreaDirty>();
    });


    w.system<Patch::Is>("UpdateDmxOutputMap").with<Patch::DmxMapDirty>()
    .kind(flecs::PreUpdate)
    .immediate()
    .each([](flecs::entity patch, Patch::Is){

        std::unordered_map<uint16_t, flecs::entity> univsByNumber;
        std::unordered_map<flecs::entity, std::vector<uint16_t>, EntityHasher> univsByFixture;

        //compile a list of universes that are needed to cover all fixtures
        Fixture::iterateWithDmx(patch,
            [&](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress dmxAddress){
                int fixtureUniverseCount = 1;
                int channels = dmxAddress.address + layout.pixelCount * layout.channelsPerPixel;
                while(channels > 512){ fixtureUniverseCount++; channels -= 512; }
                
                auto& fixtureUnivNumbers = univsByFixture[fixture];
                for(int i = 0; i < fixtureUniverseCount; i++){
                    univsByNumber[dmxAddress.universe + i] = flecs::entity::null();
                    fixtureUnivNumbers.push_back(dmxAddress.universe + i);
                }
        });

        //compare with the current list of universes, decide what to keep and delete
        std::vector<flecs::entity> toDelete;
        Artnet::Universe::iterate(patch,
            [&](flecs::entity universe, Artnet::Universe::Properties& properties){
                //if not in new list, mark for deletion
                if(univsByNumber.count(properties.universeId) == 0)
                    toDelete.push_back(universe);
                //else store in map
                else univsByNumber[properties.universeId] = universe;
        });
        
        //Delete and add universes
        for (auto e : toDelete) e.destruct();
        for(auto& [id, universe] : univsByNumber){
            if(!universe.is_valid()){
                flecs::entity newUniverse = Artnet::Universe::create(patch, id);
                if(newUniverse.is_valid()) universe = newUniverse;
            }
        }

        //Rebuild Fixture-InUniverse relationships
        for(const auto& [fixture, univList] : univsByFixture){
            fixture.remove<Fixture::InUniverse>(flecs::Wildcard);
            for(auto id : univList){
                fixture.add<Fixture::InUniverse>(univsByNumber[id]);
            }
        }

        patch.remove<Patch::DmxMapDirty>();
    });

    w.system<>("TestRender")
    .kind(flecs::OnUpdate)
    .immediate()
    .run([](flecs::iter& it){
        flecs::entity app = get(it.world());
        flecs::entity selectedPatch = Patch::getSelected(app);
        const auto& renderArea = selectedPatch.get<Patch::RenderArea>();
        glm::vec3 center = (renderArea.max + renderArea.min) * 0.5f;
        float time = ImGui::GetTime();
        Fixture::iterateWithPixelData(selectedPatch,
            [&](flecs::entity fixture, Fixture::PixelData& pixelData){
                if(pixelData.colors.size() != pixelData.positions.size()) return;
                for(int i = 0; i < pixelData.positions.size(); i++){
                    const auto& pos = pixelData.positions[i];
                    float dist = glm::distance(pos, center);
                    float br = std::sin((dist - time * 100.0) / 30.0);
                    uint8_t out = br > 0 ? br * 255.0 : 0;
                    pixelData.colors[i] = ColorRGBW{
                        .r = out,
                        .g = out,
                        .b = out,
                        .w = out
                    };
                }
        });
    });

    w.system<>("WriteArtnetOutput")
    .kind(flecs::OnValidate)
    .immediate()
    .run([](flecs::iter& it) {
        flecs::entity app = get(it.world());
        flecs::entity selectedPatch = Patch::getSelected(app);
        Fixture::iterateWithDmx(selectedPatch,
            [&](flecs::entity fixture, Fixture::Layout& l, Fixture::DmxAddress& a){
                int startUniverse = a.universe;
                int startAddress = a.address;
                const auto& pixelData = fixture.get<Fixture::PixelData>();

                fixture.each<Fixture::InUniverse>(
                    [&](flecs::entity universe){
                        auto* channels = universe.try_get_mut<Artnet::Universe::Channels>();
                        const auto* props = universe.try_get<Artnet::Universe::Properties>();
                        if(!channels || !props) return;
                        Fixture::writeColorsToUniverse(
                            pixelData.colors,
                            channels->channels,
                            props->universeId,
                            startUniverse,
                            startAddress,
                            l.channelsPerPixel);
                });
        });
    });

    /*
    w.system<>("PrintArtnetData")
    .kind(flecs::PostUpdate)
    .immediate()
    .run([](flecs::iter& it){
        flecs::entity app = get(it.world());
        flecs::entity selectedPatch = Patch::getSelected(app);
        Artnet::Universe::iterate(selectedPatch,
            [](flecs::entity universe, const Artnet::Universe::Properties& props){
                const auto& channels = universe.get<Artnet::Universe::Channels>();
                std::cout << "Universe " << props.universeId << " :" << std::endl;
                for(int i = 0; i < 512; i++) std::cout << std::setfill('0') << std::setw(3) << static_cast<int>(channels.channels[i]) << " ";
                std::cout << std::endl;
        });
    });
    */

};//App::Import()

};//namespace PixelMapper