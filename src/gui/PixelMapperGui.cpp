#include "PixelMapper.h"

#include "ImGuiCanvas.h"
#include "ImGuiHexView.h"

namespace PixelMapper{

void menubar(flecs::world& w){

    flecs::entity pixelMapper = Application::get(w);

    if(ImGui::BeginMenu("PixelMapper")){
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Edit")){
        auto selectedPatch = Application::getSelectedPatch(pixelMapper);
        Application::iteratePatches(pixelMapper, [&](flecs::entity patch){
            bool b_selected = selectedPatch == patch;
            ImGui::PushID(patch.id());
            if(ImGui::MenuItem(patch.name().c_str(), "", b_selected)){
                Application::selectPatch(pixelMapper, patch);
            }
            ImGui::PopID();
        });
        ImGui::Separator();
        if(ImGui::MenuItem("Create Patch")){
            auto newPatch = Application::createPatch(pixelMapper);
            Application::selectPatch(pixelMapper, newPatch);
        }
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("View")){
        ImGui::EndMenu();
    }
}


ImGuiCanvas canvas;

void gui(flecs::world& w) {

    flecs::entity pixelMapper = Application::get(w);
    flecs::entity selectedPatch = Application::getSelectedPatch(pixelMapper);
    flecs::entity selectedFixture = Patch::getSelectedFixture(selectedPatch);
    flecs::entity selectedUniverse = Patch::getSelectedUniverse(selectedPatch);

    if(ImGui::Begin("Fixture List")){
        if(ImGui::BeginListBox("##Fixtures", ImGui::GetContentRegionAvail())){
            if(selectedPatch.is_valid()){
                Patch::iterateDmxFixtures(selectedPatch, [&](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress& dmxAddress){
                    bool b_selected = fixture == selectedFixture;
                    if(ImGui::Selectable(fixture.name().c_str(), b_selected)){
                        Patch::selectFixture(selectedPatch, fixture);
                    }
                });
            }
            ImGui::EndListBox();
        }
    }
    ImGui::End();


    if(ImGui::Begin("Fixture Properties")){ 
        if(selectedFixture.is_valid()){

            if(selectedFixture.has<Fixture::Layout>()){                
                Fixture::Layout f = selectedFixture.get<Fixture::Layout>();
                bool edited = false;
                ImGui::SeparatorText("Fixture");
                edited |= ImGui::InputInt("Pixel Count", &f.pixelCount);
                edited |= ImGui::InputInt("Color Channels", &f.colorChannels);
                ImGui::Text("%i Bytes", f.byteCount);
                if(edited) selectedFixture.set<Fixture::Layout>(f);
            }

            if(selectedFixture.has<Fixture::DmxAddress>()){
                Fixture::DmxAddress dmx = selectedFixture.get<Fixture::DmxAddress>();
                bool edited = false;
                ImGui::SeparatorText("Dmx Address");
                uint16_t step = 1;
                uint16_t stepFast = 10;
                edited |= ImGui::InputScalar("Start Universe", ImGuiDataType_U16, &dmx.universe, &step, &stepFast);
                edited |= ImGui::InputScalar("Start Address", ImGuiDataType_U16, &dmx.address, &step, &stepFast);
                if(edited){
                    selectedFixture.set<Fixture::DmxAddress>(dmx);
                }
            }

            flecs::entity shapeType = selectedFixture.target<Fixture::WithShape>();
            if(shapeType == w.id<Shape::Line>()){
                Shape::Line& l = selectedFixture.get_mut<Fixture::WithShape, Shape::Line>();
                bool edited = false;
                ImGui::SeparatorText("Line Segment");
                edited |= ImGui::InputFloat2("Start Position", &l.start.x, "%.1fmm");
                edited |= ImGui::InputFloat2("End Position", &l.end.x, "%.1fmm");
                if(edited) selectedFixture.add<Fixture::PixelPositionsDirty>();
            }
            else if(shapeType == w.id<Shape::Circle>()){
                Shape::Circle& c = selectedFixture.get_mut<Fixture::WithShape, Shape::Circle>();
                bool edited = false;
                ImGui::SeparatorText("Circle");
                edited |= ImGui::InputFloat2("Center", &c.center.x, "%.1fmm");
                edited |= ImGui::InputFloat("Radius", &c.radius, 0.0, 0.0, "%.1fmm");
                if(edited) selectedFixture.add<Fixture::PixelPositionsDirty>();
            }

        }
    }
    ImGui::End();

    

    if (ImGui::Begin("Patch Editor", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {

        ImGui::SetNextItemAllowOverlap();
        if (ImDrawList* drawing = canvas.begin("Canvas", ImGui::GetContentRegionAvail())) {

            //draw background with grid
            canvas.drawGrid(100.0, 0xFF333333, 0xFF000000);

            if(selectedPatch.is_valid()){

                //draw RenderArea
                if(auto renderArea = selectedPatch.try_get<Patch::RenderArea>()){
                    drawing->AddRectFilled(
                        canvas.canvasToScreen(renderArea->min),
                        canvas.canvasToScreen(renderArea->max),
                        0x66000000);
                }

                //draw all fixtures
                Patch::iterateDmxFixtures(selectedPatch, [&](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress& dmxAddress){

                    flecs::entity currentShapeType = fixture.target<Fixture::WithShape>();

                    uint32_t fixtureColor = 0xFF0000FF;
                    if(fixture == selectedFixture) fixtureColor = 0xFF00FFFF;

                    if(currentShapeType == fixture.world().id<Shape::Line>()){
                        const Shape::Line& l = fixture.get<Fixture::WithShape, Shape::Line>();
                        drawing->AddLine(
                            canvas.canvasToScreen(l.start),
                            canvas.canvasToScreen(l.end),
                            fixtureColor, 5.0);
                    }
                    else if(currentShapeType == fixture.world().id<Shape::Circle>()){
                        const Shape::Circle& c = fixture.get<Fixture::WithShape, Shape::Circle>();
                        drawing->AddCircle(
                            canvas.canvasToScreen(c.center),
                            canvas.canvasSizeToScreenSize(c.radius),
                            fixtureColor,
                            layout.pixelCount,
                            5.0);
                    }
                });
    
                Patch::iteratePixels(selectedPatch, [&](flecs::entity pixel, Pixel::Position& p, Pixel::ColorRGBW& c){
                    drawing->AddCircleFilled(canvas.canvasToScreen(p.position), 2.0f, 0xFF000000);
                });

                //double click to add fixtures
                glm::vec2 canvasClickPos;
                if (canvas.isDoubleClicked(canvasClickPos)) {
                    Fixture::createLine(selectedPatch, canvasClickPos, canvasClickPos + glm::vec2(100, 100));
                }
                if (canvas.isDoubleClicked(canvasClickPos, ImGuiMouseButton_Right)) {
                    Fixture::createCircle(selectedPatch, canvasClickPos, 100);
                }

                //drag handles to move and deform fixtures
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0, 0.0, 0.0, 1.0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0, 1.0, 1.0, 1.0));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5, 0.5, 0.5, 1.0));
                w.defer_begin();
                Patch::iterateDmxFixtures(selectedPatch, [](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress& dmxAddress){
                    flecs::entity currentShapeType = fixture.target<Fixture::WithShape>();
                    ImGui::PushID(fixture.id());
                    bool edited = false;
                    if(currentShapeType == fixture.world().id<Shape::Line>()){
                        Shape::Line& l = fixture.get_mut<Fixture::WithShape, Shape::Line>();
                        edited |= canvas.dragHandle("##Start", l.start, 5.0);
                        edited |= canvas.dragHandle("##End", l.end, 5.0);
                        if(edited) fixture.add<Fixture::PixelPositionsDirty>();
                    }
                    else if(currentShapeType == fixture.world().id<Shape::Circle>()){
                        Shape::Circle& c = fixture.get_mut<Fixture::WithShape, Shape::Circle>();
                        edited |= canvas.dragHandle("##Center", c.center, 5.0);
                        glm::vec2 radiusHandle = c.center + glm::vec2(c.radius, 0.0);
                        if(canvas.dragHandle("##Radius", radiusHandle, 5.0)){
                            c.radius = glm::distance(c.center, radiusHandle);
                            edited = true;
                        }
                        if(edited) fixture.add<Fixture::PixelPositionsDirty>();
                    }
                    ImGui::PopID();
                });
                w.defer_end();
                ImGui::PopStyleColor(3);

            }//is patch alive


            canvas.end();
        }
    }
    ImGui::End();



    if(ImGui::Begin("Dmx Map")){

        if(ImGui::BeginListBox("##UniverseList", ImVec2(ImGui::CalcTextSize("Universe 99999").x, ImGui::GetContentRegionAvail().y))){
            if(selectedPatch.is_valid()){
                Patch::iterateDmxUniverses(selectedPatch, [&](flecs::entity universe, Dmx::Universe::Properties& properties){
                    bool b_selected = universe == selectedUniverse;
                    if(ImGui::Selectable(universe.name(), b_selected)){
                        Patch::selectUniverse(selectedPatch, universe);
                    }
                });
            }
            ImGui::EndListBox();
            ImGui::SameLine();

            ImGui::BeginChild("##dmxHex", ImGui::GetContentRegionAvail());


            if(selectedUniverse.is_valid()){
                std::vector<MappedField> fields;
                uint32_t colors[2] = {
                    IM_COL32(50,50,140,255),
                    IM_COL32(30,30,70,255)
                };
                int fixtureCount = 0;
                const auto* univProps = selectedUniverse.try_get<Dmx::Universe::Properties>();
                Patch::iterateFixturesInUniverse(selectedPatch, selectedUniverse, [&](flecs::entity fixture, Fixture::Layout& layout, Fixture::DmxAddress& dmxAddress){
                    bool b_selected = selectedFixture == fixture;

                    int offset;
                    int count = 0;
                    if(dmxAddress.universe == univProps->universeId) {
                        offset = dmxAddress.address;
                        count = layout.byteCount;
                        if(offset + count > 512) count -= offset + count - 512;
                    }
                    else if(dmxAddress.universe < univProps->universeId){
                        offset = 0;
                        count = layout.byteCount - (512 - dmxAddress.address);
                        for(int i = dmxAddress.universe + 1; i < univProps->universeId; i++) count -= 512;
                    }
                    if(count <= 0) return;
                    fields.push_back(MappedField{
                        .Name = fixture.name().c_str(),
                        .Count = count,
                        .Offset = offset,
                        .Color = b_selected ? IM_COL32(127, 127, 0, 255) : colors[fixtureCount % 2]
                    });
                    fixtureCount++;
                });
                auto& channels = selectedUniverse.get<Dmx::Universe::Channels>();
                const MappedField* clickedField = nullptr;
                if(DrawHexViewer(channels.channels, 512, fields, &clickedField)){
                    if(clickedField == nullptr) Patch::removeFixtureSelection(selectedPatch);
                    else {
                        auto clickedFixture = selectedPatch.target<Patch::FixtureFolder>().lookup(clickedField->Name.c_str());
                        Patch::selectFixture(selectedPatch, clickedFixture);
                    }
                }

            }
            ImGui::EndChild();

        }

    }
    ImGui::End();
}

};//namespace PixelMapper