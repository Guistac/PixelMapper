#pragma once

#include <imgui.h>
#include <glm/glm.hpp>

class ImGuiCanvas{
public:

    //screen space
    glm::vec2 offset = glm::vec2(0.0, 0.0);
    glm::vec2 frameMin, frameMax, frameSize;

    float scaling = 1.0;

    //canvas space
    glm::vec2 canvasMin, canvasMax, canvasSize;

    ImDrawList* drawing;

    glm::vec2 canvasToScreen(glm::vec2 in){ return in * scaling - offset + frameMin; }
    glm::vec2 screenToCanvas(glm::vec2 in){ return (in - frameMin + offset) / scaling; }
    glm::vec2 screenSizeToCanvasSize(glm::vec2 in){ return in / scaling; }
    glm::vec2 canvasSizeToScreenSize(glm::vec2 in){ return in * scaling; }
    float screenSizeToCanvasSize(float in){ return in / scaling; }
    float canvasSizeToScreenSize(float in){ return in * scaling; }

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

    void drawGrid(float lineSpacing, uint32_t backgroundColor, uint32_t lineColor){
        float markerWidth = 1.0;
        drawing->AddRectFilled(frameMin, frameMax, backgroundColor);
        for(float xMarker = std::floor(canvasMin.x / lineSpacing) * lineSpacing;
            xMarker < canvasMax.x;
            xMarker += lineSpacing){
                glm::vec2 markerMin = canvasToScreen(glm::vec2(xMarker, canvasMin.y));
                glm::vec2 markerMax = canvasToScreen(glm::vec2(xMarker, canvasMax.y));
                drawing->AddLine(markerMin, markerMax, lineColor, markerWidth);
        }
        for(float yMarker = std::floor(canvasMin.y / lineSpacing) * lineSpacing;
            yMarker < canvasMax.y;
            yMarker += lineSpacing){
                glm::vec2 markerMin = canvasToScreen(glm::vec2(canvasMin.x, yMarker));
                glm::vec2 markerMax = canvasToScreen(glm::vec2(canvasMax.x, yMarker));
                drawing->AddLine(markerMin, markerMax, lineColor, markerWidth);
        }
    }

    bool isDoubleClicked(glm::vec2& canvasClickPos, ImGuiMouseButton button = ImGuiMouseButton_Left){
        if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(button)){
            canvasClickPos = screenToCanvas(ImGui::GetMousePos());
            return true;
        }
        return false;
    }

    bool dragHandle(const char* id, glm::vec2& point, float handleSize){
        glm::vec2 windowPos = ImGui::GetWindowPos();
        glm::vec2 cursorPos = canvasToScreen(point) - windowPos - glm::vec2(handleSize*0.5);
        ImGui::SetCursorPos(cursorPos);
        ImGui::Button(id, glm::vec2(handleSize));
        if(ImGui::IsItemActive()){
            ImVec2 dragDelta = ImGui::GetMouseDragDelta();
            point += screenSizeToCanvasSize(dragDelta);
            if(dragDelta.x != 0.0 || dragDelta.y != 0.0) {
                ImGui::ResetMouseDragDelta();
                return true;
            }
        }
        return false;
    }

};