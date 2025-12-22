#include "PixelMapper.h"

#include <imgui.h>


class ImGuiCanvas{
public:

    //screenspace
    glm::vec2 offset = glm::vec2(0.0, 0.0);
    float scaling = 1.0;
    //screenspace
    glm::vec2 frameMin, frameMax, frameSize;

    glm::vec2 canvasToScreen(glm::vec2 in){
        return in * scaling - offset + frameMin;
    }

    glm::vec2 screenToCanvas(glm::vec2 in){
        return (in - frameMin + offset) / scaling;
    }

    ImDrawList* draw(const char* id, ImVec2 size){
        ImGui::InvisibleButton("Canvas", ImGui::GetContentRegionAvail());
        frameMin = ImGui::GetItemRectMin();
        frameMax = ImGui::GetItemRectMax();
        frameSize = ImGui::GetItemRectSize();


        if(ImGui::IsItemActive()){
            glm::vec2 drag = ImGui::GetMouseDragDelta();
            offset -= drag;
            ImGui::ResetMouseDragDelta();
        }
        if(ImGui::IsItemHovered()){
            float scrollY = ImGui::GetIO().MouseWheel * 0.01 + 1.0;
            glm::vec2 mouseCanvasA = screenToCanvas(ImGui::GetMousePos());
            scaling *= scrollY;
            glm::vec2 mouseCanvasB = screenToCanvas(ImGui::GetMousePos());
            offset += (mouseCanvasA - mouseCanvasB) * scaling;
        }

        return ImGui::GetWindowDrawList();
    }
};



namespace PixelMapper{

    
ImGuiCanvas canvas;


void gui(){
    if(ImGui::Begin("PixelMapper")){

        ImDrawList* drawing = canvas.draw("Canvas", ImGui::GetContentRegionAvail());

        drawing->AddRectFilled(canvas.frameMin, canvas.frameMax, 0xFFFFFFFF);

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

    }
    ImGui::End();
}

};