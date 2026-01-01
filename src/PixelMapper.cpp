#include "PixelMapper.h"

#include <iostream>
#include <functional>
#include <algorithm>

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

    std::vector<uint16_t> univIds = {};
    fixtureList.children([&](flecs::entity fixture){
        if(!fixture.has<IsFixture>()) return;
        const FixtureLayout* f = fixture.try_get<FixtureLayout>();
        const DmxAddress* a = fixture.try_get<DmxAddress>();
        if(!f || !a) return;
        int fixtureUniverseCount = 1;
        int channels = a->address + f->byteCount;
        while(channels > 512){
            fixtureUniverseCount++;
            channels -= 512;
        }
        for(int i = 0; i < fixtureUniverseCount; i++){
            univIds.push_back(a->universe + i);
        }
    });
    //sort the universe ids and remove duplicates
    std::sort(univIds.begin(), univIds.end());
    auto last = std::unique(univIds.begin(), univIds.end());
    univIds.erase(last, univIds.end());

    flecs::entity dmxOutput = patch.target<DmxOutput>();


    std::cout << "Patch Needs Universes: ";
    for(auto u : univIds){
        std::cout << u << " ";
    }
    std::cout << std::endl;
}



void init(flecs::world& w){

    //the FixtureShape relationship is exclusive, like ChildOf
    w.component<FixtureShape>().add(flecs::Exclusive);

    //only single fixture selection for now
    w.component<SelectedFixture>().add(flecs::Exclusive);

    w.observer<DmxAddress>("UpdateFixtureDmxMap").event(flecs::OnSet)
    .with<FixtureLayout>()
    .each([](flecs::entity fixture, DmxAddress& dmx){
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