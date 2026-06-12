#include "PixelMapper.h"

#include "ImGuiCanvas.h"
#include "ImGuiHexView.h"
#include "utils/Profiling.h"

#include <iostream>


namespace PixelMapper::Shape{


void Line_guiShapeDisplay(const void* shapeProps, const ImGuiCanvas* canvas){

}

bool Line_guiShapeEdit(const void* shapeProps, const ImGuiCanvas* canvas){
    return false;
}

bool Line_guiProps(const void* shapeProps){
    return false;
}   

void Circle_guiShapeDisplay(const void* shapeProps, const ImGuiCanvas* canvas){

}

bool Circle_guiShapeEdit(const void* shapeProps, const ImGuiCanvas* canvas){
    return false;
}

bool Circle_guiProps(const void* shapeProps){
    return false;
}



};

namespace PixelMapper::Gui{

ImGuiCanvas canvas;


void import(flecs::world& w){
    w.system<>("MainWindow").kind(flecs::PreStore)
    .run([&](flecs::iter& it){
        auto application = App::get(it.world());
        if(ImGui::BeginMainMenuBar()){
            if(ImGui::BeginMenu("PixelMapper")){
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("Edit")){
                auto selectedPatch = Patch::getSelected(application);
                Patch::iterate(application, [&](flecs::entity patch){
                    bool b_selected = selectedPatch == patch;
                    ImGui::PushID(patch.id());
                    if(ImGui::MenuItem(patch.name().c_str(), "", b_selected)){
                        Patch::select(application, patch);
                    }
                    ImGui::PopID();
                });
                ImGui::Separator();
                if(ImGui::MenuItem("Create Patch")){
                    auto newPatch = Patch::create(application);
                    Patch::select(application, newPatch);
                }
                ImGui::EndMenu();
            }
            if(ImGui::BeginMenu("View")){
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        ImGui::DockSpaceOverViewport();
    });

    w.system<>("WindowFixtureList").kind(flecs::OnStore)
    .run([&](flecs::iter& it){

        flecs::entity application = App::get(it.world());
        flecs::entity selectedPatch = Patch::getSelected(application);
        flecs::entity selectedFixture = Fixture::getSelected(selectedPatch);

        if(ImGui::Begin("Fixture List")){
            if(ImGui::BeginListBox("##Fixtures", ImGui::GetContentRegionAvail())){
                if(selectedPatch.is_valid()){
                    Fixture::iterateWithDmx(selectedPatch,
                        [&](flecs::entity fixture, Fixture::Layout& layout, const Fixture::DmxAddress& dmxAddress){
                            bool b_selected = fixture == selectedFixture;
                            if(ImGui::Selectable(fixture.name().c_str(), b_selected)){
                                Fixture::select(selectedPatch, fixture);
                            }
                    });
                }
                ImGui::EndListBox();
            }
        }
        ImGui::End();

    });
    w.system<>("WindowFixtureProperties").kind(flecs::OnStore)
    .run([&](flecs::iter& it){

        flecs::entity application = App::get(it.world());
        flecs::entity selectedPatch = Patch::getSelected(application);
        flecs::entity selectedFixture = Fixture::getSelected(selectedPatch);

        if(ImGui::Begin("Fixture Properties")){ 
            if(selectedFixture.is_valid()){

                if(selectedFixture.has<Fixture::Layout>()){                
                    Fixture::Layout f = selectedFixture.get<Fixture::Layout>();
                    bool edited = false;
                    ImGui::SeparatorText("Fixture");
                    edited |= ImGui::InputInt("Pixel Count", &f.pixelCount);
                    edited |= ImGui::InputInt("Color Channels", &f.channelsPerPixel);
                    ImGui::Text("%i Bytes", f.pixelCount * f.channelsPerPixel);
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
                if(shapeType == application.world().id<Shape::Line>()){
                    Shape::Line l = selectedFixture.get<Fixture::WithShape, Shape::Line>();
                    bool edited = false;
                    ImGui::SeparatorText("Line Segment");
                    edited |= ImGui::InputFloat2("Start Position", &l.start.x, "%.1fmm");
                    edited |= ImGui::InputFloat2("End Position", &l.end.x, "%.1fmm");
                    if(edited) {
                        selectedFixture.get_mut<Fixture::WithShape, Shape::Line>() = l;
                        selectedFixture.add<Fixture::PixelPositionsDirty>();
                    }
                }
                else if(shapeType == application.world().id<Shape::Circle>()){
                    Shape::Circle c = selectedFixture.get<Fixture::WithShape, Shape::Circle>();
                    bool edited = false;
                    ImGui::SeparatorText("Circle");
                    edited |= ImGui::InputFloat2("Center", &c.center.x, "%.1fmm");
                    edited |= ImGui::InputFloat("Radius", &c.radius, 0.0, 0.0, "%.1fmm");
                    if(edited) {
                        selectedFixture.get_mut<Fixture::WithShape, Shape::Circle>() = c;
                        selectedFixture.add<Fixture::PixelPositionsDirty>();
                    }
                }

            }
        }
        ImGui::End();

    });
    w.system<>("WindowPatchEditor").kind(flecs::OnStore)
    .run([&](flecs::iter& it){

        flecs::entity application = App::get(it.world());
        flecs::entity selectedPatch = Patch::getSelected(application);
        flecs::entity selectedFixture = Fixture::getSelected(selectedPatch);

        static bool b_showFixtures = true;
        static bool b_showPixels = true;

        if (ImGui::Begin("Patch Editor", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {

            ImGui::Checkbox("Fixtures", &b_showFixtures);
            ImGui::SameLine();
            ImGui::Checkbox("Pixels", &b_showPixels);

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

                    if(b_showFixtures){
                        Fixture::iterateWithDmx(selectedPatch,
                            [&](flecs::entity fixture, const Fixture::Layout& layout, const Fixture::DmxAddress& dmxAddress){

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
                    }
        
                    if(b_showPixels){
                        Fixture::iterateWithPixelData(selectedPatch, 
                            [&](flecs::entity fixture, const Fixture::PixelData& pixelData){
                                if(pixelData.colors.size() != pixelData.positions.size()) return;
                                glm::vec2 size2(2.0);
                                for(int i = 0; i < pixelData.colors.size(); i++){
                                    const auto& pos = canvas.canvasToScreen(pixelData.positions[i]);
                                    const auto& col = pixelData.colors[i];
                                    drawing->AddRectFilled(
                                        pos-size2,
                                        pos+size2,
                                        IM_COL32(col.r, col.g, col.b, 255));
                                }
                        });
                    }

                    if(b_showFixtures){
                        //double click to add fixtures
                        glm::vec2 canvasClickPos;
                        if (canvas.isDoubleClicked(canvasClickPos)) {
                            glm::vec3 pos = glm::vec3(canvasClickPos, 0.0);
                            Fixture::createLine(selectedPatch, pos, pos + glm::vec3(100, 100, 0.0));
                        }
                        if (canvas.isDoubleClicked(canvasClickPos, ImGuiMouseButton_Right)) {
                            glm::vec3 pos = glm::vec3(canvasClickPos, 0.0);
                            Fixture::createCircle(selectedPatch, pos, 100);
                        }

                        //drag handles to move and deform selected Fixture
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0, 0.0, 0.0, 1.0));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0, 1.0, 1.0, 1.0));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5, 0.5, 0.5, 1.0));
                        if(selectedFixture.is_valid()){
                            flecs::entity currentShapeType = selectedFixture.target<Fixture::WithShape>();
                            bool edited = false;
                            if(currentShapeType == selectedFixture.world().id<Shape::Line>()){
                                Shape::Line l = selectedFixture.get<Fixture::WithShape, Shape::Line>();
                                edited |= canvas.dragHandle("##Start", l.start, 5.0);
                                edited |= canvas.dragHandle("##End", l.end, 5.0);
                                if(edited) {
                                    selectedFixture.get_mut<Fixture::WithShape, Shape::Line>() = l;
                                    selectedFixture.add<Fixture::PixelPositionsDirty>();
                                }
                            }
                            else if(currentShapeType == selectedFixture.world().id<Shape::Circle>()){
                                Shape::Circle c = selectedFixture.get<Fixture::WithShape, Shape::Circle>();
                                edited |= canvas.dragHandle("##Center", c.center, 5.0);
                                glm::vec3 radiusHandle = c.center + glm::vec3(c.radius, 0.0, 0.0);
                                if(canvas.dragHandle("##Radius", radiusHandle, 5.0)){
                                    c.radius = glm::distance(c.center, radiusHandle);
                                    edited = true;
                                }
                                if(edited) {
                                    selectedFixture.get_mut<Fixture::WithShape, Shape::Circle>() = c;
                                    selectedFixture.add<Fixture::PixelPositionsDirty>();
                                }
                            }
                        }
                        ImGui::PopStyleColor(3);
                    }
                }//is patch alive

                canvas.end();
            }
        }
        ImGui::End();

    });
    w.system<>("WindowArtnetData").kind(flecs::OnStore)
    .run([&](flecs::iter& it){

        flecs::entity application = App::get(it.world());
        flecs::entity selectedPatch = Patch::getSelected(application);
        flecs::entity selectedFixture = Fixture::getSelected(selectedPatch);
        flecs::entity selectedUniverse = Artnet::Universe::getSelected(selectedPatch);


        if(ImGui::Begin("Artnet Data")){

            if(ImGui::BeginListBox(
                "##UniverseList",
                ImVec2(ImGui::CalcTextSize("Universe 999").x + ImGui::GetStyle().ScrollbarSize,
                ImGui::GetContentRegionAvail().y))
            ){
                if(selectedPatch.is_valid()){
                    Artnet::Universe::iterate(selectedPatch, 
                        [&](flecs::entity universe, const Artnet::Universe::Properties& properties){
                            bool b_selected = universe == selectedUniverse;
                            if(ImGui::Selectable(universe.name(), b_selected)){
                                Artnet::Universe::select(selectedPatch, universe);
                            }
                    });
                }
                ImGui::EndListBox();
            }

            ImGui::SameLine();
            ImGui::BeginChild("##dmxHex", ImGui::GetContentRegionAvail());

            if(selectedUniverse.is_valid()){
                std::vector<MappedField> fields;
                uint32_t colors[2] = {
                    IM_COL32(50,50,140,255),
                    IM_COL32(30,30,70,255)
                };
                const auto* univProps = selectedUniverse.try_get<Artnet::Universe::Properties>();
                Fixture::iterateInDmxUniverse(selectedPatch, selectedUniverse,
                    [&](flecs::entity fixture, const Fixture::Layout& layout, const Fixture::DmxAddress& dmxAddress){
                        int offset;
                        int count = 0;
                        if(dmxAddress.universe == univProps->universeId) {
                            offset = dmxAddress.address;
                            count = layout.pixelCount * layout.channelsPerPixel;
                            if(offset + count > 512) count -= offset + count - 512;
                        }
                        else if(dmxAddress.universe < univProps->universeId){
                            offset = 0;
                            count = layout.pixelCount * layout.channelsPerPixel - (512 - dmxAddress.address);
                            for(int i = dmxAddress.universe + 1; i < univProps->universeId; i++) count -= 512;
                        }
                        if(count <= 0) return;
                        fields.push_back(MappedField{
                            .Name = fixture.name().c_str(),
                            .Count = count,
                            .Offset = offset,
                            .b_selected = fixture == selectedFixture
                        });
                });
                std::sort(fields.begin(), fields.end(), [](const MappedField& a, const MappedField& b) -> bool{
                    return a.Offset < b.Offset;
                });
                for(int i = 0; i < fields.size(); i++){
                    fields[i].Color = fields[i].b_selected ? IM_COL32(127, 127, 0, 255) : colors[i % 2];
                }
                auto& channels = selectedUniverse.get<Artnet::Universe::Channels>();
                const MappedField* clickedField = nullptr;
                if(DrawHexViewer(channels.channels, 512, fields, &clickedField)){
                    if(clickedField){
                        auto clickedFixture = selectedPatch.target<Patch::FixtureFolder>().lookup(clickedField->Name.c_str());
                        Fixture::select(selectedPatch, clickedFixture);
                    }
                    else Fixture::clearSelection(selectedPatch);
                }

            }
            ImGui::EndChild();
            

        }
        ImGui::End();


    });
    w.system<>("WindowArtnetDevices").kind(flecs::OnStore)
    .run([&](flecs::iter& it){

        flecs::entity application = App::get(it.world());
        flecs::entity selectedPatch = Patch::getSelected(application);
        flecs::entity selectedArtnetDevice = Artnet::Device::getSelected(selectedPatch);


        if(ImGui::Begin("Artnet Devices")){
            if(ImGui::BeginListBox(
                "##ArtnetDeviceList",
                ImVec2(ImGui::CalcTextSize("Universe 999").x + ImGui::GetStyle().ScrollbarSize,
                ImGui::GetContentRegionAvail().y))
            ){
                if(selectedPatch.is_valid()){
                    Artnet::Device::iterateInPatch(selectedPatch, 
                        [&](flecs::entity device, const Artnet::Device::Settings& settings){
                            bool b_selected = device == selectedArtnetDevice;
                            if(ImGui::Selectable(device.name(), b_selected)){
                                Artnet::Device::select(selectedPatch, device);
                            }
                    });
                }
                ImGui::EndListBox();
            }

        }
        ImGui::End();

    });



}

};//namespace PixelMapper