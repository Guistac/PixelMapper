#include "PixelMapper.h"

#include <imgui.h>


class ImGuiCanvas{
public:

    //screenspace
    glm::vec2 offset = glm::vec2(0.0, 0.0);
    float scaling = 1.0;
    //screenspace
    glm::vec2 frameMin, frameMax, frameSize;
    glm::vec2 canvasMin, canvasMax, canvasSize;

    glm::vec2 canvasToScreen(glm::vec2 in){
        return in * scaling - offset + frameMin;
    }

    glm::vec2 screenToCanvas(glm::vec2 in){
        return (in - frameMin + offset) / scaling;
    }

    ImDrawList* draw(const char* id, ImVec2 size){
        if(size.x <= 0.0 || size.y <= 0.0) return nullptr;

        //Main Interaction Widget
        ImGui::InvisibleButton("Canvas", ImGui::GetContentRegionAvail());
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

        return ImGui::GetWindowDrawList();
    }
};



namespace PixelMapper{

    
ImGuiCanvas canvas;


void gui(){
    if(ImGui::Begin("Patch Editor")){

        if(ImDrawList* drawing = canvas.draw("Canvas", ImGui::GetContentRegionAvail())){

            ImGui::PushClipRect(canvas.frameMin, canvas.frameMax, true);

            drawing->AddRectFilled(canvas.frameMin, canvas.frameMax, 0xFFFFFFFF);

            float markerStep = 1000.0;
            uint32_t markerColor = 0xFF000088;
            float markerWidth = 1.0;
            for(float xMarker = std::floor(canvas.canvasMin.x / markerStep) * markerStep;
                xMarker < canvas.canvasMax.x;
                xMarker += markerStep){
                    glm::vec2 markerMin = canvas.canvasToScreen(glm::vec2(xMarker, canvas.canvasMin.y));
                    glm::vec2 markerMax = canvas.canvasToScreen(glm::vec2(xMarker, canvas.canvasMax.y));
                    drawing->AddLine(markerMin, markerMax, markerColor, markerWidth);
            }
               for(float yMarker = std::floor(canvas.canvasMin.y / markerStep) * markerStep;
                yMarker < canvas.canvasMax.y;
                yMarker += markerStep){
                    glm::vec2 markerMin = canvas.canvasToScreen(glm::vec2(canvas.canvasMin.x, yMarker));
                    glm::vec2 markerMax = canvas.canvasToScreen(glm::vec2(canvas.canvasMax.x, yMarker));
                    drawing->AddLine(markerMin, markerMax, markerColor, markerWidth);
            }
                


            if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)){
                glm::vec2 fixturePos = canvas.screenToCanvas(ImGui::GetMousePos());
                    currentPatch.fixtures.push_back(
                        Fixture{.startPos = fixturePos, .endPos = fixturePos + glm::vec2(100.0, 100.0)}
                    );
                }

            for(auto& fixture : currentPatch.fixtures){
                drawing->AddLine(
                    canvas.canvasToScreen(fixture.startPos),
                    canvas.canvasToScreen(fixture.endPos),
                    0xFF0000FF, 5.0);
            }

            ImGui::PopClipRect();
        }
    }
    ImGui::End();
}

};