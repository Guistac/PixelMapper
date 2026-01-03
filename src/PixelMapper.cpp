#include "PixelMapper.h"

#include <iostream>
#include <functional>

namespace PixelMapper{


flecs::entity createPatch(flecs::world& w){
    auto newPatch = w.entity("TestPatch")
        .add<IsPatch>()
        .add<PatchSettings>()
        .add<RenderArea>();

    auto fixtureFolder = w.entity("FixtureFolder").child_of(newPatch);
    newPatch.add<FixtureList>(fixtureFolder);

    auto dmxOutputFolder = w.entity("DmxOutputFolder").child_of(newPatch);
    newPatch.add<DmxOutput>(dmxOutputFolder);

    return newPatch;
}

flecs::entity getPatch(flecs::world& w){
    return w.lookup("TestPatch");
}

flecs::entity getSelectedFixture(flecs::entity patch){
    return patch.target<SelectedFixture>();
}
void selectFixture(flecs::entity patch, flecs::entity fixture){
    patch.add<SelectedFixture>(fixture);
}

void selectUniverse(flecs::entity patch, flecs::entity universe){
    patch.target<DmxOutput>().add<SelectedDmxUniverse>(universe);
}
flecs::entity getSelectedUniverse(flecs::entity patch){
    return patch.target<DmxOutput>().target<SelectedDmxUniverse>();
}


void patchIterateDmxUniverses(flecs::entity patch, std::function<void(flecs::entity dmxUniverse, DmxUniverse& u)> fn){
    auto dmxUniverseFolder = patch.target<DmxOutput>();
    if(!dmxUniverseFolder.is_valid()) return;
    auto& queries = patch.world().get<PixelMapperQueries>();
    queries.patchDmxUniverseQuery.iter().set_var("parent", dmxUniverseFolder)
    .each([fn](flecs::entity universe, DmxUniverse& u){
        fn(universe, u);
    });
}

void fixtureIteratePixels(flecs::entity fixture, std::function<void(flecs::entity pixel)> fn){
    auto& queries = fixture.world().get<PixelMapperQueries>();
    queries.fixturePixelQuery.iter().set_var("parent", fixture)
    .each([fn](flecs::entity pixel, IsPixel){
        fn(pixel);
    });
}

int fixtureGetPixelCountt(flecs::entity fixture){
    auto& queries = fixture.world().get<PixelMapperQueries>();
    return queries.fixturePixelQuery.iter().set_var("parent", fixture).count();
}

void patchIterateFixtures(flecs::entity patch, std::function<void(flecs::entity fixture, FixtureLayout& layout)> fn){
    auto fixtureFolder = patch.target<FixtureList>();
    if(!fixtureFolder.is_valid()) return;
    auto& queries = patch.world().get<PixelMapperQueries>();
    queries.patchFixtureLayoutQuery.iter().set_var("parent", fixtureFolder)
    .each([fn](flecs::entity fixture, FixtureLayout& layout){
        fn(fixture, layout);
    });
}

int patchGetFixtureCount(flecs::entity patch){
    auto fixtureFolder = patch.target<FixtureList>();
    if(!fixtureFolder.is_valid()) return 0;
    auto& queries = patch.world().get<PixelMapperQueries>();
    return queries.patchFixtureLayoutQuery.iter().set_var("parent", fixtureFolder).count();
}

void patchIteratePixels(flecs::entity patch, std::function<void(flecs::entity pixel)> fn){
    auto fixtureFolder = patch.target<FixtureList>();
    if(!fixtureFolder.is_valid()) return;
    auto& queries = patch.world().get<PixelMapperQueries>();
    queries.patchPixelQuery.iter().set_var("parent", fixtureFolder)
    .each([fn](flecs::entity pixel, IsPixel){
        fn(pixel);
    });
}

flecs::entity createFixture(flecs::entity patch, int numPixels, int channels){
    auto fixtureList = patch.target<FixtureList>();
    auto newFixture = patch.world().entity()
        .child_of(fixtureList)
        .add<IsFixture>();

    FixtureLayout layout{
        .pixelCount = numPixels,
        .colorChannels = channels,
        .byteCount = 0
    };

    newFixture.set<FixtureLayout>(layout) //triggers pixel resize observer
    .set<DmxAddress>({0,0});

    return newFixture;
}

flecs::entity createLineFixture(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels, int channels) {
    std::string fixtureName = "Line Fixture " + std::to_string(patchGetFixtureCount(patch) + 1);
    auto newFixture = createFixture(patch, numPixels, channels)
    .set_name(fixtureName.c_str())
    .set<FixtureShape, Line>({start, end}); //triggers line pixel position observer
    return newFixture;
}

flecs::entity createCircleFixture(flecs::entity patch, glm::vec2 center, float radius, int numPixels, int channels){
    std::string fixtureName = "Circle Fixture " + std::to_string(patchGetFixtureCount(patch) + 1);
    auto newFixture = createFixture(patch, numPixels, channels)
    .set_name(fixtureName.c_str())
    .set<FixtureShape, Circle>({center, radius}); //triggers circle pixel position observer
    return newFixture;
}

flecs::entity createPixel(flecs::entity fixture){
    return fixture.world().entity()
    .child_of(fixture)
    .add<IsPixel>()
    .add<ColorRGBWA>()
    .add<Position>();
}

flecs::entity getFixturePatch(flecs::entity fixture) {
    if (!fixture.is_valid() || !fixture.is_alive())
        return flecs::entity::null();
    flecs::entity fixtureList = fixture.parent();
    if (!fixtureList.is_valid())
        return flecs::entity::null();
    flecs::entity patch = fixtureList.parent();
    if (patch.is_valid() && patch.has<IsPatch>())
        return patch;
    return flecs::entity::null();
}

void updateFixturePixelPositions(flecs::entity fixture, std::function<glm::vec2(float range, int index, size_t count)> pos_func) {
    const FixtureLayout& f = fixture.get<FixtureLayout>();
    int pixelIndex = 0;
    fixtureIteratePixels(fixture, [&](flecs::entity pixel){
        if(Position* p = pixel.try_get_mut<Position>()){
            float range = f.pixelCount > 1 ? (float)pixelIndex / (float)(f.pixelCount - 1) : 0.0f;
            p->position = pos_func(range, pixelIndex, f.pixelCount);
            pixelIndex++;
        }
    });
}






void init(flecs::world& w){

    flecs::entity e = w.entity();
    e.add<FixtureLayout>();

    //the FixtureShape relationship is exclusive, like ChildOf
    w.component<FixtureShape>().add(flecs::Exclusive);

    //Only single selection for now
    w.component<SelectedFixture>().add(flecs::Exclusive);
    w.component<SelectedDmxUniverse>().add(flecs::Exclusive);

    PixelMapperQueries queries{
        .fixturePixelQuery = w.query_builder<IsPixel>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchDmxUniverseQuery = w.query_builder<DmxUniverse>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchFixtureLayoutQuery = w.query_builder<FixtureLayout>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .patchPixelQuery = w.query_builder<IsPixel>()
            .term().first(flecs::ChildOf).second("$fixture")
            .term().first(flecs::ChildOf).second("$parent").src("$fixture")
            .build()
    };
    w.component<PixelMapperQueries>().add(flecs::Singleton).set<PixelMapperQueries>(queries);


    w.observer<FixtureLayout>("ObserveFixtureLayout").event(flecs::OnSet)
    .with<IsFixture>()
    .each([](flecs::entity e, FixtureLayout& l) {
        l.pixelCount = std::clamp<int>(l.pixelCount, 1, INT_MAX);
        l.colorChannels = std::clamp<int>(l.colorChannels, 1, 4);
        l.byteCount = l.pixelCount * l.colorChannels;
        e.add<LayoutDirty>();
    });
    
    w.observer<DmxAddress>("ObserveFixtureDmxAddress").event(flecs::OnSet)
    .with<IsFixture>()
    .each([](flecs::entity fixture, DmxAddress& dmx){
        dmx.address = std::clamp<uint16_t>(dmx.address, 0, 511);
        dmx.universe = std::clamp<uint16_t>(dmx.universe, 0, 32767);
        getFixturePatch(fixture).add<DmxMapDirty>();
    });

    w.observer<>("ObserveFixtureShape")
    .with<IsFixture>()
    .with<FixtureShape>(flecs::Wildcard).event(flecs::OnSet)
    .each([](flecs::entity fixture){
        fixture.add<PixelPositionsDirty>();
        getFixturePatch(fixture).add<RenderAreaDirty>();
    });



    w.system<FixtureLayout>("UpdateFixtureLayout").with<LayoutDirty>()
    .with<IsFixture>()
    .each([](flecs::entity fixture, FixtureLayout l){
        auto w = fixture.world();

        int current = fixtureGetPixelCountt(fixture);
        
        if (current < l.pixelCount) {
            int toAdd = l.pixelCount - current;
            for (int i = 0; i < toAdd; i++) createPixel(fixture);
        } else if (current > l.pixelCount) {
            int toDelete = current - l.pixelCount;
            fixtureIteratePixels(fixture, [&](flecs::entity pixel){
                if (toDelete > 0) {
                    pixel.destruct();
                    toDelete--;
                }
            });
        }
        //trigger modified event on the FixtureShape:Shape component
        //this will cause the next observer to recalculate pixel positions
        if(flecs::entity target = fixture.target<FixtureShape>()){
            flecs::id pair_id = w.pair<FixtureShape>(target);
            fixture.modified(pair_id);
        }
        getFixturePatch(fixture).add<DmxMapDirty>(); //adjust the dmx output map
        fixture.remove<LayoutDirty>();
    });



    w.system<>("UpdateLinePixelPositions").with<FixtureShape, Line>()
    .with<PixelPositionsDirty>()
    .with<IsFixture>()
    .each([](flecs::entity fixture) {
        const Line& line = fixture.get<FixtureShape,Line>();
        updateFixturePixelPositions(fixture, [line](float r, int i, size_t c){
            return line.start + r * (line.end - line.start);
        });
        getFixturePatch(fixture).add<RenderAreaDirty>();
        fixture.remove<PixelPositionsDirty>();
    });



    w.system<>("UpdateCirclePixelPositions").with<FixtureShape, Circle>()
    .with<PixelPositionsDirty>()
    .with<IsFixture>()
    .each([](flecs::entity fixture) {
        const Circle& circle = fixture.get<FixtureShape,Circle>();
        updateFixturePixelPositions(fixture, [circle](float r, int i, int c){
            float angle = float(i) / float(c) * M_PI * 2.0;
            return glm::vec2{
                circle.center.x + cosf(angle) * circle.radius,
                circle.center.y + sinf(angle) * circle.radius
            };
        });
        getFixturePatch(fixture).add<RenderAreaDirty>();
        fixture.remove<PixelPositionsDirty>();
    });



    w.system<IsPatch>("UpdateDmxOutputMap").with<DmxMapDirty>()
    .each([](flecs::entity patch, IsPatch){
        
        flecs::entity fixtureFolder = patch.target<FixtureList>();
        flecs::entity dmxOutputFolder = patch.target<DmxOutput>();
        if(!fixtureFolder.is_valid() || !dmxOutputFolder.is_valid()) return;
        
        std::vector<uint16_t> univIds = {};
        struct FixtureToUniverse{
            flecs::entity fixture;
            std::vector<uint16_t> universes = {};
        };
        std::vector<FixtureToUniverse> fixtureToUniverseLinks;
            
        //compile a list of universes that are needed to cover all fixtures
        patchIterateFixtures(patch, [&](flecs::entity fixture, const FixtureLayout& layout){
            const DmxAddress* a = fixture.try_get<DmxAddress>();
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
        
        patchIterateDmxUniverses(patch, [&](flecs::entity dmxUniverse, const DmxUniverse& u){
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
                std::string univName = "Universe " + std::to_string(id);
                patch.world().entity()
                .child_of(dmxOutputFolder)
                .set_name(univName.c_str())
                .add<IsDmxUniverse>()
                .set<DmxUniverse>({id, 0});
            }
        }

        //Build Fixture:InUniverse-Universe relationships
        for(auto& link : fixtureToUniverseLinks) link.fixture.remove<InUniverse>(flecs::Wildcard);
        std::unordered_map<uint16_t, flecs::entity> idToUniverseEnt;

        patchIterateDmxUniverses(patch, [&](flecs::entity universe, const DmxUniverse& u){
            idToUniverseEnt[u.universeId] = universe;
        });
        for(auto& link : fixtureToUniverseLinks){
            for(auto id : link.universes){
                if(idToUniverseEnt.count(id)) link.fixture.add<InUniverse>(idToUniverseEnt[id]);
            }
        }
        patch.remove<DmxMapDirty>();
    });


    w.system<RenderArea>("UpdateRenderArea").with<RenderAreaDirty>()
    .with<IsPatch>()
    .each([](flecs::entity patch, RenderArea& ra){
        glm::vec2 min(FLT_MAX);
        glm::vec2 max(-FLT_MAX);
        bool b_hasPixels = false;
        patchIteratePixels(patch, [&](flecs::entity pixel){
             if(const Position* p = pixel.try_get<Position>()){
                min = glm::min(min, p->position);
                max = glm::max(max, p->position);
                b_hasPixels = true;
            }
        });
        if(b_hasPixels){
            ra.min = min;
            ra.max = max;
        }
        patch.remove<RenderAreaDirty>();
    });


    createPatch(w);
    createLineFixture(getPatch(w), glm::vec2(100.0, 200.0), glm::vec2(200.0, 100.0));
    createCircleFixture(getPatch(w), glm::vec2(100.0, 100.0), 50.0);
}


};//namespace PixelMapper