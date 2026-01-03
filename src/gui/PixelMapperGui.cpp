#include "PixelMapper.h"

#include "ImGuiCanvas.h"
#include <iostream>

namespace PixelMapper{


ImGuiCanvas canvas;

void gui(flecs::world& w) {

    if(ImGui::Begin("Fixture List")){
        if(ImGui::BeginListBox("##Fixtures", ImGui::GetContentRegionAvail())){
            auto selectedPatch = getPatch(w);
            auto selectedFixture = getSelectedFixture(selectedPatch);
            patchIterateFixtures(selectedPatch, [&](flecs::entity fixture, const FixtureLayout& layout){
                bool b_selected = fixture == selectedFixture;
                if(ImGui::Selectable(fixture.name().c_str(), b_selected)){
                    selectFixture(selectedPatch, fixture);
                }
            });
            ImGui::EndListBox();
        }
    }
    ImGui::End();


    if(ImGui::Begin("Fixture Properties")){
       
        auto selectedPatch = getPatch(w);
        auto selectedFixture = getSelectedFixture(selectedPatch);

        if(selectedFixture.is_valid()){

            if(selectedFixture.has<FixtureLayout>()){                
                FixtureLayout f = selectedFixture.get<FixtureLayout>();
                bool edited = false;
                ImGui::SeparatorText("Fixture");
                edited |= ImGui::InputInt("Pixel Count", &f.pixelCount);
                edited |= ImGui::InputInt("Color Channels", &f.colorChannels);
                ImGui::Text("%i Bytes", f.byteCount);
                if(edited) selectedFixture.set<FixtureLayout>(f);
            }

            if(selectedFixture.has<DmxAddress>()){
                DmxAddress dmx = selectedFixture.get<DmxAddress>();
                bool edited = false;
                ImGui::SeparatorText("Dmx Address");
                edited |= ImGui::InputScalar("Start Universe", ImGuiDataType_U16, &dmx.universe);
                edited |= ImGui::InputScalar("Start Address", ImGuiDataType_U16, &dmx.address);
                if(edited){
                    selectedFixture.set<DmxAddress>(dmx);
                }
            }

            flecs::entity shapeType = selectedFixture.target<FixtureShape>();
            if(shapeType == w.id<Line>()){
                Line l = selectedFixture.get<FixtureShape, Line>();
                bool edited = false;
                ImGui::SeparatorText("Line Segment");
                edited |= ImGui::InputFloat2("Start Position", &l.start.x, "%.1fmm");
                edited |= ImGui::InputFloat2("End Position", &l.end.x, "%.1fmm");
                if(edited) selectedFixture.set<FixtureShape, Line>(l);
            }
            else if(shapeType == w.id<Circle>()){
                Circle c = selectedFixture.get<FixtureShape, Circle>();
                bool edited = false;
                ImGui::SeparatorText("Circle");
                edited |= ImGui::InputFloat2("Center", &c.center.x, "%.1fmm");
                edited |= ImGui::InputFloat("Radius", &c.radius, 0.0, 0.0, "%.1fmm");
                if(edited) selectedFixture.set<FixtureShape, Circle>(c);
            }

        }
    }
    ImGui::End();

    

    if (ImGui::Begin("Patch Editor", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {

        ImGui::SetNextItemAllowOverlap();
        if (ImDrawList* drawing = canvas.begin("Canvas", ImGui::GetContentRegionAvail())) {
            
            auto selectedPatch = getPatch(w);
            auto selectedFixture = getSelectedFixture(selectedPatch);

            //draw background with grid
            canvas.drawGrid(100.0, 0xFF333333, 0xFF000000);

            //draw RenderArea
            if(auto renderArea = selectedPatch.try_get<RenderArea>()){
                drawing->AddRectFilled(
                    canvas.canvasToScreen(renderArea->min),
                    canvas.canvasToScreen(renderArea->max),
                    0x66000000);
            }

            //draw all fixtures
            patchIterateFixtures(selectedPatch, [&](flecs::entity fixture, const FixtureLayout& layout){
                flecs::entity currentShapeType = fixture.target<FixtureShape>();

                uint32_t fixtureColor = 0xFF0000FF;
                if(fixture == selectedFixture) fixtureColor = 0xFF00FFFF;

                if(currentShapeType == fixture.world().id<Line>()){
                    const Line& l = fixture.get<FixtureShape, Line>();
                    drawing->AddLine(
                        canvas.canvasToScreen(l.start),
                        canvas.canvasToScreen(l.end),
                        fixtureColor, 5.0);
                }
                else if(currentShapeType == fixture.world().id<Circle>()){
                    const Circle& c = fixture.get<FixtureShape, Circle>();
                    drawing->AddCircle(
                        canvas.canvasToScreen(c.center),
                        canvas.canvasSizeToScreenSize(c.radius),
                        fixtureColor,
                        layout.pixelCount,
                        5.0);
                }
            });
 
            patchIteratePixels(selectedPatch, [&](flecs::entity pixel){
                if(const Position* p = pixel.try_get<Position>()){
                    drawing->AddCircleFilled(canvas.canvasToScreen(p->position), 2.0f, 0xFF000000);
                }
            });

            //double click to add fixtures
            glm::vec2 canvasClickPos;
            if (canvas.isDoubleClicked(canvasClickPos)) {
                createLineFixture(selectedPatch, canvasClickPos, canvasClickPos + glm::vec2(100, 100));
            }
            if (canvas.isDoubleClicked(canvasClickPos, ImGuiMouseButton_Right)) {
                createCircleFixture(selectedPatch, canvasClickPos, 100);
            }

            //drag handles to move and deform fixtures
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0, 0.0, 0.0, 1.0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0, 1.0, 1.0, 1.0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5, 0.5, 0.5, 1.0));
            w.defer_begin();

            patchIterateFixtures(selectedPatch, [](flecs::entity fixture, const FixtureLayout& layout){
                flecs::entity currentShapeType = fixture.target<FixtureShape>();
                ImGui::PushID(fixture.id());
                bool edited = false;
                if(currentShapeType == fixture.world().id<Line>()){
                    Line& l = fixture.get_mut<FixtureShape, Line>();
                    edited |= canvas.dragHandle("##Start", l.start, 5.0);
                    edited |= canvas.dragHandle("##End", l.end, 5.0);
                    if(edited) fixture.modified<FixtureShape, Line>();
                }
                else if(currentShapeType == fixture.world().id<Circle>()){
                    Circle& c = fixture.get_mut<FixtureShape, Circle>();
                    edited |= canvas.dragHandle("##Center", c.center, 5.0);
                    glm::vec2 radiusHandle = c.center + glm::vec2(c.radius, 0.0);
                    if(canvas.dragHandle("##Radius", radiusHandle, 5.0)){
                        c.radius = glm::distance(c.center, radiusHandle);
                        edited = true;
                    }
                    if(edited) fixture.modified<FixtureShape, Circle>();
                }
                ImGui::PopID();
            });
            w.defer_end();
            ImGui::PopStyleColor(3);


            canvas.end();
        }
    }
    ImGui::End();



    if(ImGui::Begin("Dmx Map")){

        if(ImGui::BeginListBox("##UniverseList", ImVec2(ImGui::CalcTextSize("Universe 999").x, ImGui::GetContentRegionAvail().y))){
            auto patch = getPatch(w);
            auto selectedUniverse = getSelectedUniverse(patch);
            patchIterateDmxUniverses(patch, [&](flecs::entity universe, const DmxUniverse& u){
                bool b_selected = universe == selectedUniverse;
                if(ImGui::Selectable(universe.name(), b_selected)){
                    selectUniverse(patch, universe);
                }
            });
            ImGui::EndListBox();
        }

    }
    ImGui::End();
}

};//namespace PixelMapper