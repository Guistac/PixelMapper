#include "PixelMapper.h"

#include <iostream>
#include <functional>

namespace PixelMapper{

flecs::entity patch;



flecs::entity createPatch(flecs::world& w){
    auto newPatch = w.entity()
        .add<Patch>()
        .add<RenderArea>()
        .add<SelectedFixture>();

    auto dmxOutput = w.entity("DmxOutput")
        .add<DmxOutput>()
        .child_of(newPatch);
    return newPatch;
}

flecs::entity getPatch(flecs::world& w){
    return patch;
}



flecs::entity getSelectedFixture(flecs::entity patch){
    return patch.get<SelectedFixture>().fixture;
}

void selectFixture(flecs::entity patch, flecs::entity fixture){
    patch.set<SelectedFixture>({fixture});
}


void updateFixturePixels(flecs::entity e, std::function<glm::vec2(float range, int index, size_t count)> pos_func) {
    const Fixture& f = e.get<Fixture>();
    int pixelIndex = 0;
    e.children([&](flecs::entity child) {
        if(Pixel* p = child.try_get_mut<Pixel>()){
            float range = f.pixelCount > 1 ? (float)pixelIndex / (float)(f.pixelCount - 1) : 0.0f;
            p->position = pos_func(range, pixelIndex, f.pixelCount);
            pixelIndex++;
        }
    });
    e.parent().modified<RenderArea>();
}



void init(flecs::world& w){

    patch = createPatch(w);

    //the FixtureShape relationship is exclusive, like ChildOf
    w.component<FixtureShape>().add(flecs::Exclusive);


    w.observer<Fixture>("UpdateFixturePixelCount")
    .event(flecs::OnSet)
    .each([](flecs::entity e, Fixture& f) {
        auto w = e.world();
        auto q = w.query_builder<Pixel>()
        .with(flecs::ChildOf, e)
        .build();
        int current = q.count();
        
        if (current < f.pixelCount) {
            int toAdd = f.pixelCount - current;
            for (int i = 0; i < toAdd; i++) {
                w.entity()
                .child_of(e)
                .add<Pixel>();
            }
        } else if (current > f.pixelCount) {
            int toDelete = current - f.pixelCount;
            q.each([&](flecs::entity pixel, Pixel& p){
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
    });


    w.observer<>("UpdateLineFixturePixels")
    .with<FixtureShape, Line>().event(flecs::OnSet)
    .with<Fixture>()
    .each([](flecs::entity e){
        const Line& line = e.get<FixtureShape,Line>();
        updateFixturePixels(e, [line](float r, int i, size_t c){
            return line.start + r * (line.end - line.start);
        });
    });


    w.observer<>("UpdateCircleFixturePixels")
    .with<FixtureShape, Circle>().event(flecs::OnSet)
    .with<Fixture>()
    .each([](flecs::entity e){
        const Circle& circle = e.get<FixtureShape,Circle>();
        updateFixturePixels(e, [circle](float r, int i, int c){
            float angle = float(i) / float(c) * M_PI * 2.0;
            return glm::vec2{
                circle.center.x + cosf(angle) * circle.radius,
                circle.center.y + sinf(angle) * circle.radius
            };
        });
    });


    w.observer<RenderArea>("UpdatePatchRenderArea")
    .event(flecs::OnSet)
    .each([](flecs::entity e, RenderArea& ra){
        glm::vec2 min(FLT_MAX);
        glm::vec2 max(-FLT_MAX);
        bool b_hasPixels = false;
        e.children([&](flecs::entity fixture){
            if(!fixture.has<Fixture>()) return;
            fixture.children([&](flecs::entity pixel){
                if(const Pixel* p = pixel.try_get<Pixel>()){
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

    //on app start, add a default fixture to the world
    createLineFixture(getPatch(w), glm::vec2(0.0, 0.0), glm::vec2(100.0, 150.0));
}








flecs::entity createFixture(flecs::entity patch, int numPixels, int channels){
    auto newFixture = patch.world().entity()
        .set<Fixture>({numPixels}) //triggers pixel resize observer
        .add<DmxAddress>()
        .child_of(patch);
    return newFixture;
}

flecs::entity createLineFixture(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels, int channels) {
    int patchFixtureCount = patch.world().query_builder<Fixture>().with(flecs::ChildOf, patch).build().count();
    std::string fixtureName = "Line Fixture " + std::to_string(patchFixtureCount + 1);

    auto newFixture = createFixture(patch, numPixels, channels)
    .set_name(fixtureName.c_str())
    .set<FixtureShape, Line>({start, end}); //triggers line pixel position observer

    return newFixture;
}

flecs::entity createCircleFixture(flecs::entity patch, glm::vec2 center, float radius, int numPixels, int channels){
    int patchFixtureCount = patch.world().query_builder<Fixture>().with(flecs::ChildOf, patch).build().count();
    std::string fixtureName = "Circle Fixture " + std::to_string(patchFixtureCount + 1);

    auto newFixture = createFixture(patch, numPixels, channels)
    .set_name(fixtureName.c_str())
    .set<FixtureShape, Circle>({center, radius}); //triggers circle pixel position observer

    return newFixture;
}


};//namespace PixelMapper