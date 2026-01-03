#include "PixelMapper.h"

#include <iostream>
#include <functional>

namespace PixelMapper{

flecs::entity createPatch(flecs::world& w){
    auto newPatch = w.entity("TestPatch")
        .add<Patch::Is>()
        .add<Patch::Settings>()
        .add<Patch::RenderArea>();

    auto fixtureFolder = w.entity("FixtureFolder").child_of(newPatch);
    newPatch.add<Patch::FixtureFolder>(fixtureFolder);

    auto dmxOutputFolder = w.entity("DmxOutputFolder").child_of(newPatch);
    newPatch.add<Patch::DmxUniverseFolder>(dmxOutputFolder);

    return newPatch;
}

flecs::entity getPatch(flecs::world& w){
    return w.lookup("TestPatch");
}

namespace Patch{

    void iterateDmxUniverses(flecs::entity patch, std::function<void(flecs::entity dmxUniverse, Dmx::Universe::Properties& up)> fn){
        auto dmxUniverseFolder = patch.target<DmxUniverseFolder>();
        if(!dmxUniverseFolder.is_valid()) return;
        auto& queries = patch.world().get<Queries>();
        queries.patchDmxUniverseQuery.iter().set_var("parent", dmxUniverseFolder)
        .each([fn](flecs::entity universe, Dmx::Universe::Properties& up){
            fn(universe, up);
        });
    }

    void iterateFixtures(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::Layout& layout)> fn){
        auto fixtureFolder = patch.target<FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        auto& queries = patch.world().get<Queries>();
        queries.patchFixtureLayoutQuery.iter().set_var("parent", fixtureFolder)
        .each([fn](flecs::entity fixture, Fixture::Layout& layout){
            fn(fixture, layout);
        });
    }

    void iteratePixels(flecs::entity patch, std::function<void(flecs::entity pixel)> fn){
        auto fixtureFolder = patch.target<FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        auto& queries = patch.world().get<Queries>();
        queries.patchPixelQuery.iter().set_var("parent", fixtureFolder)
        .each([fn](flecs::entity pixel, Pixel::Is){
            fn(pixel);
        });
    }

    flecs::entity getSelectedFixture(flecs::entity patch){
        return patch.target<SelectedFixture>();
    }
    void selectFixture(flecs::entity patch, flecs::entity fixture){
        patch.add<SelectedFixture>(fixture);
    }

    flecs::entity getSelectedUniverse(flecs::entity patch){
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

    int getFixtureCount(flecs::entity patch){
        auto fixtureFolder = patch.target<FixtureFolder>();
        if(!fixtureFolder.is_valid()) return 0;
        auto& queries = patch.world().get<Queries>();
        return queries.patchFixtureLayoutQuery.iter().set_var("parent", fixtureFolder).count();
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
        .set<Fixture::Shape, Line>({start, end}); //triggers line pixel position observer
        return newFixture;
    }

    flecs::entity spawnCircleFixture(flecs::entity patch, glm::vec2 center, float radius, int numPixels, int channels){
        std::string fixtureName = "Circle Fixture " + std::to_string(getFixtureCount(patch) + 1);
        auto newFixture = createFixture(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<Fixture::Shape, Circle>({center, radius}); //triggers circle pixel position observer
        return newFixture;
    }

}

namespace Fixture{

    void iteratePixels(flecs::entity fixture, std::function<void(flecs::entity pixel)> fn){
        auto& queries = fixture.world().get<Queries>();
        queries.fixturePixelQuery.iter().set_var("parent", fixture)
        .each([fn](flecs::entity pixel, Pixel::Is){
            fn(pixel);
        });
    }

    int getPixelCount(flecs::entity fixture){
        auto& queries = fixture.world().get<Queries>();
        return queries.fixturePixelQuery.iter().set_var("parent", fixture).count();
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

}









void init(flecs::world& w){

    //the FixtureShape relationship is exclusive, like ChildOf
    w.component<Fixture::Shape>().add(flecs::Exclusive);

    //Only single selection for now
    w.component<Patch::SelectedFixture>().add(flecs::Exclusive);
    w.component<Patch::SelectedDmxUniverse>().add(flecs::Exclusive);

    Queries queries{
        .fixturePixelQuery = w.query_builder<Pixel::Is>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchDmxUniverseQuery = w.query_builder<Dmx::Universe::Properties>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchFixtureLayoutQuery = w.query_builder<Fixture::Layout>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchPixelQuery = w.query_builder<Pixel::Is>()
            .term().first(flecs::ChildOf).second("$fixture")
            .term().first(flecs::ChildOf).second("$parent").src("$fixture")
            .build()
    };
    w.component<Queries>().add(flecs::Singleton).set<Queries>(queries);


    w.observer<Fixture::Layout>("ObserveFixtureLayout").event(flecs::OnSet)
    .with<Fixture::Is>()
    .each([](flecs::entity e, Fixture::Layout& l) {
        l.pixelCount = std::clamp<int>(l.pixelCount, 1, INT_MAX);
        l.colorChannels = std::clamp<int>(l.colorChannels, 1, 4);
        l.byteCount = l.pixelCount * l.colorChannels;
        e.add<Fixture::LayoutDirty>();
    });
    
    w.observer<Fixture::DmxAddress>("ObserveFixtureDmxAddress").event(flecs::OnSet)
    .with<Fixture::Is>()
    .each([](flecs::entity fixture, Fixture::DmxAddress& dmx){
        dmx.address = std::clamp<uint16_t>(dmx.address, 0, 511);
        dmx.universe = std::clamp<uint16_t>(dmx.universe, 0, 32767);
        Fixture::getPatch(fixture).add<Patch::DmxMapDirty>();
    });

    w.observer<>("ObserveFixtureShape")
    .with<Fixture::Is>()
    .with<Fixture::Shape>(flecs::Wildcard).event(flecs::OnSet)
    .each([](flecs::entity fixture){
        fixture.add<Fixture::PixelPositionsDirty>();
        Fixture::getPatch(fixture).add<Patch::RenderAreaDirty>();
    });



    w.system<Fixture::Layout>("UpdateFixtureLayout").with<Fixture::LayoutDirty>()
    .with<Fixture::Is>()
    .each([](flecs::entity fixture, Fixture::Layout l){
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
        Fixture::getPatch(fixture).add<Patch::DmxMapDirty>(); //adjust the dmx output map
        fixture.remove<Fixture::LayoutDirty>();
    });



    w.system<>("UpdateLinePixelPositions").with<Fixture::Shape, Line>()
    .with<Fixture::PixelPositionsDirty>()
    .with<Fixture::Is>()
    .each([](flecs::entity fixture) {
        const Line& line = fixture.get<Fixture::Shape,Line>();
        Fixture::updatePixelPositions(fixture, [line](float r, int i, size_t c){
            return line.start + r * (line.end - line.start);
        });
        Fixture::getPatch(fixture).add<Patch::RenderAreaDirty>();
        fixture.remove<Fixture::PixelPositionsDirty>();
    });



    w.system<>("UpdateCirclePixelPositions").with<Fixture::Shape, Circle>()
    .with<Fixture::PixelPositionsDirty>()
    .with<Fixture::Is>()
    .each([](flecs::entity fixture) {
        const Circle& circle = fixture.get<Fixture::Shape,Circle>();
        Fixture::updatePixelPositions(fixture, [circle](float r, int i, int c){
            float angle = float(i) / float(c) * M_PI * 2.0;
            return glm::vec2{
                circle.center.x + cosf(angle) * circle.radius,
                circle.center.y + sinf(angle) * circle.radius
            };
        });
        Fixture::getPatch(fixture).add<Patch::RenderAreaDirty>();
        fixture.remove<Fixture::PixelPositionsDirty>();
    });



    w.system<Patch::Is>("UpdateDmxOutputMap").with<Patch::DmxMapDirty>()
    .each([](flecs::entity patch, Patch::Is){
        
        //flecs::entity fixtureFolder = patch.target<Patch::FixtureFolder>();
        //flecs::entity dmxOutputFolder = patch.target<Patch::DmxUniverseFolder>();
        //if(!fixtureFolder.is_valid() || !dmxOutputFolder.is_valid()) return;
        
        std::vector<uint16_t> univIds = {};
        struct FixtureToUniverse{
            flecs::entity fixture;
            std::vector<uint16_t> universes = {};
        };
        std::vector<FixtureToUniverse> fixtureToUniverseLinks;
            
        //compile a list of universes that are needed to cover all fixtures
        Patch::iterateFixtures(patch, [&](flecs::entity fixture, const Fixture::Layout& layout){
            const Fixture::DmxAddress* a = fixture.try_get<Fixture::DmxAddress>();
            if(!a) return;
            
            int fixtureUniverseCount = 1;
            int channels = a->address + layout.byteCount;
            while(channels > 512){ fixtureUniverseCount++; channels -= 512; }
            
            FixtureToUniverse link{.fixture = fixture};
            for(int i = 0; i < fixtureUniverseCount; i++){
                univIds.push_back(a->universe + i);
                link.universes.push_back(a->universe + i);
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
        
        Patch::iterateDmxUniverses(patch, [&](flecs::entity dmxUniverse, const Dmx::Universe::Properties& u){
        // If this universe ID is NOT in our new list, mark for deletion
            if (std::find(univIds.begin(), univIds.end(), u.universeId) == univIds.end()) {
                toDelete.push_back(dmxUniverse);
            } else {
                existingUnivs.push_back(u.universeId);
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

        Patch::iterateDmxUniverses(patch, [&](flecs::entity universe, const Dmx::Universe::Properties& u){
            idToUniverseEnt[u.universeId] = universe;
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


    createPatch(w);
    Patch::spawnLineFixture(getPatch(w), glm::vec2(100.0, 200.0), glm::vec2(200.0, 100.0));
    Patch::spawnCircleFixture(getPatch(w), glm::vec2(100.0, 100.0), 50.0);
}


};//namespace PixelMapper