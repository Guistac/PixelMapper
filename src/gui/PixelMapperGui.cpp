#include <imgui.h>
#include "PixelMapper.h"

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
    if (ImGui::Begin("Patch Editor")) {
        if (ImDrawList* drawing = canvas.begin("Canvas", ImGui::GetContentRegionAvail())) {
            
            //draw background
            drawing->AddRectFilled(canvas.frameMin, canvas.frameMax, 0xFF333333);
            canvas.drawGrid(100.0);

            //double click to add fixtures
            glm::vec2 canvasClickPos;
            if (canvas.isDoubleClicked(canvasClickPos)) {
                CreateFixture(canvasClickPos, canvasClickPos + glm::vec2(100, 100), 10);
            }

            //draw fixtures
            world.query<const FixtureLine>().each([&](const FixtureLine& f) {
                drawing->AddLine(
                    canvas.canvasToScreen(f.start),
                    canvas.canvasToScreen(f.end),
                    0xFF0000FF, 5.0);
            });

            //draw pixels
            auto pixelQuery = world.query_builder<const Position>()
                .with<IsPixel>()
                .build();
            pixelQuery.each([&](const Position& p){
                drawing->AddCircleFilled(canvas.canvasToScreen(p.value), 2.0f, 0xFF000000);
            });

            canvas.end();
        }
    }
    ImGui::End();
}

};