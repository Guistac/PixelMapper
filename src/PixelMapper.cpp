#include "PixelMapper.h"

#include <iostream>
#include <functional>
#include <algorithm>
#include <set>

namespace PixelMapper{


flecs::entity createPatch(flecs::world& w){
    auto newPatch = w.entity("TestPatch")
        .add<IsPatch>()
        .add<PatchSettings>()
        .add<RenderArea>();

    auto fixtureList = w.entity("FixtureFolder").child_of(newPatch);
    newPatch.add<FixtureList>(fixtureList);

    auto dmxOutput = w.entity("DmxOutputFolder").child_of(newPatch);    
    newPatch.add<DmxOutput>(dmxOutput);

    return newPatch;
}

flecs::entity getPatch(flecs::world& w){
    return w.lookup("TestPatch");
}

int patchGetFixtureCount(flecs::entity patch){
    auto fixtureList = patch.target<FixtureList>();
    return patch.world().query_builder<FixtureLayout>().with(flecs::ChildOf, fixtureList).build().count();
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



flecs::entity createFixture(flecs::entity patch, int numPixels, int channels){
    auto fixtureList = patch.target<FixtureList>();
    auto newFixture = patch.world().entity()
        .child_of(fixtureList)
        .add<IsFixture>()
        .set<FixtureLayout>({numPixels, channels}) //triggers pixel resize observer
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

void updateFixturePixelPositions(flecs::entity fixture, std::function<glm::vec2(float range, int index, size_t count)> pos_func) {
    const FixtureLayout& f = fixture.get<FixtureLayout>();
    int pixelIndex = 0;
    fixture.children([&](flecs::entity child) {
        if(!child.has<IsPixel>()) return;
        if(Position* p = child.try_get_mut<Position>()){
            float range = f.pixelCount > 1 ? (float)pixelIndex / (float)(f.pixelCount - 1) : 0.0f;
            p->position = pos_func(range, pixelIndex, f.pixelCount);
            pixelIndex++;
        }
    });
    fixture.parent().parent().modified<RenderArea>();
}


void resizePatchDmxOutput(flecs::entity patch){
    if(!patch.has<PatchSettings>()) return;
    auto fixtureList = patch.target<FixtureList>();
    if(!fixtureList.is_valid()) return;
    flecs::entity dmxOutput = patch.target<DmxOutput>();
    if (!dmxOutput.is_valid()) return;

    std::vector<uint16_t> univIds = {};
    struct FixtureToUniverse{
        flecs::entity fixture;
        std::vector<uint16_t> universes = {};
    };
    std::vector<FixtureToUniverse> fixtureToUniverseLinks;

    //compile a list of universes that are needed to cover all fixtures
    fixtureList.children([&](flecs::entity fixture){
        if(!fixture.has<IsFixture>()) return;
        const FixtureLayout* f = fixture.try_get<FixtureLayout>();
        const DmxAddress* a = fixture.try_get<DmxAddress>();
        if(!f || !a) return;

        int fixtureUniverseCount = 1;
        int channels = a->address + f->byteCount;
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

    //Delete universes that are not needed
    dmxOutput.children([&](flecs::entity e) {
        if (!e.has<IsDmxUniverse>() || !e.has<DmxUniverse>()) return;
        const DmxUniverse& u = e.get<DmxUniverse>();
        // If this universe ID is NOT in our new list, mark for deletion
        if (std::find(univIds.begin(), univIds.end(), u.universeId) == univIds.end()) {
            toDelete.push_back(e);
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
            .child_of(dmxOutput)
            .set_name(univName.c_str())
            .add<IsDmxUniverse>()
            .set<DmxUniverse>({id, 0});
        }
    }

    //Build Fixture:InUniverse-Universe relationships
    for(auto& link : fixtureToUniverseLinks) link.fixture.remove<InUniverse>(flecs::Wildcard);
    std::unordered_map<uint16_t, flecs::entity> idToUniverseEnt;
    patch.world().query_builder<DmxUniverse>()
    .with(flecs::ChildOf, dmxOutput)
    .build()
    .each([&](flecs::entity universe, DmxUniverse& u){
        idToUniverseEnt[u.universeId] = universe;
    });
    for(auto& link : fixtureToUniverseLinks){
        for(auto id : link.universes){
            if(idToUniverseEnt.count(id)) link.fixture.add<InUniverse>(idToUniverseEnt[id]);
        }
    }

    //Sort Universes by id
    patch.world().query_builder<DmxUniverse>()
    .with(flecs::ChildOf, dmxOutput)
    .order_by<DmxUniverse>([](flecs::entity_t e1, const DmxUniverse* u1, flecs::entity_t e2, const DmxUniverse* u2) -> int{
        return (u1->universeId > u2->universeId) - (u1->universeId < u2->universeId);
    })
    .build();
}



void init(flecs::world& w){

    //the FixtureShape relationship is exclusive, like ChildOf
    w.component<FixtureShape>().add(flecs::Exclusive);

    //only single fixture selection for now
    w.component<SelectedFixture>().add(flecs::Exclusive);
    w.component<SelectedDmxUniverse>().add(flecs::Exclusive);

    w.observer<>("UpdateFixtureDmxMap")
    .with<DmxAddress>().event(flecs::OnSet)
    .each([](flecs::entity fixture){
        DmxAddress& dmx = fixture.get_mut<DmxAddress>();
        dmx.address = std::clamp<uint16_t>(dmx.address, 0, 511);
        dmx.universe = std::clamp<uint16_t>(dmx.universe, 0, 32767);
        auto fixtureFolder = fixture.parent();
        if(!fixtureFolder.is_valid()) return;
        auto patch = fixtureFolder.parent();
        if(!patch.is_valid()) return;
        resizePatchDmxOutput(patch);
    });


    w.observer<FixtureLayout>("UpdateFixturePixelCount").event(flecs::OnSet)
    .each([](flecs::entity e, FixtureLayout& f) {

        f.byteCount = f.pixelCount * f.colorChannels;


        auto w = e.world();
        auto q = w.query_builder<>()
        .with<IsPixel>()
        .with(flecs::ChildOf, e).build();
        int current = q.count();
        
        if (current < f.pixelCount) {
            int toAdd = f.pixelCount - current;
            for (int i = 0; i < toAdd; i++) createPixel(e);
        } else if (current > f.pixelCount) {
            int toDelete = current - f.pixelCount;
            q.each([&](flecs::entity pixel){
                if (toDelete > 0) {
                    pixel.destruct();
                    toDelete--;
                }
            });
        }
        //trigger modified event on the FixtureShape:Shape component
        //this will cause the next observer to recalculate pixel positions
        if(flecs::entity target = e.target<FixtureShape>()){
            flecs::id pair_id = w.pair<FixtureShape>(target);
            e.modified(pair_id);
        }
        e.modified<DmxAddress>(); //adjust the dmx output map
    });

    


    w.observer<>("UpdateLineFixturePixels")
    .with<FixtureShape, Line>().event(flecs::OnSet)
    .with<FixtureLayout>()
    .each([](flecs::entity e){
        const Line& line = e.get<FixtureShape,Line>();
        updateFixturePixelPositions(e, [line](float r, int i, size_t c){
            return line.start + r * (line.end - line.start);
        });
    });


    w.observer<>("UpdateCircleFixturePixels")
    .with<FixtureShape, Circle>().event(flecs::OnSet)
    .with<FixtureLayout>()
    .each([](flecs::entity e){
        const Circle& circle = e.get<FixtureShape,Circle>();
        updateFixturePixelPositions(e, [circle](float r, int i, int c){
            float angle = float(i) / float(c) * M_PI * 2.0;
            return glm::vec2{
                circle.center.x + cosf(angle) * circle.radius,
                circle.center.y + sinf(angle) * circle.radius
            };
        });
    });


    w.observer<RenderArea>("UpdatePatchRenderArea").event(flecs::OnSet)
    .each([](flecs::entity e, RenderArea& ra){
        glm::vec2 min(FLT_MAX);
        glm::vec2 max(-FLT_MAX);
        bool b_hasPixels = false;
        auto fixtureList = e.target<FixtureList>();
        fixtureList.children([&](flecs::entity fixture){
            if(!fixture.has<IsFixture>()) return;
            fixture.children([&](flecs::entity pixel){
                if(!pixel.has<IsPixel>()) return;
                if(const Position* p = pixel.try_get<Position>()){
                    min = glm::min(min, p->position);
                    max = glm::max(max, p->position);
                    b_hasPixels = true;
                }
            });
        });
        if(b_hasPixels){
            ra.min = min;
            ra.max = max;
        }
    });

    createPatch(w);
    createLineFixture(getPatch(w), glm::vec2(0.0, 0.0), glm::vec2(100.0, 150.0));
}


};//namespace PixelMapper