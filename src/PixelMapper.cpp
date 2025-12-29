#include "PixelMapper.h"

namespace PixelMapper{

flecs::world world;

void init(){

    world.import<flecs::stats>();
    world.set<flecs::Rest>({});

    //register an observer with callback for FixtureLine Set or Modify
    world.observer<FixtureLine>()
        .event(flecs::OnSet) // Triggered when .set<FixtureSegment> is called
        .each([](flecs::entity parent, FixtureLine& segment) {
        
       auto childQuery = world.query_builder<Position>()
            .with<IsPixel>()
            .term().first(flecs::ChildOf).second(parent) // Correct way to define a Pair term, kinda hard to read
            .build();

        int pixelCount = childQuery.count();

        int i = 0;
        childQuery.each([&](Position& p) {
            float range = (pixelCount > 1) ? (float)i / (float)(pixelCount - 1) : 0.0f;
            p.value = segment.start + range * (segment.end - segment.start);
            i++;
        });
    });

    //on app start, add a default fixture to the world
    CreateFixture(glm::vec2(0.0, 0.0), glm::vec2(100.0, 150.0), 10);
}


// Helper to spawn a new fixture
flecs::entity CreateFixture(glm::vec2 start, glm::vec2 end, int numPixels) {

    //Create Pixels as children
    for(int i = 0; i < numPixels; i++) {
        world.entity()
            .child_of(f)
            .add<IsPixel>()
            .set<Position>({glm::vec2(0,0)})
            .set<Color>({255, 255, 255, 255, 255});
    }

    //add fixture component (triggers onFixtureUpdate observer)
    f.set<FixtureLine>({start, end, (size_t)numPixels});

    return f;
}

};