#include "PixelMapper.h"

#include <algorithm>
#include <iostream>

#include "utils/FlecsUtils.h"




namespace PixelMapper::App{
    flecs::entity get(const flecs::world& w){
        return w.target<Is>();
    };
    struct Queries{
        flecs::query<Patch::Is> patch;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress> dmxFixtureInPatch;
        flecs::query<Fixture::Is, Fixture::Layout, Fixture::DmxAddress> fixtureInDmxUniverse;
        flecs::query<Dmx::Universe::Is, Dmx::Universe::Properties> dmxUniverseInPatch;
        flecs::query<Pixel::Is, Pixel::Position, Pixel::ColorRGBW> pixelInPatch;
        flecs::query<Pixel::Is, Pixel::Position, Pixel::ColorRGBW> pixelInFixture;
    };
    const Queries& getQueries(const flecs::world& w){
        return get(w).get<Queries>();
    }
}//namespace PixelMapper::App


namespace PixelMapper::Patch{

    flecs::entity create(flecs::entity pixelMapper){
        auto patchFolder = pixelMapper.target<App::PatchFolder>();
        if(!patchFolder.is_valid()) return flecs::entity::null();
        const auto& world = pixelMapper.world();

        auto newPatch = world.entity()
            .add<Patch::Is>()
            .add<Patch::Settings>()
            .add<Patch::RenderArea>()
            .child_of(patchFolder);

        std::string patchName = "Patch " + std::to_string(getCount(pixelMapper));
        newPatch.set_name(patchName.c_str());

        auto fixtureFolder = world.entity("FixtureFolder").child_of(newPatch);
        auto dmxOutputFolder = world.entity("DmxOutputFolder").child_of(newPatch);

        newPatch.add<Patch::FixtureFolder>(fixtureFolder);
        newPatch.add<Patch::DmxUniverseFolder>(dmxOutputFolder);
        
        return newPatch;
    }

    flecs::entity getSelected(flecs::entity pixelMapper){
        return pixelMapper.target<App::SelectedPatch>();
    }
    void select(flecs::entity pixelMapper, flecs::entity patch){
        pixelMapper.add<App::SelectedPatch>(patch);
    }

    int getCount(flecs::entity pixelMapper){
        auto patchFolder = pixelMapper.target<App::PatchFolder>();
        if(!patchFolder.is_valid()) return 0;
        const auto& queries = pixelMapper.get<App::Queries>();
        return queries.patch.set_var("parent", patchFolder).count();
    }

    void iterate(flecs::entity pixelMapper, std::function<void(flecs::entity patch)> fn){
        auto patchFolder = pixelMapper.target<App::PatchFolder>();
        if(!patchFolder.is_valid()) return;
        App::getQueries(pixelMapper.world()).patch.set_var("parent", patchFolder)
        .each([fn](flecs::entity patch, Patch::Is){
            fn(patch);
        });
    }

}//namespace PixelMapper::Patch


namespace PixelMapper::Fixture{

    flecs::entity create(flecs::entity patch, int numPixels, int channels){
        auto fixtureList = patch.target<Patch::FixtureFolder>();
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

    flecs::entity createLine(flecs::entity patch, glm::vec2 start, glm::vec2 end, int numPixels, int channels) {
        std::string fixtureName = "Line Fixture " + std::to_string(Fixture::getCountWithDmx(patch) + 1);
        auto newFixture = create(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<WithShape, Shape::Line>({start, end});
        return newFixture;
    }

    flecs::entity createCircle(flecs::entity patch, glm::vec2 center, float radius, int numPixels, int channels){
        std::string fixtureName = "Circle Fixture " + std::to_string(Fixture::getCountWithDmx(patch) + 1);
        auto newFixture = create(patch, numPixels, channels)
        .set_name(fixtureName.c_str())
        .set<WithShape, Shape::Circle>({center, radius});
        return newFixture;
    }

    void select(flecs::entity patch, flecs::entity fixture){
        patch.add<Patch::SelectedFixture>(fixture);
    }
    flecs::entity getSelected(flecs::entity patch){
        if(!patch.is_valid() || !patch.is_alive()) return flecs::entity::null();
        return patch.target<Patch::SelectedFixture>();
    }
    void clearSelection(flecs::entity patch){
        patch.remove<Patch::SelectedFixture>(flecs::Wildcard);
    }

    void setDmxProperties(flecs::entity fixture, uint16_t universe, uint16_t startAddress){
        if(!fixture.is_valid()) return;
        auto* dmx = fixture.try_get_mut<Fixture::DmxAddress>();
        if(!dmx) return;
        dmx->universe = universe;
        dmx->address = startAddress;
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

    int getCountWithDmx(flecs::entity patch){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return 0;
        return App::getQueries(patch.world()).dmxFixtureInPatch.set_var("parent", fixtureFolder).count();
    }
    void iterateWithDmx(flecs::entity patch, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        App::getQueries(patch.world()).dmxFixtureInPatch.set_var("parent", fixtureFolder)
        .each([fn](flecs::entity fixture, Fixture::Is, Fixture::Layout& layout, Fixture::DmxAddress& dmxAddress){
            fn(fixture, layout, dmxAddress);
        });
    }

    void iterateInDmxUniverse(flecs::entity patch, flecs::entity universe, std::function<void(flecs::entity fixture, Fixture::Layout&, Fixture::DmxAddress&)> fn){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        App::getQueries(patch.world()).fixtureInDmxUniverse
        .set_var("parent", fixtureFolder)
        .set_var("universe", universe)
        .each([fn](flecs::entity fixture, Fixture::Is, Fixture::Layout& layout, Fixture::DmxAddress& dmxAddress){
            fn(fixture, layout, dmxAddress);
        });
    }

}//namespace PixelMapper::Fixture


namespace PixelMapper::Pixel{

    flecs::entity create(flecs::entity fixture){
        return fixture.world().entity()
        .child_of(fixture)
        .add<Is>()
        .add<ColorRGBW>()
        .add<Position>();
    }

    void updatePositionsInFixture(flecs::entity fixture, std::function<glm::vec2(float range, int index, size_t count)> pos_func) {
        const Fixture::Layout& f = fixture.get<Fixture::Layout>();
        int pixelIndex = 0;
        iterateInFixture(fixture, [&](flecs::entity pixel, Pixel::Position& p, Pixel::ColorRGBW& c){
            float range = f.pixelCount > 1 ? (float)pixelIndex / (float)(f.pixelCount - 1) : 0.0f;
            p.position = pos_func(range, pixelIndex, f.pixelCount);
            pixelIndex++;
        });
        fixture.remove<Fixture::PixelPositionsDirty>();
        Fixture::getPatch(fixture).add<Patch::RenderAreaDirty>();
    }


    void iterateInPatch(flecs::entity patch, std::function<void(flecs::entity pixel, Position& position, ColorRGBW& color)> fn){
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if(!fixtureFolder.is_valid()) return;
        App::getQueries(patch.world()).pixelInPatch.set_var("parent", fixtureFolder)
        .each([fn](flecs::entity pixel, Is, Position position, ColorRGBW color){
            fn(pixel, position, color);
        });
    }

    int getCountInFixture(flecs::entity fixture){
        return App::getQueries(fixture.world()).pixelInFixture.set_var("parent", fixture).count();
    }

    void iterateInFixture(flecs::entity fixture, std::function<void(flecs::entity pixel, Position&, ColorRGBW&)> fn){
        App::getQueries(fixture.world()).pixelInFixture.set_var("parent", fixture)
        .each([fn](flecs::entity pixel, Is, Position& position, ColorRGBW& color){
            fn(pixel, position, color);
        });
    }
};


namespace PixelMapper::Dmx::Universe{
    void iterate(flecs::entity patch, std::function<void(flecs::entity dmxUniverse, Dmx::Universe::Properties&)> fn){
        auto dmxUniverseFolder = patch.target<Patch::DmxUniverseFolder>();
        if(!dmxUniverseFolder.is_valid()) return;
        App::getQueries(patch.world()).dmxUniverseInPatch.set_var("parent", dmxUniverseFolder)
        .each([fn](flecs::entity universe, Dmx::Universe::Is, Dmx::Universe::Properties& properties){
            fn(universe, properties);
        });
    }

    flecs::entity getSelected(flecs::entity patch){
        if(!patch.is_valid() || !patch.is_alive()) return flecs::entity::null();
        return patch.target<Patch::SelectedDmxUniverse>();
    }
    void select(flecs::entity patch, flecs::entity universe){
        patch.add<Patch::SelectedDmxUniverse>(universe);
    }

    flecs::entity create(flecs::entity patch, uint16_t universeId){
        auto dmxUniverseFolder = patch.target<Patch::DmxUniverseFolder>();
        if(!dmxUniverseFolder.is_valid()) return flecs::entity::null();
        std::string univName = "Universe " + std::to_string(universeId);
        return patch.world().entity()
            .child_of(dmxUniverseFolder)
            .set_name(univName.c_str())
            .add<Dmx::Universe::Is>()
            .add<Dmx::Universe::Channels>()
            .set<Dmx::Universe::Properties>({universeId, 0});
    }
};


namespace PixelMapper::Patch{
    void import(flecs::world& w){
        w.component<Is>();
        w.component<FixtureFolder>();
        w.component<DmxUniverseFolder>();
        w.component<SelectedFixture>();
        w.component<SelectedDmxUniverse>();
        w.component<DmxMapDirty>();
        w.component<RenderAreaDirty>();
        w.component<Settings>();
        w.component<RenderArea>();
    }
}
namespace PixelMapper::Fixture{
    void import(flecs::world& w){
        w.component<Is>();
        w.component<WithShape>();
        w.component<InUniverse>();
        w.component<LayoutDirty>();
        w.component<PixelPositionsDirty>();
        w.component<Layout>();
        w.component<DmxAddress>();
    }
}
namespace PixelMapper::Pixel{
    void import(flecs::world& w){
        w.component<Is>();
        w.component<ColorRGBW>().add(flecs::Sparse);
        w.component<Position>().add(flecs::Sparse);
    }
}
namespace PixelMapper::Dmx::Universe{
    void import(flecs::world& w){
        w.component<Is>();
        w.component<SendToController>();
        w.component<Properties>().add(flecs::Sparse);
        w.component<Channels>().add(flecs::Sparse);
    }
}
namespace PixelMapper::Shape{
    void import(flecs::world& w){
        w.component<Line>();
        w.component<Circle>();
    }
}


void PixelMapper::App::import(flecs::world& w){

    //——————————————————— COMPONENTS ——————————————————————

    w.component<Is>();
    w.component<PatchFolder>();
    w.component<SelectedPatch>();
    Patch::import(w);
    Fixture::import(w);
    Pixel::import(w);
    Dmx::Universe::import(w);
    Shape::import(w);

    //————————————————— PAIR PROPERTIES ———————————————————

    w.component<Fixture::WithShape>().add(flecs::Exclusive);
    w.component<Patch::SelectedFixture>().add(flecs::Exclusive);
    w.component<Patch::SelectedDmxUniverse>().add(flecs::Exclusive);
    w.component<SelectedPatch>().add(flecs::Exclusive);

    //———————————————————— TREE ROOT ——————————————————————

    auto pixelMapper = w.entity("PixelMapperApp");
    w.add<Is>(pixelMapper);
    auto patchFolder = w.entity("Patches").child_of(pixelMapper);
    pixelMapper.add<PatchFolder>(patchFolder);

    //————————————————————— QUERIES ———————————————————————

    Queries queries{
        .patch = w.query_builder<Patch::Is>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .dmxFixtureInPatch = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .fixtureInDmxUniverse = w.query_builder<Fixture::Is, Fixture::Layout, Fixture::DmxAddress>()
            .term().first(flecs::ChildOf).second("$parent")
            .with<Fixture::InUniverse>().second("$universe")
            .build(),
        .dmxUniverseInPatch = w.query_builder<Dmx::Universe::Is, Dmx::Universe::Properties>()
            .term().first(flecs::ChildOf).second("$parent")
            .build(),
        .pixelInPatch = w.query_builder<Pixel::Is, Pixel::Position, Pixel::ColorRGBW>()
            .term().first(flecs::ChildOf).second("$fixture")
            .term().first(flecs::ChildOf).second("$parent").src("$fixture")
            .build(),
        .pixelInFixture = w.query_builder<Pixel::Is, Pixel::Position, Pixel::ColorRGBW>()
            .term().first(flecs::ChildOf).second("$parent")
            .build()
    };
    pixelMapper.set<Queries>(queries);

    //———————————————————— OBSERVERS ——————————————————————

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

    //————————————————————— SYSTEMS ———————————————————————
    
    w.system<Fixture::Layout>("UpdateFixtureLayout").with<Fixture::LayoutDirty>()
    .kind(flecs::OnLoad)
    .with<Fixture::Is>()
    .immediate()
    .each([](flecs::entity fixture, Fixture::Layout l){
        int current = Pixel::getCountInFixture(fixture);
        if (current < l.pixelCount) {
            int toAdd = l.pixelCount - current;
            for (int i = 0; i < toAdd; i++) Pixel::create(fixture);
        } else if (current > l.pixelCount) {
            int toDelete = current - l.pixelCount;
            Pixel::iterateInFixture(fixture, [&](flecs::entity pixel, Pixel::Position& p, Pixel::ColorRGBW& c){
                if (toDelete > 0) {
                    pixel.destruct();
                    toDelete--;
                }
            });
        }
        fixture.remove<Fixture::LayoutDirty>();
        fixture.add<Fixture::PixelPositionsDirty>(); //recalculate pixel positions
        Fixture::getPatch(fixture).add<Patch::DmxMapDirty>(); //adjust the dmx output map
    });


    w.system<>("UpdateLinePixelPositions").with<Fixture::WithShape, Shape::Line>()
    .kind(flecs::PostLoad)
    .with<Fixture::PixelPositionsDirty>()
    .with<Fixture::Is>()
    .immediate()
    .each([](flecs::entity fixture) {
        const Shape::Line& line = fixture.get<Fixture::WithShape,Shape::Line>();
        Pixel::updatePositionsInFixture(fixture, [line](float r, int i, size_t c){
            return line.start + r * (line.end - line.start);
        });
    });


    w.system<>("UpdateCirclePixelPositions").with<Fixture::WithShape, Shape::Circle>()
    .kind(flecs::PostLoad)
    .with<Fixture::PixelPositionsDirty>()
    .with<Fixture::Is>()
    .immediate()
    .each([](flecs::entity fixture) {
        const Shape::Circle& circle = fixture.get<Fixture::WithShape,Shape::Circle>();
        Pixel::updatePositionsInFixture(fixture, [circle](float r, int i, int c){
            float angle = float(i) / float(c) * M_PI * 2.0;
            return glm::vec2{
                circle.center.x + cosf(angle) * circle.radius,
                circle.center.y + sinf(angle) * circle.radius
            };
        });
    });


    w.system<Patch::RenderArea>("UpdateRenderArea").with<Patch::RenderAreaDirty>()
    .kind(flecs::PreUpdate)
    .with<Patch::Is>()
    .immediate()
    .each([](flecs::entity patch, Patch::RenderArea& ra){
        glm::vec2 min(FLT_MAX);
        glm::vec2 max(-FLT_MAX);
        bool b_hasPixels = false;
        Pixel::iterateInPatch(patch, [&](flecs::entity pixel, Pixel::Position& p, Pixel::ColorRGBW& c){
            min = glm::min(min, p.position);
            max = glm::max(max, p.position);
            b_hasPixels = true;
        });
        if(b_hasPixels){
            ra.min = min;
            ra.max = max;
        }
        patch.remove<Patch::RenderAreaDirty>();
    });


    w.system<Patch::Is>("UpdateDmxOutputMap").with<Patch::DmxMapDirty>()
    .kind(flecs::PreUpdate)
    .immediate()
    .each([](flecs::entity patch, Patch::Is){

        std::unordered_map<uint16_t, flecs::entity> univsByNumber;
        std::unordered_map<flecs::entity, std::vector<uint16_t>, EntityHasher> univsByFixture;

        //compile a list of universes that are needed to cover all fixtures
        Fixture::iterateWithDmx(patch, [&](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress dmxAddress){
            int fixtureUniverseCount = 1;
            int channels = dmxAddress.address + layout.byteCount;
            while(channels > 512){ fixtureUniverseCount++; channels -= 512; }
            
            auto& fixtureUnivNumbers = univsByFixture[fixture];
            for(int i = 0; i < fixtureUniverseCount; i++){
                univsByNumber[dmxAddress.universe + i] = flecs::entity::null();
                fixtureUnivNumbers.push_back(dmxAddress.universe + i);
            }
        });

        //compare with the current list of universes, decide what to keep and delete
        std::vector<flecs::entity> toDelete;
        Dmx::Universe::iterate(patch, [&](flecs::entity universe, Dmx::Universe::Properties& properties){
            //if not in new list, mark for deletion
            if(univsByNumber.count(properties.universeId) == 0)
                toDelete.push_back(universe);
            //else store in map
            else univsByNumber[properties.universeId] = universe;
        });
        
        //Delete and add universes
        for (auto e : toDelete) e.destruct();
        for(auto& [id, universe] : univsByNumber){
            if(!universe.is_valid()){
                flecs::entity newUniverse = Dmx::Universe::create(patch, id);
                if(newUniverse.is_valid()) universe = newUniverse;
            }
        }

        //Rebuild Fixture-InUniverse relationships
        for(const auto& [fixture, univList] : univsByFixture){
            fixture.remove<Fixture::InUniverse>(flecs::Wildcard);
            for(auto id : univList){
                fixture.add<Fixture::InUniverse>(univsByNumber[id]);
            }
        }

        patch.remove<Patch::DmxMapDirty>();
    });

};//main Application Import