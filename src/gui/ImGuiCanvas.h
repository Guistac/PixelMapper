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
    bool isPanning = false;

    glm::vec2 canvasToScreen(glm::vec2 in){ return in * scaling - offset + frameMin; }
    glm::vec2 screenToCanvas(glm::vec2 in){ return (in - frameMin + offset) / scaling; }
    glm::vec3 screenSizeToCanvasSize(glm::vec3 in){ return in / scaling; }
    glm::vec3 canvasSizeToScreenSize(glm::vec3 in){ return in * scaling; }
    float screenSizeToCanvasSize(float in){ return in / scaling; }
    float canvasSizeToScreenSize(float in){ return in * scaling; }

    ImDrawList* begin(const char* id, ImVec2 size){
        if(size.x <= 0.0 || size.y <= 0.0) return nullptr;

        // Position of the canvas in screen space
        ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
        frameMin = glm::vec2(cursorScreenPos.x, cursorScreenPos.y);
        frameMax = frameMin + glm::vec2(size.x, size.y);
        frameSize = glm::vec2(size.x, size.y);

        // Reserve space in layout without capturing mouse events
        ImGui::Dummy(size);

        bool isHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(frameMin.x, frameMin.y), ImVec2(frameMax.x, frameMax.y));

        //if mouse is held down on canvas, allow dragging to move offset if panning is active
        if(ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            if(ImGui::IsMouseClicked(ImGuiMouseButton_Left) && isHovered){
                isPanning = ImGui::IsKeyDown(ImGuiKey_Space);
            }
            if(isPanning){
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                glm::vec2 drag{delta.x, delta.y};
                offset -= drag;
            }
        } else {
            isPanning = false;
        }

        // Set cursor to Hand if Space is held while hovered (or during panning drag)
        if(isHovered && ImGui::IsKeyDown(ImGuiKey_Space)){
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        //if hovered, allow zoom by vertical scrolling
        if(isHovered){
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
        bool isHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(frameMin.x, frameMin.y), ImVec2(frameMax.x, frameMax.y));
        if(isHovered && ImGui::IsMouseDoubleClicked(button) && !ImGui::IsAnyItemHovered()){
            canvasClickPos = screenToCanvas(ImGui::GetMousePos());
            return true;
        }
        return false;
    }

    bool dragHandle(const char* id, glm::vec3& point, float handleSize, bool* outIsActive = nullptr){
        glm::vec2 screenPos = canvasToScreen(point) - glm::vec2(handleSize*0.5);
        ImGui::SetCursorScreenPos(screenPos);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, handleSize * 0.5f);
        ImGui::Button(id, glm::vec2(handleSize));
        ImGui::PopStyleVar();
        if(ImGui::IsItemActive()){
            if(outIsActive) *outIsActive = true;
            ImVec2 delta = ImGui::GetMouseDragDelta();
            glm::vec3 dragDelta{delta.x, delta.y, 0.0};
            point += screenSizeToCanvasSize(dragDelta);
            if(dragDelta.x != 0.0 || dragDelta.y != 0.0) {
                ImGui::ResetMouseDragDelta();
                return true;
            }
        }
        return false;
    }

    // ── Additional helpers ──────────────────────────────────────────

    /// Returns true if the canvas widget is currently hovered.
    bool isHovered() const { return ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(frameMin.x, frameMin.y), ImVec2(frameMax.x, frameMax.y)); }

    /// Returns the current mouse position in canvas space.
    glm::vec2 getMouseCanvasPos() { return screenToCanvas(ImGui::GetMousePos()); }

    /// Overload: convert a vec3 canvas pos to screen (ignores Z).
    glm::vec2 canvasToScreen(glm::vec3 in){ return canvasToScreen(glm::vec2(in.x, in.y)); }

    /// Pan + scale the canvas so that the bounding box [bMin..bMax] fits in the view.
    void zoomToFit(glm::vec2 bMin, glm::vec2 bMax, glm::vec2 margin = glm::vec2(20,20)){
        glm::vec2 bSize = bMax - bMin;
        if (bSize.x <= 0 || bSize.y <= 0) return;
        float scaleX = (frameSize.x - margin.x * 2.0f) / bSize.x;
        float scaleY = (frameSize.y - margin.y * 2.0f) / bSize.y;
        scaling = std::min(scaleX, scaleY);
        if (scaling <= 0) scaling = 1.0f;
        // Center the bounding box
        glm::vec2 center = (bMin + bMax) * 0.5f;
        offset = center * scaling - frameSize * 0.5f;
    }

}; // class ImGuiCanvas