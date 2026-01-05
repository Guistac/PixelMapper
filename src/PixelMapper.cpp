#include "PixelMapper.h"

#include <algorithm>
#include <iostream>

#include "utils/FlecsUtils.h"



namespace PixelMapper::Application{

    flecs::entity get(const flecs::world& w){
        return w.target<Is>();
    }

    const Queries& getQueries(const flecs::world& w){
        return get(w).get<Queries>();
    }

    void iteratePatches(flecs::entity pixelMapper, std::function<void(flecs::entity patch)> fn){
        auto patchFolder = pixelMapper.target<PatchFolder>();
        if(!patchFolder.is_valid()) return;
        getQueries(pixelMapper.world()).patchQuery.set_var("parent", patchFolder)
        .each([fn](flecs::entity patch, Patch::Is){
            fn(patch);
        });
    }

    int getPatchCount(flecs::entity pixelMapper){
        auto patchFolder = pixelMapper.target<PatchFolder>();
        if(!patchFolder.is_valid()) return 0;
        const auto& queries = pixelMapper.get<Queries>();
        return queries.patchQuery.set_var("parent", patchFolder).count();
    }

    flecs::entity getSelectedPatch(flecs::entity pixelMapper){
        return pixelMapper.target<SelectedPatch>();
    }
    void selectPatch(flecs::entity pixelMapper, flecs::entity patch){
        pixelMapper.add<SelectedPatch>(patch);
    }

    flecs::entity createPatch(flecs::entity pixelMapper){
        auto patchFolder = pixelMapper.target<PatchFolder>();
        if(!patchFolder.is_valid()) return flecs::entity::null();
        const auto& world = pixelMapper.world();

        auto newPatch = world.entity()
            .add<Patch::Is>()
            .add<Patch::Settings>()
            .add<Patch::RenderArea>()
            .child_of(patchFolder);

        std::string patchName = "Patch " + std::to_string(getPatchCount(pixelMapper));
        newPatch.set_name(patchName.c_str());

        auto fixtureFolder = world.entity("FixtureFolder").child_of(newPatch);
        auto dmxOutputFolder = world.entity("DmxOutputFolder").child_of(newPatch);

        newPatch.add<Patch::FixtureFolder>(fixtureFolder);
        newPatch.add<Patch::DmxUniverseFolder>(dmxOutputFolder);
        
        return newPatch;
    }

}//namespace PixelMapper::Application


namespace PixelMapper::Patch{

    void iterateDmxUniverses(flecs::entity patch, std::function<void(flecs::entity dmxUniverse, Dmx::Universe::Properties&)> fn){
        auto dmxUniverseFolder = patch.target<DmxUniverseFolder>();
        if(!dmxUniverseFolder.is_valid()) return;
        Application::getQueries(patch.world()).patchDmxUniverseQuery.set_var("parent", dmxUniverseFolder)
        .each([fn](flecs::entity universe, Dmx::Universe::Is, Dmx::Universe::Properties& properties){
            fn(universe, properties);
        });
    }

    void iterateDmxFixtures(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn){
        auto fixtureFolder = patch.target<FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        Application::getQueries(patch.world()).patchDmxFixtureQuery.set_var("parent", fixtureFolder)
        .each([fn](flecs::entity fixture, Fixture::Is, Fixture::Layout& layout, Fixture::DmxAddress& dmxAddress){
            fn(fixture, layout, dmxAddress);
        });
    }

    void iterateFixturesInUniverse(flecs::entity patch, flecs::entity universe, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn){
        auto fixtureFolder = patch.target<FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        Application::getQueries(patch.world()).patchFixtureInUniverseQuery
        .set_var("parent", fixtureFolder)
        .set_var("universe", universe)
        .each([fn](flecs::entity fixture, Fixture::Is, Fixture::Layout& layout, Fixture::DmxAddress& dmxAddress){
            fn(fixture, layout, dmxAddress);
        });
    }

    void iteratePixels(flecs::entity patch, std::function<void(flecs::entity pixel, Pixel::Position& position, Pixel::ColorRGBW& color)> fn){
        auto fixtureFolder = patch.target<FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        Application::getQueries(patch.world()).patchPixelQuery.set_var("parent", fixtureFolder)
        .each([fn](flecs::entity pixel, Pixel::Is, Pixel::Position position, Pixel::ColorRGBW color){
            fn(pixel, position, color);
        });
    }

    flecs::entity getSelectedFixture(flecs::entity patch){
        if(!patch.is_valid() || !patch.is_alive()) return flecs::entity::null();
        return patch.target<SelectedFixture>();
    }
    void selectFixture(flecs::entity patch, flecs::entity fixture){
        patch.add<SelectedFixture>(fixture);
    }
    void removeFixtureSelection(flecs::entity patch){
        patch.remove<SelectedFixture>(flecs::Wildcard);
    }
    int getDmxFixtureCount(flecs::entity patch){
        auto fixtureFolder = patch.target<FixtureFolder>();
        if(!fixtureFolder.is_valid()) return 0;
        return Application::getQueries(patch.world()).patchDmxFixtureQuery.set_var("parent", fixtureFolder).count();
    }

    flecs::entity getSelectedUniverse(flecs::entity patch){
        if(!patch.is_valid() || !patch.is_alive()) return flecs::entity::null();
        return patch.target<SelectedDmxUniverse>();
    }
    void selectUniverse(flecs::entity patch, flecs::entity universe){
        patch.add<SelectedDmxUniverse>(universe);
    }

    flecs::entity createUniverse(flecs::entity patch, uint16_t universeId){
        auto dmxUniverseFolder = patch.target<DmxUniverseFolder>();
        if(!dmxUniverseFolder.is_valid()) return flecs::entity::null();
        std::string univName = "Universe " + std::to_string(universeId);
        return patch.world().entity()
            .child_of(dmxUniverseFolder)
            .set_name(univName.c_str())
            .add<Dmx::Universe::Is>()
            .add<Dmx::Universe::Channels>()
            .set<Dmx::Universe::Properties>({universeId, 0});
    }

}//namespace PixelMapper::Patch


namespace PixelMapper::Fixture{

    void iteratePixels(flecs::entity fixture, std::function<void(flecs::entity pixel, Pixel::Position&, Pixel::ColorRGBW&)> fn){
        Application::getQueries(fixture.world()).fixturePixelQuery.set_var("parent", fixture)
        .each([fn](flecs::entity pixel, Pixel::Is, Pixel::Position& position, Pixel::ColorRGBW& color){
            fn(pixel, position, color);
        });
    }

    void setDmxProperties(flecs::entity fixture, uint16_t universe, uint16_t startAddress){
        if(!fixture.is_valid()) return;
        auto* dmx = fixture.try_get_mut<Fixture::DmxAddress>();
        if(!dmx) return;
        dmx->universe = universe;
        dmx->address = startAddress;
    }

    int getPixelCount(flecs::entity fixture){
        return Application::getQueries(fixture.world()).fixturePixelQuery.set_var("parent", fixture).count();
    }

    flecs::entity createPixel(flecs::entity fixture){
        return fixture.world().entity()
        .child_of(fixture)
        .add<Pixel::Is>()
        .add<Pixel::ColorRGBW>()
        .add<Pixel::Position>();
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

    void updatePixelPositions(flecs::entity fixture, std::function<glm::vec2(float range, int index, size_t count)> pos_func) {
        const Fixture::Layout& f = fixture.get<Fixture::Layout>();
        int pixelIndex = 0;
        iteratePixels(fixture, [&](flecs::entity pixel, Pixel::Position& p, Pixel::ColorRGBW& c){
            float range = f.pixelCount > 1 ? (float)pixelIndex / (float)(f.pixelCount - 1) : 0.0f;
            p.position = pos_func(range, pixelIndex, f.pixelCount);
            pixelIndex++;
        });
        fixture.remove<Fixture::PixelPositionsDirty>();
        Fixture::getPatch(fixture).add<Patch::RenderAreaDirty>();
    }

    flecs::entity create(flecs::entity patch, int numPixels, int channels){
        auto fixtureList = patch.target<Patch::FixtureFolder>();
        auto newFixture = patch.world().entity()
            .child_of(fixtureList)
            .add<Fixture::Is>();

        Fixture::Layout layout{
            .pixelCount = numPixels,
            .colorChannels = channels,
            .byteCount = 0
        };

        newFixture.set<Fixture::Layout>(layout) //triggers pixel resize observer
        .set<Fixture::DmxAddress>({0,0});

        return newFixture;
    }

    flecs::entity createLine(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels, int channels) {
        std::string fixtureName = "Line Fixture " + std::to_string(Patch::getDmxFixtureCount(patch) + 1);
        auto newFixture = create(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<WithShape, Shape::Line>({start, end});
        return newFixture;
    }

    flecs::entity createCircle(flecs::entity patch, glm::vec2 center, float radius, int numPixels, int channels){
        std::string fixtureName = "Circle Fixture " + std::to_string(Patch::getDmxFixtureCount(patch) + 1);
        auto newFixture = create(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<WithShape, Shape::Circle>({center, radius});
        return newFixture;
    }

}//namespace PixelMapper::Fixture


namespace PixelMapper::Patch{
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
namespace PixelMapper::Fixture{
    void import(flecs::world& w){
        w.component<Is>();
        w.component<WithShape>();
        w.component<InUniverse>();
        w.component<LayoutDirty>();
        w.component<PixelPositionsDirty>();
        w.component<Layout>();
        w.component<DmxAddress>();
    }
}
namespace PixelMapper::Pixel{
    void import(flecs::world& w){
        w.component<Is>();
        w.component<ColorRGBW>().add(flecs::Sparse);
        w.component<Position>().add(flecs::Sparse);
    }
}
namespace PixelMapper::Dmx::Universe{
    void import(flecs::world& w){
        w.component<Is>();
        w.component<SendToController>();
        w.component<Properties>().add(flecs::Sparse);
        w.component<Channels>().add(flecs::Sparse);
    }
}
namespace PixelMapper::Shape{
    void import(flecs::world& w){
        w.component<Line>();
        w.component<Circle>();
    }
}


void PixelMapper::Application::import(flecs::world& w){

    //——————————————————— COMPONENTS ——————————————————————

    w.component<Is>();
    w.component<PatchFolder>();
    w.component<SelectedPatch>();
    Patch::import(w);
    Fixture::import(w);
    Pixel::import(w);
    Dmx::Universe::import(w);
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
        .patchQuery = w.query_builder<Patch::Is>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchDmxFixtureQuery = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchFixtureInUniverseQuery = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress>()
            .term().first(flecs::ChildOf).second("$parent")
            .with<Fixture::InUniverse>().second("$universe")
            .build(),
        .patchDmxUniverseQuery = w.query_builder<Dmx::Universe::Is, Dmx::Universe::Properties>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchPixelQuery = w.query_builder<Pixel::Is, Pixel::Position, Pixel::ColorRGBW>()
            .term().first(flecs::ChildOf).second("$fixture")
            .term().first(flecs::ChildOf).second("$parent").src("$fixture")
            .build(),
        .fixturePixelQuery = w.query_builder<Pixel::Is, Pixel::Position, Pixel::ColorRGBW>()
            .term().first(flecs::ChildOf).second("$parent")
            .build()
    };
    pixelMapper.set<Queries>(queries);

    //———————————————————— OBSERVERS ——————————————————————

    w.observer<Fixture::Layout>("ObserveFixtureLayout").event(flecs::OnSet)
    .with<Fixture::Is>()
    .each([](flecs::entity e, Fixture::Layout& l) {
        std::cout << "ObserveFixtureLayout" << std::endl;
        l.pixelCount = std::clamp<int>(l.pixelCount, 1, INT_MAX);
        l.colorChannels = std::clamp<int>(l.colorChannels, 1, 4);
        l.byteCount = l.pixelCount * l.colorChannels;
        e.add<Fixture::LayoutDirty>();
    });
    
    w.observer<Fixture::DmxAddress>("ObserveFixtureDmxAddress").event(flecs::OnSet)
    .with<Fixture::Is>()
    .each([](flecs::entity fixture, Fixture::DmxAddress& dmx){
        std::cout << "ObserveFixtureDmxAddress" << std::endl;
        dmx.address = std::clamp<uint16_t>(dmx.address, 0, 511);
        dmx.universe = std::clamp<uint16_t>(dmx.universe, 0, 32767);
        Fixture::getPatch(fixture).add<Patch::DmxMapDirty>();
    });

    //————————————————————— SYSTEMS ———————————————————————
    
    w.system<Fixture::Layout>("UpdateFixtureLayout").with<Fixture::LayoutDirty>()
    .with<Fixture::Is>()
    .each([](flecs::entity fixture, Fixture::Layout l){
        std::cout << "UpdateFixtureLayout" << std::endl;

        int current = Fixture::getPixelCount(fixture);
        if (current < l.pixelCount) {
            int toAdd = l.pixelCount - current;
            for (int i = 0; i < toAdd; i++) Fixture::createPixel(fixture);
        } else if (current > l.pixelCount) {
            int toDelete = current - l.pixelCount;
            Fixture::iteratePixels(fixture, [&](flecs::entity pixel, Pixel::Position& p, Pixel::ColorRGBW& c){
                if (toDelete > 0) {
                    pixel.destruct();
                    toDelete--;
                }
            });
        }

        fixture.remove<Fixture::LayoutDirty>();
        fixture.add<Fixture::PixelPositionsDirty>(); //recalculate pixel positions
        Fixture::getPatch(fixture).add<Patch::DmxMapDirty>(); //adjust the dmx output map
    });



    w.system<>("UpdateLinePixelPositions").with<Fixture::WithShape, Shape::Line>()
    .with<Fixture::PixelPositionsDirty>()
    .with<Fixture::Is>()
    .each([](flecs::entity fixture) {
        std::cout << "UpdateLinePixelPositions" << std::endl;

        const Shape::Line& line = fixture.get<Fixture::WithShape,Shape::Line>();
        Fixture::updatePixelPositions(fixture, [line](float r, int i, size_t c){
            return line.start + r * (line.end - line.start);
        });
    });



    w.system<>("UpdateCirclePixelPositions").with<Fixture::WithShape, Shape::Circle>()
    .with<Fixture::PixelPositionsDirty>()
    .with<Fixture::Is>()
    .each([](flecs::entity fixture) {
        std::cout << "UpdateCirclePixelPositions" << std::endl;

        const Shape::Circle& circle = fixture.get<Fixture::WithShape,Shape::Circle>();
        Fixture::updatePixelPositions(fixture, [circle](float r, int i, int c){
            float angle = float(i) / float(c) * M_PI * 2.0;
            return glm::vec2{
                circle.center.x + cosf(angle) * circle.radius,
                circle.center.y + sinf(angle) * circle.radius
            };
        });
    });


    w.system<Patch::RenderArea>("UpdateRenderArea").with<Patch::RenderAreaDirty>()
    .with<Patch::Is>()
    .each([](flecs::entity patch, Patch::RenderArea& ra){
        std::cout << "UpdateRenderArea" << std::endl;
        glm::vec2 min(FLT_MAX);
        glm::vec2 max(-FLT_MAX);
        bool b_hasPixels = false;
        Patch::iteratePixels(patch, [&](flecs::entity pixel, Pixel::Position& p, Pixel::ColorRGBW& c){
            min = glm::min(min, p.position);
            max = glm::max(max, p.position);
            b_hasPixels = true;
        });
        if(b_hasPixels){
            ra.min = min;
            ra.max = max;
        }

        patch.remove<Patch::RenderAreaDirty>();
    });


    w.system<Patch::Is>("UpdateDmxOutputMap").with<Patch::DmxMapDirty>()
    .each([](flecs::entity patch, Patch::Is){
        std::cout << "UpdateDmxOutputMap" << std::endl;

        std::unordered_map<uint16_t, flecs::entity> univsByNumber;
        std::unordered_map<flecs::entity, std::vector<uint16_t>, EntityHasher> univsByFixture;

        //compile a list of universes that are needed to cover all fixtures
        Patch::iterateDmxFixtures(patch, [&](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress dmxAddress){
            int fixtureUniverseCount = 1;
            int channels = dmxAddress.address + layout.byteCount;
            while(channels > 512){ fixtureUniverseCount++; channels -= 512; }
            
            auto& fixtureUnivNumbers = univsByFixture[fixture];
            for(int i = 0; i < fixtureUniverseCount; i++){
                univsByNumber[dmxAddress.universe + i] = flecs::entity::null();
                fixtureUnivNumbers.push_back(dmxAddress.universe + i);
            }
        });

        //compare with the current list of universes, decide what to keep and delete
        std::vector<flecs::entity> toDelete;
        Patch::iterateDmxUniverses(patch, [&](flecs::entity universe, Dmx::Universe::Properties& properties){
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
                flecs::entity newUniverse = Patch::createUniverse(patch, id);
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

};//main Application Import