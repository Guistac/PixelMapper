#include "PixelMapper.h"

#include "ImGuiCanvas.h"
#include <iostream>

namespace PixelMapper{


ImGuiCanvas canvas;

void gui(flecs::world& w) {

    if(ImGui::Begin("Fixture List")){
        if(ImGui::BeginListBox("##Fixtures", ImGui::GetContentRegionAvail())){
            auto selectedPatch = getPatch(w);
            auto fixtureList = selectedPatch.target<FixtureList>();
            auto selectedFixture = getSelectedFixture(selectedPatch);
            w.query_builder<FixtureLayout>()
                .with(flecs::ChildOf, fixtureList)
                .build()
                .each([&](flecs::entity e, FixtureLayout& f){
                    bool b_selected = e == selectedFixture;
                    if(ImGui::Selectable(e.name().c_str(), b_selected)){
                        selectFixture(selectedPatch, e);
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
            auto fixtureQuery = w.query_builder<const FixtureLayout>().build();
            auto pixelQuery = w.query_builder<const Position, const ColorRGBWA>() // Find Pixel where any ancestor (up the ChildOf relationship) is selectedPatch
                .with(flecs::ChildOf, selectedPatch).up(flecs::ChildOf).build();

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
            fixtureQuery.each([drawing, selectedFixture](flecs::entity e, const FixtureLayout& f){
                flecs::entity currentShapeType = e.target<FixtureShape>();

                uint32_t fixtureColor = 0xFF0000FF;
                if(e == selectedFixture) fixtureColor = 0xFF00FFFF;

                if(currentShapeType == e.world().id<Line>()){
                    const Line& l = e.get<FixtureShape, Line>();
                    drawing->AddLine(
                        canvas.canvasToScreen(l.start),
                        canvas.canvasToScreen(l.end),
                        fixtureColor, 5.0);
                }
                else if(currentShapeType == e.world().id<Circle>()){
                    const Circle& c = e.get<FixtureShape, Circle>();
                    drawing->AddCircle(
                        canvas.canvasToScreen(c.center),
                        canvas.canvasSizeToScreenSize(c.radius),
                        fixtureColor,
                        f.pixelCount,
                        5.0);
                }
            });

 
            //draw all pixels
            pixelQuery.each([&](const Position& p, const ColorRGBWA& c) {
                drawing->AddCircleFilled(canvas.canvasToScreen(p.position), 2.0f, 0xFF000000);
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
            fixtureQuery.each([&](flecs::entity e, const FixtureLayout& f){
                flecs::entity currentShapeType = e.target<FixtureShape>();
                ImGui::PushID(e.id());
                bool edited = false;
                if(currentShapeType == e.world().id<Line>()){
                    Line l = e.get<FixtureShape, Line>();
                    edited |= canvas.dragHandle("##Start", l.start, 5.0);
                    edited |= canvas.dragHandle("##End", l.end, 5.0);
                    if(edited) e.set<FixtureShape, Line>(l);
                }
                else if(currentShapeType == e.world().id<Circle>()){
                    Circle c = e.get<FixtureShape, Circle>();
                    edited |= canvas.dragHandle("##Center", c.center, 5.0);
                    glm::vec2 radiusHandle = c.center + glm::vec2(c.radius, 0.0);
                    if(canvas.dragHandle("##Radius", radiusHandle, 5.0)){
                        c.radius = glm::distance(c.center, radiusHandle);
                        edited = true;
                    }
                    if(edited) e.set<FixtureShape, Circle>(c);
                }
                ImGui::PopID();
            });
            ImGui::PopStyleColor(3);


            canvas.end();
        }
    }
    ImGui::End();
}

};//namespace PixelMapper