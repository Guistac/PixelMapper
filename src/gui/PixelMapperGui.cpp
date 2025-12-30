#include <imgui.h>
#include "PixelMapper.h"

#include <iostream>

class ImGuiCanvas{
public:

    //screen space
    glm::vec2 offset = glm::vec2(0.0, 0.0);
    glm::vec2 frameMin, frameMax, frameSize;

    float scaling = 1.0;

    //canvas space
    glm::vec2 canvasMin, canvasMax, canvasSize;

    ImDrawList* drawing;

    glm::vec2 canvasToScreen(glm::vec2 in){
        return in * scaling - offset + frameMin;
    }

    glm::vec2 screenToCanvas(glm::vec2 in){
        return (in - frameMin + offset) / scaling;
    }

    glm::vec2 screenSizeToCanvasSize(glm::vec2 in){
        return in / scaling;
    }

    glm::vec2 canvasSizeToScreenSize(glm::vec2 in){
        return in * scaling;
    }

    ImDrawList* begin(const char* id, ImVec2 size){
        if(size.x <= 0.0 || size.y <= 0.0) return nullptr;

        //Main Interaction Widget
        ImGui::InvisibleButton(id, size);
        frameMin = ImGui::GetItemRectMin();
        frameMax = ImGui::GetItemRectMax();
        frameSize = ImGui::GetItemRectSize();

        //if mouse is held down on canvas, allow dragging to move offset
        if(ImGui::IsItemActive()){
            glm::vec2 drag = ImGui::GetMouseDragDelta();
            offset -= drag;
            ImGui::ResetMouseDragDelta();
        }

        //if hovered, allow zoom by vertical scrolling
        if(ImGui::IsItemHovered()){
            float scrollY = ImGui::GetIO().MouseWheel * 0.01 + 1.0;
            glm::vec2 mouseCanvasA = screenToCanvas(ImGui::GetMousePos());
            scaling *= scrollY;
            glm::vec2 mouseCanvasB = screenToCanvas(ImGui::GetMousePos());
            offset += (mouseCanvasA - mouseCanvasB) * scaling;
        }

        //update canvas bounds
        canvasMin = screenToCanvas(frameMin);
        canvasMax = screenToCanvas(frameMax);
        canvasSize = frameSize / scaling;

        ImGui::PushClipRect(frameMin, frameMax, true);

        drawing = ImGui::GetWindowDrawList();
        return drawing;
    }

    void end(){
        ImGui::PopClipRect();
    }

    void drawGrid(float lineSpacing){
        uint32_t markerColor = 0xFF000000;
        float markerWidth = 1.0;
        for(float xMarker = std::floor(canvasMin.x / lineSpacing) * lineSpacing;
            xMarker < canvasMax.x;
            xMarker += lineSpacing){
                glm::vec2 markerMin = canvasToScreen(glm::vec2(xMarker, canvasMin.y));
                glm::vec2 markerMax = canvasToScreen(glm::vec2(xMarker, canvasMax.y));
                drawing->AddLine(markerMin, markerMax, markerColor, markerWidth);
        }
        for(float yMarker = std::floor(canvasMin.y / lineSpacing) * lineSpacing;
            yMarker < canvasMax.y;
            yMarker += lineSpacing){
                glm::vec2 markerMin = canvasToScreen(glm::vec2(canvasMin.x, yMarker));
                glm::vec2 markerMax = canvasToScreen(glm::vec2(canvasMax.x, yMarker));
                drawing->AddLine(markerMin, markerMax, markerColor, markerWidth);
        }
    }

    bool isDoubleClicked(glm::vec2& canvasClickPos){
        if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)){
            canvasClickPos = screenToCanvas(ImGui::GetMousePos());
            return true;
        }
        return false;
    }
};


namespace PixelMapper{


ImGuiCanvas canvas;

void gui() {

    if(ImGui::Begin("Fixture List")){
        if(ImGui::BeginListBox("##Fixtures", ImGui::GetContentRegionAvail())){
                auto selectedPatch = getPatch();
                auto selectedFixture = getSelectedFixture(selectedPatch);
                world.query_builder<Fixture>()
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

        ImGui::End();
    }

    if(ImGui::Begin("Fixture Properties")){
       
        auto selectedPatch = getPatch();
        auto selectedFixture = getSelectedFixture(selectedPatch);

        if(selectedFixture.is_valid()){

            if(selectedFixture.has<Fixture>()){                
                Fixture f = selectedFixture.get<Fixture>();
                bool edited = false;
                ImGui::SeparatorText("Fixture");
                edited |= ImGui::InputInt("Pixel Count", &f.pixelCount);
                edited |= ImGui::InputInt("Channels Per Pixel", &f.channelsPerPixel);
                if(edited){
                    selectedFixture.set<Fixture>(f);
                }
            }
            if(selectedFixture.has<Line>()){
                Line l = selectedFixture.get<Line>();
                bool edited = false;
                ImGui::SeparatorText("Line Segment");
                edited |= ImGui::InputFloat2("Start Position", &l.start.x, "%.1fmm");
                edited |= ImGui::InputFloat2("End Position", &l.end.x, "%.1fmm");
                if(edited){
                    selectedFixture.set<Line>(l);
                }
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
        }

        ImGui::End();
    }

    

    if (ImGui::Begin("Patch Editor", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {

        ImGui::SetNextItemAllowOverlap();
        if (ImDrawList* drawing = canvas.begin("Canvas", ImGui::GetContentRegionAvail())) {
            
            //draw background
            drawing->AddRectFilled(canvas.frameMin, canvas.frameMax, 0xFF333333);
            canvas.drawGrid(100.0);

            auto selectedPatch = getPatch();
            auto selectedFixture = getSelectedFixture(selectedPatch);

            //double click to add fixtures
            glm::vec2 canvasClickPos;
            if (canvas.isDoubleClicked(canvasClickPos)) {
                createFixture(selectedPatch, canvasClickPos, canvasClickPos + glm::vec2(100, 100));
            }

            //draw all fixtures
            world.query_builder<const Fixture, const Line>()
                .with(flecs::ChildOf, selectedPatch)
                .build()
                .each([&](flecs::entity e, const Fixture& f, const Line& l) {
                    uint32_t fixtureColor = 0xFF0000FF;
                    if(e == selectedFixture) fixtureColor = 0xFF00FFFF;
                    drawing->AddLine(
                        canvas.canvasToScreen(l.start),
                        canvas.canvasToScreen(l.end),
                        fixtureColor, 5.0);
                });

            //draw all pixels
            world.query_builder<const Pixel>()
                // Find Pixel where any ancestor (up the ChildOf relationship) is selectedPatch
                .with(flecs::ChildOf, selectedPatch).up(flecs::ChildOf) 
                .build()
                .each([&](const Pixel& p) {
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
            world.query_builder<const Fixture>()
                .with(flecs::ChildOf, selectedPatch)
                .build()
                .each([&](flecs::entity e, const Fixture& f){
                    if(e.has<Line>()){
                        Line l = e.get<Line>();
                        bool edited = false;
                        ImGui::PushID(e.id());
                        edited |= dragHandle("##Start", l.start);
                        edited |= dragHandle("##End", l.end);
                        ImGui::PopID();
                        if(edited){
                            e.set<Line>(l);
                        }
                    }
                });
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            canvas.end();
 
        }
    }
    ImGui::End();
}

};