#include "PixelMapper.h"

#include <algorithm>
#include <thread>
#include <atomic>

#include <iostream>
#include <iomanip>

#include "utils/FlecsUtils.h"
#include "utils/Profiling.h"

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
        flecs::query<Artnet::Device::Is, Artnet::Device::Settings> artnetDeviceInPatch;
    };
    const Queries& getQueries(const flecs::world& w){
        return get(w).get<Queries>();
    }


    std::thread rtPatchRunner;
    PatchProgram* currentPatchProgram;
    std::mutex patchProgramLock;
    bool b_rtPatchRunner = false;

    void pushNewProgram(PatchProgram* newProg){
        patchProgramLock.lock();
        PatchProgram* oldProgram = currentPatchProgram;
        currentPatchProgram = newProg;
        patchProgramLock.unlock();
        delete oldProgram;
    }

    void runPatch(){
        b_rtPatchRunner = true;
        while(b_rtPatchRunner){
            auto start = std::chrono::steady_clock::now();
            patchProgramLock.lock();
            if(currentPatchProgram){
                render(App::currentPatchProgram);
                encode(App::currentPatchProgram);
            }
            patchProgramLock.unlock();
            std::this_thread::sleep_until(start + std::chrono::milliseconds(3));
        }
    }

    void terminate(){
        b_rtPatchRunner = false;
        if(rtPatchRunner.joinable()) rtPatchRunner.join();
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
        auto artnetDeviceFolder = world.entity("ArtnetDeviceFolder").child_of(newPatch);

        newPatch.add<Patch::FixtureFolder>(fixtureFolder);
        newPatch.add<Patch::DmxUniverseFolder>(dmxOutputFolder);
        newPatch.add<Patch::ArtnetDeviceFolder>(artnetDeviceFolder);

        select(pixelMapper, newPatch);
        
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

};//namespace Artnet::Universe


namespace Artnet::Device{

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

};


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
    void Line_setPixelPositions(const void* shapeProps, std::vector<glm::vec3>& positions){
        Line* line = (Line*)shapeProps;
        size_t count = positions.size();
        for(int i = 0; i < count; i++){
            float range = count > 1 ? (float)i / (float)(count - 1) : 0.0f;
            positions[i] = line->start + range * (line->end - line->start);
        }
    }

    void Circle_setPixelPositions(const void* shapeProps, std::vector<glm::vec3>& positions){
        Circle* circle = (Circle*)shapeProps;
        size_t count = positions.size();
        for(int i = 0; i < count; i++){
            float angle = float(i) / float(count) * M_PI * 2.0;
            positions[i].x = circle->center.x + cosf(angle) * circle->radius;
            positions[i].y = circle->center.y + sinf(angle) * circle->radius;
        }
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
    w.component<Patch::SelectedArtnetDevice>().add(flecs::Exclusive);
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
            .build(),
        .artnetDeviceInPatch = w.query_builder<Artnet::Device::Is, Artnet::Device::Settings>()
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
        flecs::entity patch = Fixture::getPatch(fixture);
        patch.add<Patch::RenderAreaDirty>();
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
        patch.add<Patch::ProgramDirty>();
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
        patch.add<Patch::ProgramDirty>();
    });

    w.system<Patch::Is>("PatchProgramCompile").with<Patch::ProgramDirty>()
    .kind(flecs::OnUpdate)
    .immediate()
    .each([](flecs::entity patch, Patch::Is){
        std::cout << "Compiling " << patch.name() << std::endl;
        patch.remove<Patch::ProgramDirty>();
        App::pushNewProgram(PatchProgram::compile(patch));
    });

    App::rtPatchRunner = std::thread([](){
        App::runPatch();
    });

};//App::Import()




PatchProgram* PatchProgram::compile(flecs::entity patch){
    PatchProgram* program = new PatchProgram();

    //prepare pixel buffers
    program->pixelCount = 0;
    Fixture::iterateWithDmx(patch, [&](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress& addr){ program->pixelCount += layout.pixelCount; });
    program->pixelColors = (ColorRGBW*)malloc(program->pixelCount * sizeof(ColorRGBW));
    program->pixelPositions = (glm::vec3*)malloc(program->pixelCount * sizeof(glm::vec3));

    //prepare universe buffers
    program->universeCount = Artnet::Universe::getCount(patch);
    program->universes = (PatchProgram::Universe*)malloc(program->universeCount * sizeof(PatchProgram::Universe));
    std::unordered_map<uint16_t, int> universeIndexByID; //store the id of each universe for later retrieval
    int universeIndex = 0;
    Artnet::Universe::iterate(patch, [&](flecs::entity universe, Artnet::Universe::Properties& props){
        universeIndexByID[props.universeId] = universeIndex;
        program->universes[universeIndex].id = props.universeId;
        universeIndex++;
    });

    
    int pixelIndex = 0;
    std::vector<PatchProgram::Pix2UniCopyInstr> p2us;
    Fixture::iterateWithDmx(patch, [&](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress& addr){
        int fixtureByteCount = layout.pixelCount * layout.channelsPerPixel;
        int universeSpanSize = (addr.address + fixtureByteCount + 511) / 512;

        if(const auto* pixelData = fixture.try_get<Fixture::PixelData>()){
            memcpy(program->pixelPositions + pixelIndex, pixelData->positions.data(), pixelData->positions.size() * sizeof(glm::vec3));
        }

        int universeId = addr.universe;
        int universeStartByte = addr.address;
        int pixelStartByte = 0;
        int remainingByteCount = fixtureByteCount;
        int fixturePixelIndex = 0;
        int fixturePixelByteIndex = 0;
        while(remainingByteCount > 0){
            int bytesInUniverse = std::min(512 - universeStartByte, remainingByteCount);

            if(universeIndexByID.count(universeId)){
                PatchProgram::Pix2UniCopyInstr p2u;
                p2u.universeIndex = universeIndexByID[universeId];
                p2u.universeOffset = universeStartByte;
                p2u.byteCount = bytesInUniverse;
                p2u.bytesPerPixel = layout.channelsPerPixel;
                p2u.pixelIndex = pixelIndex + fixturePixelIndex;
                p2u.pixelStartByte = fixturePixelByteIndex;
                p2us.push_back(p2u);
            }

            universeId++;
            universeStartByte = 0; //following universes always start at 0
            remainingByteCount -= bytesInUniverse;
            fixturePixelIndex += (fixturePixelByteIndex + bytesInUniverse) / layout.channelsPerPixel;
            fixturePixelByteIndex = (fixturePixelByteIndex + bytesInUniverse) % layout.channelsPerPixel;
        }

        pixelIndex += layout.pixelCount;
    });

    program->p2uCount = p2us.size();
    program->p2us = (PatchProgram::Pix2UniCopyInstr*)malloc(program->p2uCount * sizeof(PatchProgram::Pix2UniCopyInstr));
    memcpy(program->p2us, p2us.data(), program->p2uCount * sizeof(PatchProgram::Pix2UniCopyInstr));

    const auto& renderArea = patch.get<Patch::RenderArea>();
    program->pixelPosMin = renderArea.min;
    program->pixelPosMax = renderArea.max;

    return program;
}

PatchProgram::~PatchProgram(){
    free(p2us);
    free(pixelColors);
    free(pixelPositions);
    free(universes);
}

void render(PatchProgram* program){
    glm::vec3 center = (program->pixelPosMin + program->pixelPosMax) * 0.5f;
    float time = ImGui::GetTime();
    for(int i = 0; i < program->pixelCount; i++){
        const auto& pos = program->pixelPositions[i];
        auto& col = program->pixelColors[i];
        float dist = glm::distance(pos, center);
        float br = std::sin((dist - time * 100.0) / 30.0);
        uint8_t out = br > 0 ? br * 255.0 : 0;
        col.r = out;
        col.g = out;
        col.b = out;
        col.w = out;
    }
}

void encode(PatchProgram* program) {
    for (int i = 0; i < program->p2uCount; i++) {
        const PatchProgram::Pix2UniCopyInstr& map = program->p2us[i];
        uint8_t* dest = program->universes[map.universeIndex].buffer + map.universeOffset;
        int bytesWritten = 0;
        
        // Start with the first (potentially partial) pixel
        int p = map.pixelIndex;
        int pByte = map.pixelStartByte;
        
        while (bytesWritten < map.byteCount) {
            const uint8_t* src = reinterpret_cast<const uint8_t*>(&program->pixelColors[p]);
            
            // How many bytes can we take from this pixel?
            int availableInPixel = map.bytesPerPixel - pByte;
            int remainingInMap = map.byteCount - bytesWritten;
            int toCopy = std::min(availableInPixel, remainingInMap);
            
            std::memcpy(dest + bytesWritten, src + pByte, toCopy);
            
            bytesWritten += toCopy;
            p++;      // Move to next pixel
            pByte = 0; // Following pixels always start at byte 0
        }
    }
}

};//namespace PixelMapper