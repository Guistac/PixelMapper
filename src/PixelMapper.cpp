#include "PixelMapper.h"

#include <iostream>

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


    w.observer<>("UpdateFixturePixelPositions")
    .with<FixtureShape>(flecs::Wildcard)
    .event(flecs::OnSet)
    .each([](flecs::entity e) {

        if(!e.has<Fixture>()) return;
        const Fixture& f = e.get<Fixture>();

        auto w = e.world();
        auto pixelQuery = w.query_builder<Pixel>().with(flecs::ChildOf, e).build();

        flecs::entity currentShapeType = e.target<FixtureShape>();
        if(currentShapeType == w.id<Line>()){
            const Line& l = e.get<FixtureShape, Line>();
            pixelQuery.each([f,l](flecs::iter& it, size_t index, Pixel& p) {
                float range = (f.pixelCount > 1) ? (float)index / (float)(f.pixelCount - 1) : 0.0f;
                p.position = l.start + range * (l.end - l.start);
            });
        }
        else if(currentShapeType == w.id<Circle>()){
            const Circle& c = e.get<FixtureShape, Circle>();
            pixelQuery.each([f,c](flecs::iter& it, size_t index, Pixel& p) {
                float t = (f.pixelCount > 1) ? (float)index / (float)f.pixelCount : 0.0f;
                float angle = t * M_PI * 2.0;
                p.position.x = c.center.x + cosf(angle) * c.radius;
            p.position.y = c.center.y + sinf(angle) * c.radius;
            });
        }

        //trigger the render area observer to recalculate pixel bounds
        e.parent().modified<RenderArea>();
    });


    w.observer<RenderArea>("UpdatePatchRenderArea")
    .event(flecs::OnSet)
    .each([](flecs::entity e, RenderArea& ra){
        auto pixelQuery = e.world().query_builder<const Pixel>()
        .with(flecs::ChildOf, e).up(flecs::ChildOf).build();
        glm::vec2 min(FLT_MAX, FLT_MAX);
        glm::vec2 max(FLT_MIN, FLT_MIN);
        pixelQuery.each([&](const Pixel& p) {
            min = glm::min(min, p.position);
            max = glm::max(max, p.position);
        });
        ra.min = min;
        ra.max = max;
    });

    //on app start, add a default fixture to the world
    createLineFixture(getPatch(w), glm::vec2(0.0, 0.0), glm::vec2(100.0, 150.0));
}








flecs::entity createFixture(flecs::entity patch, int numPixels, int channels){
    auto newFixture = patch.world().entity()
        .set<Fixture>({numPixels}) //triggers pixel creation observer
        .add<DmxAddress>()
        .child_of(patch);
    return newFixture;
}

flecs::entity createLineFixture(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels, int channels) {
    int patchFixtureCount = patch.world().query_builder<Fixture>().with(flecs::ChildOf, patch).build().count();
    std::string fixtureName = "Line Fixture " + std::to_string(patchFixtureCount + 1);

    auto newFixture = createFixture(patch, numPixels, channels)
    .set_name(fixtureName.c_str())
    .set<FixtureShape, Line>({start, end});

    return newFixture;
}

flecs::entity createCircleFixture(flecs::entity patch, glm::vec2 center, float radius, int numPixels, int channels){
    int patchFixtureCount = patch.world().query_builder<Fixture>().with(flecs::ChildOf, patch).build().count();
    std::string fixtureName = "Circle Fixture " + std::to_string(patchFixtureCount + 1);

    auto newFixture = createFixture(patch, numPixels, channels)
    .set_name(fixtureName.c_str())
    .set<FixtureShape, Circle>({center, radius});

    return newFixture;
}


};