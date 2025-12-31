#include "PixelMapper.h"

#include <iostream>

namespace PixelMapper{

flecs::world world;
flecs::entity patch;

void init(){

    world.import<flecs::stats>();
    world.set<flecs::Rest>({});

    patch = createPatch();


    world.observer<Fixture>("UpdateFixturePixelCount")
    .event(flecs::OnSet)
    .each([](flecs::entity fixture, Fixture& f) {
        auto q = fixture.world().query_builder<Pixel>()
        .with(flecs::ChildOf, fixture)
        .build();
        int current = q.count();
        
        if (current < f.pixelCount) {
            int toAdd = f.pixelCount - current;
            for (int i = 0; i < toAdd; i++) {
                world.entity()
                .child_of(fixture)
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
    });


    world.system<const Fixture, const Line>("UpdateLinePixels")
    .detect_changes()
    .each([](flecs::entity e, const Fixture& f, const Line& l) {
        auto pixels = e.world().query_builder<Pixel>().with(flecs::ChildOf, e).build();
        pixels.each([&](flecs::iter& it, size_t index, Pixel& p) {
            float range = (f.pixelCount > 1) ? (float)index / (float)(f.pixelCount - 1) : 0.0f;
            p.position = l.start + range * (l.end - l.start);
        });
    });


    world.system<const Fixture, const Circle>("UpdateCirclePixels")
    .detect_changes()
    .each([](flecs::entity e, const Fixture& f, const Circle& c) {
        auto pixels = e.world().query_builder<Pixel>().with(flecs::ChildOf, e).build();
        pixels.each([&](flecs::iter& it, size_t index, Pixel& p) {
            float t = (f.pixelCount > 1) ? (float)index / (float)f.pixelCount : 0.0f;
            float angle = t * M_PI * 2.0;
            p.position.x = c.center.x + cosf(angle) * c.radius;
            p.position.y = c.center.y + sinf(angle) * c.radius;
        });
    });


    //on app start, add a default fixture to the world
    createLineFixture(getPatch(), glm::vec2(0.0, 0.0), glm::vec2(100.0, 150.0));
}





flecs::entity createPatch(){
    auto newPatch = world.entity("Patch")
        .add<Patch>()
        .add<RenderArea>()
        .add<SelectedFixture>();

    auto dmxOutput = world.entity("DmxOutput")
        .add<DmxOutput>()
        .child_of(newPatch);

    return newPatch;
}

flecs::entity getPatch(){
    return patch;
}




flecs::entity createLineFixture(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels, int channels) {

    int patchFixtureCount = world.query_builder<Fixture>().with(flecs::ChildOf, patch).build().count();
    std::string fixtureName = "Line Fixture " + std::to_string(patchFixtureCount + 1);

    auto newFixture = world.entity(fixtureName.c_str())
        .set<Fixture>({numPixels}) //triggers pixel creation observer
        .add<DmxAddress>()
        .child_of(patch);

    flecs::entity lineComponent = newFixture.set<Line>({start, end});   //triggers pixel position compute

    return newFixture;
}

flecs::entity createCircleFixture(flecs::entity patch, glm::vec2 center, float radius, int numPixels, int channelsPerPixel){
    
    int patchFixtureCount = world.query_builder<Fixture>().with(flecs::ChildOf, patch).build().count();
    std::string fixtureName = "Circle Fixture " + std::to_string(patchFixtureCount + 1);

    auto newFixture = world.entity(fixtureName.c_str())
        .set<Fixture>({numPixels}) //triggers pixel creation observer
        .add<DmxAddress>()
        .child_of(patch);

    flecs::entity lineComponent = newFixture.set<Circle>({center, radius});   //triggers pixel position compute

    return newFixture;
}

flecs::entity getSelectedFixture(flecs::entity patch){
    return patch.get<SelectedFixture>().fixture;
}

void selectFixture(flecs::entity patch, flecs::entity fixture){
    patch.set<SelectedFixture>({fixture});
}


};