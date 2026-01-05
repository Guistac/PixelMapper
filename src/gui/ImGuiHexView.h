#include <imgui.h>


struct MappedField {
    std::string Name;
    int Offset;
    int Count;
    ImU32 Color;
};


bool DrawHexViewer(const uint8_t* data, size_t dataSize, const std::vector<MappedField>& fields, const MappedField** clickedField) {
    static ImGuiTableFlags flags =  ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX;
    static std::string hoveredGroup = "";
    bool b_anyGroupHovered = false;
    bool blink = std::sin(ImGui::GetTime() * 10.0f) > 0.0;
    bool ret = false;
    
    // Calculate size to make buttons perfectly square/uniform
    ImVec2 cellSize = glm::vec2(ImGui::CalcTextSize("00")) + glm::vec2(ImGui::GetStyle().FramePadding) * 2.0f;

    // Optimization: Build map once per frame
    std::vector<const MappedField*> collisionMap[512];
    for (const auto& f : fields) {
        for (int i = 0; i < f.Count; i++) {
            int idx = f.Offset + i;
            if (idx >= 0 && idx < 512) collisionMap[idx].push_back(&f);
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f); // Remove button borders for flush look
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0)); // Make button transparent so table background shows through
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1,1,1,0.2f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1,1,1,0.1f));


    if (ImGui::BeginTable("HexEditor", 17, flags)) {
        for (int row = 0; row < 32; row++) {
            ImGui::TableNextRow();
            
            // Address Column
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding(); // Align "Addr" text with buttons
            ImGui::TextDisabled(" %04X ", row * 16);

            for (int col = 0; col < 16; col++) {
                int byteIdx = row * 16 + col;
                ImGui::TableSetColumnIndex(col + 1);

                if (byteIdx < (int)dataSize) {
                    const auto& owners = collisionMap[byteIdx];

                    // 1. Color Logic
                    if (owners.size() > 1) {
                        ImU32 conflictCol = IM_COL32(blink ? 180 : 100, 0, 0, 255);
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, conflictCol);
                    } else if (!owners.empty()) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, owners[0]->Color);
                    }

                    bool b_groupHovered = false;
                    if(!owners.empty()){
                        if(owners.back()->Name == hoveredGroup) {
                            b_groupHovered = true;
                            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
                        }
                    }

                    // 2. Button Logic
                    ImGui::PushID(byteIdx);
                    char label[4];
                    snprintf(label, 4, "%02X", data[byteIdx]);
                    if (ImGui::Button(label, cellSize)) {
                        if (!owners.empty()) {
                            *clickedField = owners.back();
                        }
                        ret = true;
                    }
                    ImGui::PopID();

                    if(b_groupHovered) ImGui::PopStyleColor();

                    // 3. Tooltip
                    if(ImGui::IsItemHovered()){
                        b_anyGroupHovered = true;
                        if (!owners.empty()) {
                            hoveredGroup = owners.back()->Name;
                            ImGui::BeginTooltip();
                            if (owners.size() > 1) {
                                ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "Overlap Detected (%d fields)", (int)owners.size());
                                ImGui::Separator();
                            }
                            for (auto* f : owners) {
                                ImGui::BulletText("%s (%d to %d)", f->Name.c_str(), f->Offset, f->Offset + f->Count - 1);
                            }
                            ImGui::EndTooltip();
                        }
                        else hoveredGroup = "";
                    }
                }
            }
        }
        ImGui::EndTable();
    }

    if(!b_anyGroupHovered) hoveredGroup = "";
    
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(2);
    return ret;
}