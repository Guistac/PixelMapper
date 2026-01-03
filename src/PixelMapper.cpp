#include "PixelMapper.h"

#include <iostream>

namespace PixelMapper{

flecs::entity get(const flecs::world& w){
    return w.target<PixelMapper::Is>();
}

struct Queries{
    flecs::query<Patch::Is> patchQuery;
    flecs::query<Fixture::Is> patchFixtureLayoutQuery;
    flecs::query<Dmx::Universe::Is> patchDmxUniverseQuery;
    flecs::query<Pixel::Is> fixturePixelQuery;
    flecs::query<Pixel::Is> patchPixelQuery;
};

const Queries& getQueries(const flecs::world& w){
    return get(w).get<Queries>();
}

void iteratePatches(flecs::entity pixelMapper, std::function<void(flecs::entity patch)> fn){
    auto patchFolder = pixelMapper.target<PatchFolder>();
    if(!patchFolder.is_valid()) return;
    getQueries(pixelMapper.world()).patchQuery.iter().set_var("parent", patchFolder)
    .each([fn](flecs::entity patch, Patch::Is){
        fn(patch);
    });
}

int getPatchCount(flecs::entity pixelMapper){
    auto patchFolder = pixelMapper.target<PatchFolder>();
    if(!patchFolder.is_valid()) return 0;
    const auto& queries = pixelMapper.get<Queries>();
    return queries.patchQuery.iter().set_var("parent", patchFolder).count();
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





namespace Patch{

    void iterateDmxUniverses(flecs::entity patch, std::function<void(flecs::entity dmxUniverse)> fn){
        auto dmxUniverseFolder = patch.target<DmxUniverseFolder>();
        if(!dmxUniverseFolder.is_valid()) return;
        getQueries(patch.world()).patchDmxUniverseQuery.iter().set_var("parent", dmxUniverseFolder)
        .each([fn](flecs::entity universe, Dmx::Universe::Is){
            fn(universe);
        });
    }

    void iterateFixtures(flecs::entity patch, std::function<void(flecs::entity fixture)> fn){
        auto fixtureFolder = patch.target<FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        getQueries(patch.world()).patchFixtureLayoutQuery.iter().set_var("parent", fixtureFolder)
        .each([fn](flecs::entity fixture, Fixture::Is){
            fn(fixture);
        });
    }

    void iteratePixels(flecs::entity patch, std::function<void(flecs::entity pixel)> fn){
        auto fixtureFolder = patch.target<FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        getQueries(patch.world()).patchPixelQuery.iter().set_var("parent", fixtureFolder)
        .each([fn](flecs::entity pixel, Pixel::Is){
            fn(pixel);
        });
    }

    flecs::entity getSelectedFixture(flecs::entity patch){
        if(!patch.is_valid() || !patch.is_alive()) return flecs::entity::null();
        return patch.target<SelectedFixture>();
    }
    void selectFixture(flecs::entity patch, flecs::entity fixture){
        patch.add<SelectedFixture>(fixture);
    }
    int getFixtureCount(flecs::entity patch){
        auto fixtureFolder = patch.target<FixtureFolder>();
        if(!fixtureFolder.is_valid()) return 0;
        return getQueries(patch.world()).patchFixtureLayoutQuery.iter().set_var("parent", fixtureFolder).count();
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
            .set<Dmx::Universe::Properties>({universeId, 0});
    }

    flecs::entity createFixture(flecs::entity patch, int numPixels, int channels){
        auto fixtureList = patch.target<FixtureFolder>();
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

    flecs::entity spawnLineFixture(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels, int channels) {
        std::string fixtureName = "Line Fixture " + std::to_string(getFixtureCount(patch) + 1);
        auto newFixture = createFixture(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<Fixture::Shape, Shape::Line>({start, end});
        return newFixture;
    }

    flecs::entity spawnCircleFixture(flecs::entity patch, glm::vec2 center, float radius, int numPixels, int channels){
        std::string fixtureName = "Circle Fixture " + std::to_string(getFixtureCount(patch) + 1);
        auto newFixture = createFixture(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<Fixture::Shape, Shape::Circle>({center, radius});
        return newFixture;
    }

}//namespace Patch

namespace Fixture{

    void iteratePixels(flecs::entity fixture, std::function<void(flecs::entity pixel)> fn){
        getQueries(fixture.world()).fixturePixelQuery.iter().set_var("parent", fixture)
        .each([fn](flecs::entity pixel, Pixel::Is){
            fn(pixel);
        });
    }

    int getPixelCount(flecs::entity fixture){
        return getQueries(fixture.world()).fixturePixelQuery.iter().set_var("parent", fixture).count();
    }

    flecs::entity createPixel(flecs::entity fixture){
        return fixture.world().entity()
        .child_of(fixture)
        .add<Pixel::Is>()
        .add<Pixel::ColorRGBWA>()
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
        iteratePixels(fixture, [&](flecs::entity pixel){
            if(Pixel::Position* p = pixel.try_get_mut<Pixel::Position>()){
                float range = f.pixelCount > 1 ? (float)pixelIndex / (float)(f.pixelCount - 1) : 0.0f;
                p->position = pos_func(range, pixelIndex, f.pixelCount);
                pixelIndex++;
            }
        });
    }

}//namespace Fixture






Module::Module(flecs::world& w){
    w.module<Module>();

    w.component<Is>();
    w.component<PatchFolder>();
    w.component<SelectedPatch>();

    auto paw = w.scope("Patch");
    paw.component<Patch::Is>();
    paw.component<Patch::FixtureFolder>();
    paw.component<Patch::DmxUniverseFolder>();
    paw.component<Patch::SelectedFixture>();
    paw.component<Patch::SelectedDmxUniverse>();
    paw.component<Patch::DmxMapDirty>();
    paw.component<Patch::RenderAreaDirty>();
    paw.component<Patch::Settings>();
    paw.component<Patch::RenderArea>();
    
    auto fiw = w.scope("Fixture");
    fiw.component<Fixture::Is>();
    fiw.component<Fixture::Shape>();
    fiw.component<Fixture::InUniverse>();
    fiw.component<Fixture::LayoutDirty>();
    fiw.component<Fixture::PixelPositionsDirty>();
    fiw.component<Fixture::Layout>();
    fiw.component<Fixture::DmxAddress>();
    
    auto pxw = w.scope("Pixel");
    pxw.component<Pixel::Is>();
    pxw.component<Pixel::ColorRGBWA>();
    pxw.component<Pixel::Position>();

    auto duw = w.scope("Dmx::Universe");
    duw.component<Dmx::Universe::Is>();
    duw.component<Dmx::Universe::Properties>();
    duw.component<Dmx::Universe::Channels>();

    auto shw = w.scope("Shape");
    shw.scope("Shape");
    shw.component<Shape::Line>();
    shw.component<Shape::Circle>();

    w.component<Fixture::Shape>().add(flecs::Exclusive);
    w.component<Patch::SelectedFixture>().add(flecs::Exclusive);
    w.component<Patch::SelectedDmxUniverse>().add(flecs::Exclusive);
    w.component<SelectedPatch>().add(flecs::Exclusive);


    auto pixelMapper = w.entity("PixelMapper");
    w.add<PixelMapper::Is>(pixelMapper);
    auto patchFolder = w.entity("PixelMapperPatches").child_of(pixelMapper);
    pixelMapper.add<PatchFolder>(patchFolder);

    Queries queries{
        .patchQuery = w.query_builder<Patch::Is>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .fixturePixelQuery = w.query_builder<Pixel::Is>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchDmxUniverseQuery = w.query_builder<Dmx::Universe::Is>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchFixtureLayoutQuery = w.query_builder<Fixture::Is>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchPixelQuery = w.query_builder<Pixel::Is>()
            .term().first(flecs::ChildOf).second("$fixture")
            .term().first(flecs::ChildOf).second("$parent").src("$fixture")
            .build()
    };
    pixelMapper.set<Queries>(queries);

    //———————— OBSERVERS

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

    //———————— SYSTEMS

    w.system<Fixture::Layout>("UpdateFixtureLayout").with<Fixture::LayoutDirty>()
    .with<Fixture::Is>()
    .each([](flecs::entity fixture, Fixture::Layout l){
        std::cout << "UpdateFixtureLayout" << std::endl;
        auto w = fixture.world();

        int current = Fixture::getPixelCount(fixture);
        
        if (current < l.pixelCount) {
            int toAdd = l.pixelCount - current;
            for (int i = 0; i < toAdd; i++) Fixture::createPixel(fixture);
        } else if (current > l.pixelCount) {
            int toDelete = current - l.pixelCount;
            Fixture::iteratePixels(fixture, [&](flecs::entity pixel){
                if (toDelete > 0) {
                    pixel.destruct();
                    toDelete--;
                }
            });
        }
        //trigger modified event on the FixtureShape:Shape component
        //this will cause the next observer to recalculate pixel positions
        if(flecs::entity target = fixture.target<Fixture::Shape>()){
            flecs::id pair_id = w.pair<Fixture::Shape>(target);
            fixture.modified(pair_id);
        }

        fixture.remove<Fixture::LayoutDirty>();
        Fixture::getPatch(fixture).add<Patch::DmxMapDirty>(); //adjust the dmx output map
        fixture.add<Fixture::PixelPositionsDirty>(); //recalculate pixel positions
    });



    w.system<>("UpdateLinePixelPositions").with<Fixture::Shape, Shape::Line>()
    .with<Fixture::PixelPositionsDirty>()
    .with<Fixture::Is>()
    .each([](flecs::entity fixture) {
        std::cout << "UpdateLinePixelPositions" << std::endl;
        const Shape::Line& line = fixture.get<Fixture::Shape,Shape::Line>();
        Fixture::updatePixelPositions(fixture, [line](float r, int i, size_t c){
            return line.start + r * (line.end - line.start);
        });

        fixture.remove<Fixture::PixelPositionsDirty>();
        Fixture::getPatch(fixture).add<Patch::RenderAreaDirty>();
    });



    w.system<>("UpdateCirclePixelPositions").with<Fixture::Shape, Shape::Circle>()
    .with<Fixture::PixelPositionsDirty>()
    .with<Fixture::Is>()
    .each([](flecs::entity fixture) {
        std::cout << "UpdateCirclePixelPositions" << std::endl;
        const Shape::Circle& circle = fixture.get<Fixture::Shape,Shape::Circle>();
        Fixture::updatePixelPositions(fixture, [circle](float r, int i, int c){
            float angle = float(i) / float(c) * M_PI * 2.0;
            return glm::vec2{
                circle.center.x + cosf(angle) * circle.radius,
                circle.center.y + sinf(angle) * circle.radius
            };
        });

        fixture.remove<Fixture::PixelPositionsDirty>();
        Fixture::getPatch(fixture).add<Patch::RenderAreaDirty>();
    });



    w.system<Patch::Is>("UpdateDmxOutputMap").with<Patch::DmxMapDirty>()
    .each([](flecs::entity patch, Patch::Is){

        std::cout << "UpdateDmxOutputMap" << std::endl;
        
        std::vector<uint16_t> univIds = {};
        struct FixtureToUniverse{
            flecs::entity fixture;
            std::vector<uint16_t> universes = {};
        };
        std::vector<FixtureToUniverse> fixtureToUniverseLinks;
            
        //compile a list of universes that are needed to cover all fixtures
        Patch::iterateFixtures(patch, [&](flecs::entity fixture){
            const Fixture::DmxAddress* address = fixture.try_get<Fixture::DmxAddress>();
            const Fixture::Layout* layout = fixture.try_get<Fixture::Layout>();
            if(!address || !layout) return;
            
            int fixtureUniverseCount = 1;
            int channels = address->address + layout->byteCount;
            while(channels > 512){ fixtureUniverseCount++; channels -= 512; }
            
            FixtureToUniverse link{.fixture = fixture};
            for(int i = 0; i < fixtureUniverseCount; i++){
                univIds.push_back(address->universe + i);
                link.universes.push_back(address->universe + i);
            }
            fixtureToUniverseLinks.push_back(link);
        });
                
        //sort the universe ids and remove duplicates
        std::sort(univIds.begin(), univIds.end());
        auto lastUnique = std::unique(univIds.begin(), univIds.end());
        univIds.erase(lastUnique, univIds.end());
        
        // We'll track which existing entities we still need
        std::vector<uint16_t> existingUnivs;
        std::vector<flecs::entity> toDelete;
        
        Patch::iterateDmxUniverses(patch, [&](flecs::entity universe){
            const auto* properties = universe.try_get<Dmx::Universe::Properties>();
            // If this universe ID is NOT in our new list, mark for deletion
            if (std::find(univIds.begin(), univIds.end(), properties->universeId) == univIds.end()) {
                toDelete.push_back(universe);
            } else {
                existingUnivs.push_back(properties->universeId);
            }
        });
        
        //Safe Deletion outside of iteration
        for (auto e : toDelete) e.destruct();


        // Add missing universes
        for (uint16_t id : univIds) {
            if(std::find(existingUnivs.begin(), existingUnivs.end(), id) == existingUnivs.end()){
                Patch::createUniverse(patch, id);
            }
        }

        //Build Fixture:InUniverse-Universe relationships
        for(auto& link : fixtureToUniverseLinks) link.fixture.remove<Fixture::InUniverse>(flecs::Wildcard);
        std::unordered_map<uint16_t, flecs::entity> idToUniverseEnt;

        Patch::iterateDmxUniverses(patch, [&](flecs::entity universe){
            const auto* properties = universe.try_get<Dmx::Universe::Properties>();
            idToUniverseEnt[properties->universeId] = universe;
        });
        for(auto& link : fixtureToUniverseLinks){
            for(auto id : link.universes){
                if(idToUniverseEnt.count(id)) link.fixture.add<Fixture::InUniverse>(idToUniverseEnt[id]);
            }
        }

        patch.remove<Patch::DmxMapDirty>();
    });


    w.system<Patch::RenderArea>("UpdateRenderArea").with<Patch::RenderAreaDirty>()
    .with<Patch::Is>()
    .each([](flecs::entity patch, Patch::RenderArea& ra){
        std::cout << "UpdateRenderArea" << std::endl;
        glm::vec2 min(FLT_MAX);
        glm::vec2 max(-FLT_MAX);
        bool b_hasPixels = false;
        Patch::iteratePixels(patch, [&](flecs::entity pixel){
             if(const Pixel::Position* p = pixel.try_get<Pixel::Position>()){
                min = glm::min(min, p->position);
                max = glm::max(max, p->position);
                b_hasPixels = true;
            }
        });
        if(b_hasPixels){
            ra.min = min;
            ra.max = max;
        }

        patch.remove<Patch::RenderAreaDirty>();
    });

};//struct Module




};//namespace PixelMapper