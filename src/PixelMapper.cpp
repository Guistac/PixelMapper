#include "PixelMapper.h"

#include <iostream>

namespace PixelMapper{

flecs::world world;
flecs::entity patch;

void init(){

    world.import<flecs::stats>();
    world.set<flecs::Rest>({});

    patch = createPatch();

    //register an observer with callback for Fixture and Line Modify
    world.observer<Fixture, Line>()
        .event(flecs::OnSet) // Triggered when .set<FixtureSegment> is called
        .each([](flecs::entity parent, Fixture& fixture, Line& line) {

        //delete all pixels
        world.query_builder<Pixel>()
            .with(flecs::ChildOf, parent)
            .build()
            .each([](flecs::entity e, const Pixel& p){
                e.destruct();
            });

        //create pixels and compute their position
        for(int i = 0; i < fixture.pixelCount; i++) {
            float range = (fixture.pixelCount > 1) ? (float)i / (float)(fixture.pixelCount - 1) : 0.0f;
            world.entity()
                .child_of(parent)
                .set<Pixel>({.position = line.start + range * (line.end - line.start)});
        }
    });

    /*
    world.system("EncodeDmxOutput")
        .with<isPatch>()
        .with<IsSelected>()
        .each([](const flecs::entity& parent){

            auto fixtureQuery = world.query_builder<FixtureLine, DmxAddress>()
                .with<IsFixture>()
                .with(flecs::ChildOf, parent)
                .build();
            
            

        });
*/

    //on app start, add a default fixture to the world
    createFixture(getPatch(), glm::vec2(0.0, 0.0), glm::vec2(100.0, 150.0));
}





flecs::entity createPatch(){
    auto newPatch = world.entity("Patch")
        .add<Patch>()
        .add<SelectedFixture>();

    auto dmxOutput = world.entity("DmxOutput")
        .add<DmxOutput>()
        .child_of(newPatch);

    return newPatch;
}

flecs::entity getPatch(){
    return patch;
}




// Helper to spawn a new fixture in the selected Patch
flecs::entity createFixture(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels, int channels) {

    int patchFixtureCount = world.query_builder<Fixture>().with(flecs::ChildOf, patch).build().count();
    std::string fixtureName = "Fixture " + std::to_string(patchFixtureCount + 1);

    auto newFixture = world.entity(fixtureName.c_str())
        .add<DmxAddress>()
        .child_of(patch);

    //Create Pixels as children
    for(int i = 0; i < numPixels; i++) {
        world.entity()
            .child_of(newFixture)
            .add<Pixel>();
    }

    //add fixture component (triggers onFixtureUpdate observer)
    newFixture.set<Fixture>({numPixels, channels});
    newFixture.set<Line>({start, end});

    return newFixture;
}

flecs::entity getSelectedFixture(flecs::entity patch){
    return patch.get<SelectedFixture>().fixture;
}

void selectFixture(flecs::entity patch, flecs::entity fixture){
    patch.set<SelectedFixture>({fixture});
}


};