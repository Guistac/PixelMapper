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
                w.query_builder<Fixture>()
                    .with(flecs::ChildOf, selectedPatch)
                    .build()
                    .each([&](flecs::entity e, Fixture& f){
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

            if(selectedFixture.has<Fixture>()){                
                Fixture f = selectedFixture.get<Fixture>();
                bool edited = false;
                ImGui::SeparatorText("Fixture");
                edited |= ImGui::InputInt("Pixel Count", &f.pixelCount);
                if(edited) selectedFixture.set<Fixture>(f);
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
            
            //draw background
            drawing->AddRectFilled(canvas.frameMin, canvas.frameMax, 0xFF333333);
            canvas.drawGrid(100.0);

            auto selectedPatch = getPatch(w);
            auto selectedFixture = getSelectedFixture(selectedPatch);

            //double click to add fixtures
            glm::vec2 canvasClickPos;
            if (canvas.isDoubleClicked(canvasClickPos)) {
                createLineFixture(selectedPatch, canvasClickPos, canvasClickPos + glm::vec2(100, 100));
            }
            if (canvas.isDoubleClicked(canvasClickPos, ImGuiMouseButton_Right)) {
                createCircleFixture(selectedPatch, canvasClickPos, 100);
            }

            if(auto renderArea = selectedPatch.try_get<RenderArea>()){
                drawing->AddRectFilled(
                    canvas.canvasToScreen(renderArea->min),
                    canvas.canvasToScreen(renderArea->max),
                    0x66000000);
            }

            auto fixtureQuery = w.query_builder<const Fixture>().build();
            auto pixelQuery = w.query_builder<const Pixel>() // Find Pixel where any ancestor (up the ChildOf relationship) is selectedPatch
                .with(flecs::ChildOf, selectedPatch).up(flecs::ChildOf).build();

            //draw all fixtures
            fixtureQuery.each([drawing, selectedFixture](flecs::entity e, const Fixture& f){
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
            pixelQuery.each([&](const Pixel& p) {
                drawing->AddCircleFilled(canvas.canvasToScreen(p.position), 2.0f, 0xFF000000);
            });


            //imgui utility for a button to move a vector by dragging
            glm::vec2 windowPos = ImGui::GetWindowPos();
            float grabSize = ImGui::GetTextLineHeight() * 0.5;
            auto dragHandle = [&](const char* id, glm::vec2& point)->bool{
                glm::vec2 cursorPos = canvas.canvasToScreen(point) - windowPos - glm::vec2(grabSize*0.5);
                ImGui::SetCursorPos(cursorPos);
                ImGui::Button(id, glm::vec2(grabSize));
                if(ImGui::IsItemActive()){
                    ImVec2 dragDelta = ImGui::GetMouseDragDelta();
                    point += canvas.screenSizeToCanvasSize(dragDelta);
                    if(dragDelta.x != 0.0 || dragDelta.y != 0.0) {
                        ImGui::ResetMouseDragDelta();
                        return true;
                    }
                }
                return false;
            };


            //Drag Handles for each Line Fixture Start and End
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0, 0.0, 0.0, 1.0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0, 1.0, 1.0, 1.0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5, 0.5, 0.5, 1.0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, grabSize * 0.5);

            fixtureQuery.each([&](flecs::entity e, const Fixture& f){
                flecs::entity currentShapeType = e.target<FixtureShape>();
                ImGui::PushID(e.id());
                bool edited = false;
                if(currentShapeType == e.world().id<Line>()){
                    Line l = e.get<FixtureShape, Line>();
                    edited |= dragHandle("##Start", l.start);
                    edited |= dragHandle("##End", l.end);
                    if(edited) e.set<FixtureShape, Line>(l);
                }
                else if(currentShapeType == e.world().id<Circle>()){
                    Circle c = e.get<FixtureShape, Circle>();
                    edited |= dragHandle("##Center", c.center);
                    glm::vec2 radiusHandle = c.center + glm::vec2(c.radius, 0.0);
                    if(dragHandle("##Radius", radiusHandle)){
                        c.radius = glm::distance(c.center, radiusHandle);
                        edited = true;
                    }
                    if(edited) e.set<FixtureShape, Circle>(c);
                }
                ImGui::PopID();
            });

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();


            canvas.end();
        }
    }
    ImGui::End();
}

};//namespace PixelMapper