#include "PixelMapper.h"
#include "network/CommandHandlers.h"
#include <sol/sol.hpp>

#include "ImGuiCanvas.h"
#include "ImGuiHexView.h"
#include "utils/Profiling.h"
#include "TextEditor.h"
#include "Presets.h"
#include "CueList.h"
#include "EffectBank.h"
#include <glad/glad.h>
#include <fstream>
#include <sstream>
#include <memory>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace PixelMapper::Shape{
void Line_guiShapeDisplay(const void*, const ImGuiCanvas*){}
bool Line_guiShapeEdit(const void*, const ImGuiCanvas*){ return false; }
bool Line_guiProps(const void*){ return false; }
void Circle_guiShapeDisplay(const void*, const ImGuiCanvas*){}
bool Circle_guiShapeEdit(const void*, const ImGuiCanvas*){ return false; }
bool Circle_guiProps(const void*){ return false; }
};

namespace PixelMapper::Gui{

ImGuiCanvas canvas;

static std::unique_ptr<TextEditor> luaEditor;
static std::unique_ptr<TextEditor> glslEditor;
static bool editorsInitialized = false;
static flecs::id_t currentPatchId = 0;

static std::unique_ptr<TextEditor> setupScriptEditor;
static bool setupEditorInitialized = false;
static bool setupScriptHasUncompiledChanges = false;
static bool showSetupHelpWindow = false;

static bool autoCompile = true;
static bool hasUncompiledChanges = false;
 
static void DrawCompactProgress(ImDrawList* drawList, ImVec2 pos, float radius, float progress, ImU32 colorBg, ImU32 colorFg) {
    ImVec2 center = ImVec2(pos.x + radius, pos.y + radius);
    
    // Draw background circle outline
    drawList->AddCircle(center, radius, colorBg, 0, 1.0f);
    
    // Draw filled pie arc if progress > 0
    if (progress > 0.001f) {
        float startAngle = -3.1415926535f / 2.0f; // Top
        float endAngle = startAngle + progress * 3.1415926535f * 2.0f;
        
        drawList->PathClear();
        drawList->PathLineTo(center);
        drawList->PathArcTo(center, radius - 0.5f, startAngle, endAngle, 32);
        drawList->PathLineTo(center);
        drawList->PathFillConvex(colorFg);
    }
}

static glm::vec3 hsv2rgb(float h, float s, float v) {
    float r = 0.0f, g = 0.0f, b = 0.0f;
    int i = (int)floorf(h * 6.0f);
    float f = h * 6.0f - (float)i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - f * s);
    float t = v * (1.0f - (1.0f - f) * s);
    switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        case 5: r = v; g = p; b = q; break;
    }
    return glm::vec3(r, g, b);
}

static glm::vec3 rgb2hsv(glm::vec3 rgb) {
    float r = rgb.r, g = rgb.g, b = rgb.b;
    float maxVal = std::max({r, g, b});
    float minVal = std::min({r, g, b});
    float h = 0.0f, s = 0.0f, v = maxVal;
    float d = maxVal - minVal;
    s = (maxVal == 0.0f) ? 0.0f : d / maxVal;
    if (maxVal != minVal) {
        if (maxVal == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (maxVal == g) h = (b - r) / d + 2.0f;
        else if (maxVal == b) h = (r - g) / d + 4.0f;
        h /= 6.0f;
    }
    return glm::vec3(h, s, v);
}

static glm::vec3 hsl2rgb(float h, float s, float l) {
    auto hue2rgb = [](float p, float q, float t) -> float {
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
        if (t < 1.0f/2.0f) return q;
        if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
        return p;
    };
    float r = l, g = l, b = l;
    if (s != 0.0f) {
        float q = (l < 0.5f) ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        r = hue2rgb(p, q, h + 1.0f/3.0f);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1.0f/3.0f);
    }
    return glm::vec3(r, g, b);
}

static glm::vec3 rgb2hsl(glm::vec3 rgb) {
    float r = rgb.r, g = rgb.g, b = rgb.b;
    float maxVal = std::max({r, g, b});
    float minVal = std::min({r, g, b});
    float h = 0.0f, s = 0.0f, l = (maxVal + minVal) * 0.5f;
    if (maxVal != minVal) {
        float d = maxVal - minVal;
        s = (l > 0.5f) ? d / (2.0f - maxVal - minVal) : d / (maxVal + minVal);
        if (maxVal == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        else if (maxVal == g) h = (b - r) / d + 2.0f;
        else if (maxVal == b) h = (r - g) / d + 4.0f;
        h /= 6.0f;
    }
    return glm::vec3(h, s, l);
}

static void setEditingCueIndex(flecs::entity app, int idx) {
    if (auto* ui = &app.get_mut<App::UIConfig>()) {
        ui->editingCueIndex = idx;
        if (idx >= -1) {
            ui->editingBankIndex = -1;
        }
    }
    auto p = std::atomic_load(&App::currentPatchProgram);
    if (p) {
        p->editingCueIndex.store(idx);
        if (idx >= -1) {
            p->editingBankIndex.store(-1);
        }
    }
}
 
static void setEditingBankIndex(flecs::entity app, int idx) {
    if (auto* ui = &app.get_mut<App::UIConfig>()) {
        ui->editingBankIndex = idx;
        if (idx >= 0) {
            ui->editingCueIndex = -2;
        }
    }
    auto p = std::atomic_load(&App::currentPatchProgram);
    if (p) {
        p->editingBankIndex.store(idx);
        if (idx >= 0) {
            p->editingCueIndex.store(-2);
        }
    }
}

static GLuint vfbPreviewTexId = 0;
static int currentVfbWidth = 0;
static int currentVfbHeight = 0;

enum class GuiLayout {
    PatchEditing = 0,
    EffectsControl = 1
};

static void applyLayout(flecs::entity app, App::UIConfig* ui, GuiLayout layout) {
    ui->currentLayout = (int)layout;
    if (layout == GuiLayout::PatchEditing) {
        ui->showPatchEditor       = true;
        ui->showFixturesWindow    = true;
        ui->showArtnetDevices     = true;
        ui->showNetworkSettings   = true;
        ui->showArtnetData        = true;
        ui->showScriptEditor      = false;
        ui->showCuesWindow        = false;
        ui->showOfflinePreviewWindow = false;
        ui->showEffectBankWindow  = false;
        ui->showFixtureSetupScriptWindow = false;
    } else if (layout == GuiLayout::EffectsControl) {
        ui->showScriptEditor      = true;
        ui->showCuesWindow        = true;
        ui->showPatchEditor       = true;
        ui->showFixturesWindow    = false;
        ui->showArtnetDevices     = false;
        ui->showNetworkSettings   = false;
        ui->showArtnetData        = false;
        ui->showOfflinePreviewWindow = true;
        ui->showEffectBankWindow  = true;
        ui->showFixtureSetupScriptWindow = false;
    }
}

static void reorderCues(flecs::entity patch, flecs::entity dragCue, flecs::entity dropCue) {
    if (!dragCue.is_valid() || !dropCue.is_valid() || dragCue == dropCue) return;

    flecs::entity cueFolder = patch.target<CueList::CueFolder>();
    if (!cueFolder.is_valid()) return;

    struct CueSortEntry {
        flecs::entity entity;
        int order = 0;
    };
    std::vector<CueSortEntry> sortedCues;
    cueFolder.children([&](flecs::entity child) {
        if (child.has<CueList::Cue::Is>()) {
            CueSortEntry entry;
            entry.entity = child;
            if (const auto* ord = child.try_get<CueList::Cue::IndexOrder>()) entry.order = ord->value;
            sortedCues.push_back(entry);
        }
    });

    std::sort(sortedCues.begin(), sortedCues.end(), [](const CueSortEntry& a, const CueSortEntry& b) {
        return a.order < b.order;
    });

    std::vector<flecs::entity> list;
    for (const auto& entry : sortedCues) {
        list.push_back(entry.entity);
    }

    auto dragIt = std::find(list.begin(), list.end(), dragCue);
    auto dropIt = std::find(list.begin(), list.end(), dropCue);
    if (dragIt == list.end() || dropIt == list.end()) return;

    auto* session = &cueFolder.get_mut<CueList::SessionState>();
    flecs::entity activeCueEntity;
    if (session && session->activeIndex >= 0 && session->activeIndex < (int)list.size()) {
        activeCueEntity = list[session->activeIndex];
    }

    list.erase(dragIt);
    dropIt = std::find(list.begin(), list.end(), dropCue);
    list.insert(dropIt, dragCue);

    for (int i = 0; i < (int)list.size(); ++i) {
        list[i].set<CueList::Cue::IndexOrder>({i});
        if (list[i] == activeCueEntity && session) {
            session->activeIndex = i;
            auto program = std::atomic_load(&App::currentPatchProgram);
            if (program) {
                program->activeCueIndex.store(i);
            }
        }
    }

    patch.add<Patch::ProgramDirty>();
}

static flecs::entity getCueEntityByIndex(flecs::entity cueFolder, int idx) {
    if (!cueFolder.is_valid() || idx < 0) return flecs::entity::null();
    struct CueSortEntry {
        flecs::entity entity;
        int order = 0;
    };
    std::vector<CueSortEntry> sortedCues;
    cueFolder.children([&](flecs::entity child) {
        if (child.has<CueList::Cue::Is>()) {
            CueSortEntry entry;
            entry.entity = child;
            if (const auto* ord = child.try_get<CueList::Cue::IndexOrder>()) entry.order = ord->value;
            sortedCues.push_back(entry);
        }
    });
    if (idx >= (int)sortedCues.size()) return flecs::entity::null();
    std::sort(sortedCues.begin(), sortedCues.end(), [](const CueSortEntry& a, const CueSortEntry& b) {
        return a.order < b.order;
    });
    return sortedCues[idx].entity;
}

static flecs::entity getEffectEntityByIndex(flecs::entity bankFolder, int idx) {
    if (!bankFolder.is_valid() || idx < 0) return flecs::entity::null();
    std::vector<flecs::entity> fxList;
    bankFolder.children([&](flecs::entity child) {
        if (child.has<EffectBank::Effect::Is>()) {
            fxList.push_back(child);
        }
    });
    if (idx >= (int)fxList.size()) return flecs::entity::null();
    return fxList[idx];
}

// ─────────────────── hit-test helpers ───────────────────────────

static float ptSegDistSq(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    glm::vec2 ab = b - a, ap = p - a;
    float t = std::clamp(glm::dot(ap, ab) / (glm::dot(ab, ab) + 1e-6f), 0.0f, 1.0f);
    glm::vec2 d = p - (a + t * ab);
    return glm::dot(d, d);
}

static Patch::MultiSelection* getOrCreateMultiSelection(flecs::entity patch) {
    if (!patch.is_valid() || !patch.is_alive()) return nullptr;
    auto* ms = patch.try_get_mut<Patch::MultiSelection>();
    if (!ms) {
        patch.set<Patch::MultiSelection>({});
        ms = patch.try_get_mut<Patch::MultiSelection>();
    }
    return ms;
}
static void msAdd(flecs::entity patch, flecs::entity f) {
    if (auto* ms = getOrCreateMultiSelection(patch)) ms->ids.insert(f.id());
}
static void msClear(flecs::entity patch) {
    if (auto* ms = getOrCreateMultiSelection(patch)) ms->ids.clear();
}
static bool msContains(flecs::entity patch, flecs::entity f) {
    const auto* ms = patch.try_get<Patch::MultiSelection>();
    return ms && ms->ids.count(f.id()) > 0;
}
static int msCount(flecs::entity patch) {
    const auto* ms = patch.try_get<Patch::MultiSelection>();
    return ms ? (int)ms->ids.size() : 0;
}

void import(flecs::world& w){

    // ─────────────── Main Menu Bar ────────────────────────────
    w.system<>("MainWindow").kind(flecs::PreStore)
    .run([&](flecs::iter& it){
        auto app = App::get(it.world());
        auto* ui = &app.get_mut<App::UIConfig>();
        auto prog = std::atomic_load(&App::currentPatchProgram);
        if (prog) {
            bool shouldRender = ui->showFrame && !ui->canvas3dMode && ui->showPatchEditor;
            prog->showPlaybackPreview.store(shouldRender);
        }
        if(ImGui::BeginMainMenuBar()){

            if(ImGui::BeginMenu("File")){
                if(ImGui::MenuItem("Save Patch", "Cmd+S"))
                    PixelMapper::Network::sendFileIORequest("SAVE", "patches/default.xml");
                if(ImGui::MenuItem("Load Patch"))
                    PixelMapper::Network::sendFileIORequest("LOAD", "patches/default.xml");
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Edit")){
                auto sel = Patch::getSelected(app);
                Patch::iterate(app, [&](flecs::entity patch){
                    bool b = (sel == patch);
                    ImGui::PushID(patch.id());
                    if(ImGui::MenuItem(patch.name().c_str(), "", b)) Patch::select(app, patch);
                    ImGui::PopID();
                });
                ImGui::Separator();
                if(ImGui::MenuItem("New Patch")) {
                    Patch::select(app, Patch::create(app));
                }
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("Layout")){
                if(ImGui::MenuItem("Patch Editing", nullptr, ui->currentLayout == (int)GuiLayout::PatchEditing)) {
                    applyLayout(app, ui, GuiLayout::PatchEditing);
                }
                if(ImGui::MenuItem("Effects & Control", nullptr, ui->currentLayout == (int)GuiLayout::EffectsControl)) {
                    applyLayout(app, ui, GuiLayout::EffectsControl);
                }
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("View")){
                ImGui::MenuItem("Fixtures",                nullptr, &ui->showFixturesWindow);
                ImGui::MenuItem("Patch Editor",            nullptr, &ui->showPatchEditor);
                ImGui::MenuItem("Artnet Data",             nullptr, &ui->showArtnetData);
                ImGui::MenuItem("Patch & Network Settings",nullptr, &ui->showNetworkSettings);
                ImGui::MenuItem("Artnet Devices",          nullptr, &ui->showArtnetDevices);
                ImGui::MenuItem("Effect Editor",           nullptr, &ui->showScriptEditor);
                ImGui::MenuItem("Fixture Setup Script",    nullptr, &ui->showFixtureSetupScriptWindow);
                ImGui::MenuItem("Cue List",                nullptr, &ui->showCuesWindow);
                ImGui::MenuItem("Effect Preview",          nullptr, &ui->showOfflinePreviewWindow);
                ImGui::MenuItem("Effect Bank",             nullptr, &ui->showEffectBankWindow);
                ImGui::Separator();
                ImGui::MenuItem("Generative Dashboard",    nullptr, &ui->showGenerativeDashboardWindow);
                ImGui::MenuItem("Generative Telemetry",    nullptr, &ui->showGenerativeTelemetryWindow);
                ImGui::MenuItem("Generative Palettes",     nullptr, &ui->showPalettesWindow);
                ImGui::MenuItem("Generative Motives",      nullptr, &ui->showMotivesWindow);
                ImGui::EndMenu();
            }

            // ── RT stats (right-aligned) ──
            float rtFps    = App::rtFps.load();
            float rtMbps   = App::rtBitrateMbps.load();
            char statusBuf[96];
            snprintf(statusBuf, sizeof(statusBuf),
                     "RT: %.1f fps   |   ArtNet: %.2f Mbit/s", rtFps, rtMbps);
            float w2 = ImGui::CalcTextSize(statusBuf).x + 24.0f;
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - w2);
            ImGui::TextDisabled("%s", statusBuf);

            ImGui::EndMainMenuBar();
        }
        ImGui::DockSpaceOverViewport();
    });

    w.system<>("KeyboardShortcuts").kind(flecs::PreStore)
    .run([&](flecs::iter& it){
        auto app = App::get(it.world());
        if(ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_LeftSuper)){
            if(ImGui::IsKeyPressed(ImGuiKey_S, false)) PixelMapper::Network::sendFileIORequest("SAVE", "patches/default.xml");
        }
    });

    // ─────────────── WindowFixtures ───────────────
    w.system<>("WindowFixtures").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app = App::get(it.world());
        auto* ui = &app.get_mut<App::UIConfig>();
        if (!ui->showFixturesWindow) return;
        auto selectedPatch   = Patch::getSelected(app);
        auto selectedFixture = Fixture::getSelected(selectedPatch);
        bool hasSel          = selectedFixture.is_valid() && selectedFixture.is_alive();

        if(ImGui::Begin("Fixtures", &ui->showFixturesWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            // Title list + props table
            if(ImGui::BeginTable("FixTable", 2, ImGuiTableFlags_Resizable)){
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SeparatorText("Fixtures List");

                // Toolbar buttons
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
                if (ImGui::Button("+ Line")) {
                    auto f = Fixture::createLine(selectedPatch, {-200, 0, 0}, {200, 0, 0});
                    Fixture::select(selectedPatch, f); msClear(selectedPatch);
                }
                ImGui::SameLine();
                if (ImGui::Button("+ Circle")) {
                    auto f = Fixture::createCircle(selectedPatch, {0, 0, 0}, 150);
                    Fixture::select(selectedPatch, f); msClear(selectedPatch);
                }
                ImGui::SameLine();
                
                ImGui::PushStyleColor(ImGuiCol_Button,        {0.5f, 0.1f, 0.1f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.7f, 0.15f, 0.15f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.4f, 0.05f, 0.05f, 1.0f});
                bool anySelected = hasSel || msCount(selectedPatch) > 0;
                if (!anySelected) ImGui::BeginDisabled();
                if (ImGui::Button("Remove")) {
                    if(msCount(selectedPatch) > 0){
                        const auto* ms = selectedPatch.try_get<Patch::MultiSelection>();
                        if(ms) for(auto fid : ms->ids){
                            flecs::entity(selectedPatch.world(), fid).destruct();
                        }
                        msClear(selectedPatch);
                        Fixture::clearSelection(selectedPatch);
                    } else if(selectedFixture.is_valid()){
                        selectedFixture.destruct();
                        Fixture::clearSelection(selectedPatch);
                    }
                }
                if (!anySelected) ImGui::EndDisabled();
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar();

                ImGui::Separator();

                static uint64_t renamingId = 0;
                static char renameBuf[64] = "";

                if(ImGui::BeginListBox("##FixsList", ImGui::GetContentRegionAvail())){
                    Fixture::iterateWithDmx(selectedPatch,
                        [&](flecs::entity f, const Fixture::Layout&, const Fixture::DmxAddress&)
                    {
                        bool isSel = (f == selectedFixture);
                        ImGui::PushID((int)f.id());
                        if(renamingId == f.id()){
                            if(ImGui::InputText("##ren", renameBuf, sizeof(renameBuf),
                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)){
                                f.set_name(renameBuf); renamingId = 0;
                            }
                            if(!ImGui::IsItemActive() && ImGui::IsMouseClicked(0)) renamingId = 0;
                        } else {
                            if(ImGui::Selectable(f.name().c_str(), isSel)){
                                Fixture::select(selectedPatch, f); msClear(selectedPatch);
                            }
                            if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)){
                                renamingId = f.id();
                                std::strncpy(renameBuf, f.name().c_str(), sizeof(renameBuf)-1);
                            }
                        }

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            uint64_t dragFid = f.id();
                            ImGui::SetDragDropPayload("DND_FIXTURE_ORDER", &dragFid, sizeof(dragFid));
                            ImGui::Text("Move %s", f.name().c_str());
                            ImGui::EndDragDropSource();
                        }

                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_FIXTURE_ORDER")) {
                                uint64_t dragFid = *(const uint64_t*)payload->Data;
                                flecs::entity dragFixture(selectedPatch.world(), dragFid);
                                Fixture::reorder(selectedPatch, dragFixture, f);
                            }
                            ImGui::EndDragDropTarget();
                        }

                        ImGui::PopID();
                    });
                    ImGui::EndListBox();
                }
                // Properties
                ImGui::TableSetColumnIndex(1);
                if(msCount(selectedPatch) > 1){
                    ImGui::TextDisabled("%d fixtures selected.", msCount(selectedPatch));
                    ImGui::Separator();
                    if (ImGui::Button("Reset Z to 0 for All Selected")) {
                        const auto* ms = selectedPatch.try_get<Patch::MultiSelection>();
                        if(ms) {
                            for(auto fid : ms->ids) {
                                flecs::entity mf(selectedPatch.world(), fid);
                                if(!mf.is_valid() || !mf.is_alive()) continue;
                                auto mst = mf.target<Fixture::WithShape>();
                                if(mst == mf.world().id<Shape::Line>()){
                                    auto l = mf.get<Fixture::WithShape, Shape::Line>();
                                    float avgZ = (l.start.z + l.end.z) * 0.5f;
                                    l.start.z -= avgZ;
                                    l.end.z -= avgZ;
                                    mf.set<Fixture::WithShape, Shape::Line>(l);
                                    mf.add<Fixture::PixelPositionsDirty>();
                                } else if(mst == mf.world().id<Shape::Circle>()){
                                    auto c = mf.get<Fixture::WithShape, Shape::Circle>();
                                    c.center.z = 0.0f;
                                    mf.set<Fixture::WithShape, Shape::Circle>(c);
                                    mf.add<Fixture::PixelPositionsDirty>();
                                }
                            }
                        }
                    }
                    static float multiZ = 0.0f;
                    if (ImGui::SliderFloat("Z Height Shift All", &multiZ, -500.0f, 500.0f, "%.1fmm")) {
                        const auto* ms = selectedPatch.try_get<Patch::MultiSelection>();
                        if(ms) {
                            for(auto fid : ms->ids) {
                                flecs::entity mf(selectedPatch.world(), fid);
                                if(!mf.is_valid() || !mf.is_alive()) continue;
                                auto mst = mf.target<Fixture::WithShape>();
                                if(mst == mf.world().id<Shape::Line>()){
                                    auto l = mf.get<Fixture::WithShape, Shape::Line>();
                                    float avgZ = (l.start.z + l.end.z) * 0.5f;
                                    float deltaZ = multiZ - avgZ;
                                    l.start.z += deltaZ;
                                    l.end.z += deltaZ;
                                    mf.set<Fixture::WithShape, Shape::Line>(l);
                                    mf.add<Fixture::PixelPositionsDirty>();
                                } else if(mst == mf.world().id<Shape::Circle>()){
                                    auto c = mf.get<Fixture::WithShape, Shape::Circle>();
                                    c.center.z = multiZ;
                                    mf.set<Fixture::WithShape, Shape::Circle>(c);
                                    mf.add<Fixture::PixelPositionsDirty>();
                                }
                            }
                        }
                    }
                } else if(hasSel){
                    ImGui::Text("Properties: %s", selectedFixture.name().c_str());
                    ImGui::Separator();

                    char nameBuf[128] = {};
                    std::strncpy(nameBuf, selectedFixture.name().c_str(), sizeof(nameBuf) - 1);
                    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                        selectedFixture.set_name(nameBuf);
                    }

                    if(selectedFixture.has<Fixture::Layout>()){
                        Fixture::Layout f = selectedFixture.get<Fixture::Layout>();
                        bool e = false;
                        ImGui::SeparatorText("Layout");
                        e |= ImGui::InputInt("Pixel Count", &f.pixelCount);
                        e |= ImGui::InputInt("Color Channels", &f.channelsPerPixel);
                        ImGui::Text("%i Bytes", f.pixelCount * f.channelsPerPixel);
                        if(e) selectedFixture.set<Fixture::Layout>(f);
                    }
                    if(selectedFixture.has<Fixture::DmxAddress>()){
                        Fixture::DmxAddress dmx = selectedFixture.get<Fixture::DmxAddress>();
                        if (selectedFixture.get<Fixture::Layout>().pixelCount == 0) {
                            ImGui::SeparatorText("Dmx Address");
                            ImGui::TextDisabled("N/A (Visual Indicator)");
                        } else {
                            bool e = false;
                            ImGui::SeparatorText("Dmx Address");
                            uint16_t s1 = 1, s10 = 10;
                            e |= ImGui::InputScalar("Universe", ImGuiDataType_U16, &dmx.universe, &s1, &s10);
                            e |= ImGui::InputScalar("Address",  ImGuiDataType_U16, &dmx.address,  &s1, &s10);
                            if(e) selectedFixture.set<Fixture::DmxAddress>(dmx);
                        }
                    }
                    auto shapeType = selectedFixture.target<Fixture::WithShape>();
                    if(shapeType == selectedFixture.world().id<Shape::Line>()){
                        Shape::Line l = selectedFixture.get<Fixture::WithShape, Shape::Line>();
                        bool e = false;
                        ImGui::SeparatorText("Line Segment");
                        e |= ImGui::InputFloat3("Start", &l.start.x, "%.1fmm");
                        e |= ImGui::InputFloat3("End",   &l.end.x,   "%.1fmm");
                        
                        float avgZ = (l.start.z + l.end.z) * 0.5f;
                        float curAvgZ = avgZ;
                        if (ImGui::SliderFloat("Z Height", &avgZ, -500.0f, 500.0f, "%.1fmm")) {
                            float deltaZ = avgZ - curAvgZ;
                            l.start.z += deltaZ;
                            l.end.z += deltaZ;
                            e = true;
                        }
                        
                        if(e){ selectedFixture.get_mut<Fixture::WithShape, Shape::Line>() = l; selectedFixture.add<Fixture::PixelPositionsDirty>(); }
                    } else if(shapeType == selectedFixture.world().id<Shape::Circle>()){
                        Shape::Circle c = selectedFixture.get<Fixture::WithShape, Shape::Circle>();
                        bool e = false;
                        ImGui::SeparatorText("Circle");
                        e |= ImGui::InputFloat3("Center", &c.center.x, "%.1fmm");
                        e |= ImGui::InputFloat("Radius",  &c.radius,   0, 0, "%.1fmm");
                        
                        e |= ImGui::SliderFloat("Z Height", &c.center.z, -500.0f, 500.0f, "%.1fmm");
                        
                        if(e){ selectedFixture.get_mut<Fixture::WithShape, Shape::Circle>() = c; selectedFixture.add<Fixture::PixelPositionsDirty>(); }
                    }
                } else {
                    ImGui::TextDisabled("Select a fixture.");
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    });

    // ─────────────── Patch Editor (Canvas) ───────────────────────
    w.system<>("WindowPatchEditor").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app             = App::get(it.world());
        auto* ui             = &app.get_mut<App::UIConfig>();
        if (!ui->showPatchEditor) return;
        auto selectedPatch   = Patch::getSelected(app);
        auto selectedFixture = Fixture::getSelected(selectedPatch);

        // 3D rotation projection helper
        auto projectPoint = [&](glm::vec3 pos) -> glm::vec2 {
            if (ui->canvas3dMode) {
                float rx = glm::radians(ui->canvasRotationX);
                float ry = glm::radians(ui->canvasRotationY);

                // Rotate Y (yaw)
                float cy = std::cos(ry);
                float sy = std::sin(ry);
                glm::vec3 rotY = glm::vec3(
                    pos.x * cy + pos.z * sy,
                    pos.y,
                    -pos.x * sy + pos.z * cy
                );

                // Rotate X (pitch)
                float cx = std::cos(rx);
                float sx = std::sin(rx);
                glm::vec3 rotXY = glm::vec3(
                    rotY.x,
                    rotY.y * cx - rotY.z * sx,
                    rotY.y * sx + rotY.z * cx
                );

                return glm::vec2(rotXY.x, rotXY.y);
            } else {
                return glm::vec2(pos.x, pos.y);
            }
        };

        auto zoomToFit3D = [&]() {
            if (!selectedPatch.is_valid()) return;
            glm::vec2 pMin(FLT_MAX);
            glm::vec2 pMax(-FLT_MAX);
            bool hasPoints = false;
            Fixture::iterateWithPixelData(selectedPatch, std::function<void(flecs::entity, const Fixture::PixelData&)>(
                [&](flecs::entity, const Fixture::PixelData& pd) {
                    for (const auto& pos : pd.positions) {
                        glm::vec2 proj = projectPoint(pos);
                        pMin = glm::min(pMin, proj);
                        pMax = glm::max(pMax, proj);
                        hasPoints = true;
                    }
                }
            ));
            if (hasPoints) {
                canvas.zoomToFit(pMin, pMax, {15, 15});
            } else if (const auto* ra = selectedPatch.try_get<Patch::RenderArea>()) {
                if (ui->canvas3dMode) {
                    glm::vec3 corners[8] = {
                        {ra->min.x, ra->min.y, ra->min.z},
                        {ra->max.x, ra->min.y, ra->min.z},
                        {ra->min.x, ra->max.y, ra->min.z},
                        {ra->max.x, ra->max.y, ra->min.z},
                        {ra->min.x, ra->min.y, ra->max.z},
                        {ra->max.x, ra->min.y, ra->max.z},
                        {ra->min.x, ra->max.y, ra->max.z},
                        {ra->max.x, ra->max.y, ra->max.z}
                    };
                    for (int i = 0; i < 8; ++i) {
                        glm::vec2 proj = projectPoint(corners[i]);
                        pMin = glm::min(pMin, proj);
                        pMax = glm::max(pMax, proj);
                    }
                    canvas.zoomToFit(pMin, pMax, {15, 15});
                } else {
                    canvas.zoomToFit(glm::vec2(ra->min), glm::vec2(ra->max), {15, 15});
                }
            }
        };

        // Marquee state
        static bool      marqueeActive = false;
        static glm::vec2 marqueeStart{0}, marqueeEnd{0};

        // Fixture dragging state
        static bool      fixtureDragging = false;
        static bool      fixtureHasDragged = false;
        static flecs::entity clickedFixture;
        static glm::vec2 lastDragMouseCanvas{0};

        if(ImGui::Begin("Patch Editor", &ui->showPatchEditor,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::Checkbox("Lock", &ui->patchLocked); ImGui::SameLine();
            ImGui::Checkbox("Grid", &ui->showGrid); ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::SliderFloat("Opacity", &ui->previewOpacity, 0.0f, 1.0f, "%.2f"); ImGui::SameLine();
            ImGui::Checkbox("Fixtures", &ui->showFixtures); ImGui::SameLine();
            ImGui::Checkbox("Pixels",   &ui->showPixels);   ImGui::SameLine();
            if (ui->showPixels) {
                ImGui::SetNextItemWidth(50);
                ImGui::InputFloat("Size", &ui->pixelSize, 0.0f, 0.0f, "%.0f");
                if (ui->pixelSize < 1.0f) ui->pixelSize = 1.0f;
                ImGui::SameLine();
            }
            ImGui::Checkbox("Rendered", &ui->showFrame);    ImGui::SameLine();
            if (selectedPatch.is_valid()) {
                if (auto* s = selectedPatch.try_get_mut<Patch::Settings>()) {
                    if (ImGui::Checkbox("Highlight", &s->highlightSelected)) {
                        selectedPatch.add<Patch::ProgramDirty>();
                    }
                    ImGui::SameLine();
                }
            }
            ImGui::Checkbox("3D Canvas", &ui->canvas3dMode); ImGui::SameLine();
            if (ui->canvas3dMode) {
                ImGui::Text("Rot: %.0f, %.0f", ui->canvasRotationX, ui->canvasRotationY); ImGui::SameLine();
                if (ImGui::Button("Reset View")) {
                    ui->canvasRotationX = 0.0f;
                    ui->canvasRotationY = 0.0f;
                }
                ImGui::SameLine();
            }
            ImGui::Checkbox("Auto Zoom", &ui->autoZoom); ImGui::SameLine();
            if(ImGui::Button("Zoom to Fit") && selectedPatch.is_valid()){
                zoomToFit3D();
            }

            if(ImDrawList* drawing = canvas.begin("Canvas", ImGui::GetContentRegionAvail())){
                if (ui->autoZoom && selectedPatch.is_valid()) {
                    zoomToFit3D();
                }
                // ── Capture canvas interaction state BEFORE any child widgets ──
                bool canvasHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(canvas.frameMin, canvas.frameMax) && !ImGui::IsAnyItemHovered();
                bool canvasClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && canvasHovered;
                bool canvasActive  = ImGui::IsMouseDown(ImGuiMouseButton_Left) && canvasHovered;
                glm::vec2 mCanvas  = canvas.getMouseCanvasPos();
                bool shiftHeld     = ImGui::GetIO().KeyShift;

                static bool isRotating = false;
                if (ui->canvas3dMode) {
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && canvasHovered && !canvas.isPanning) {
                            isRotating = true;
                        }
                        if (isRotating) {
                            ImVec2 delta = ImGui::GetIO().MouseDelta;
                            ui->canvasRotationX += delta.y * 0.5f; // pitch
                            ui->canvasRotationY += delta.x * 0.5f; // yaw
                            
                            // clamp pitch to avoid flipping
                            ui->canvasRotationX = std::clamp(ui->canvasRotationX, -89.0f, 89.0f);
                            
                            // keep yaw within [0, 360)
                            ui->canvasRotationY = fmodf(ui->canvasRotationY, 360.0f);
                            if (ui->canvasRotationY < 0.0f) ui->canvasRotationY += 360.0f;
                        }
                    } else {
                        isRotating = false;
                    }
                }

                if (ui->showGrid) {
                    canvas.drawGrid(100.0, 0xFF333333, 0xFF000000);
                } else {
                    drawing->AddRectFilled(canvas.frameMin, canvas.frameMax, 0xFF000000);
                }

                if(selectedPatch.is_valid()){

                    // ── VFB preview texture (only in 2D mode) ──
                    if(ui->showFrame && !ui->canvas3dMode){
                        auto prog = std::atomic_load(&App::currentPatchProgram);
                        if(prog){
                            int tw = prog->vfbWidth;
                            int th = prog->vfbHeight;
                            GLuint texID = 0;
                            if(prog->renderMode == Patch::RenderMode::GLSL){
                                GLuint currentTex = prog->glslCurrentPlaybackPreviewTexID.load();
                                if (currentTex != 0) {
                                    texID = currentTex;
                                } else {
                                    texID = prog->glslPlaybackPreviewFboTex;
                                }
                            } else if(prog->vfbPixels){

                                if(vfbPreviewTexId == 0) glGenTextures(1, &vfbPreviewTexId);
                                glBindTexture(GL_TEXTURE_2D, vfbPreviewTexId);
                                if(currentVfbWidth != tw || currentVfbHeight != th){
                                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0,
                                                 GL_RGBA, GL_UNSIGNED_BYTE, prog->vfbPixels);
                                    currentVfbWidth = tw; currentVfbHeight = th;
                                } else {
                                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw, th,
                                                    GL_RGBA, GL_UNSIGNED_BYTE, prog->vfbPixels);
                                }
                                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                                            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                                            glBindTexture(GL_TEXTURE_2D, 0);
                                            texID = vfbPreviewTexId;
                            }
                            if(const auto* ra = selectedPatch.try_get<Patch::RenderArea>()){
                                glm::vec2 pMin = canvas.canvasToScreen(glm::vec2(ra->min));
                                glm::vec2 pMax = canvas.canvasToScreen(glm::vec2(ra->max));
                                ImU32 tintCol = IM_COL32(255, 255, 255, (int)(ui->previewOpacity * 255));
                                if(texID) drawing->AddImage((ImTextureID)(intptr_t)texID, pMin, pMax, ImVec2(0,0), ImVec2(1,1), tintCol);
                                else      drawing->AddRectFilled(pMin, pMax, IM_COL32(0, 0, 0, (int)(ui->previewOpacity * 0.4f * 255)));
                            }
                        }
                    } else if(const auto* ra = selectedPatch.try_get<Patch::RenderArea>()){
                        if (!ui->canvas3dMode) {
                            drawing->AddRectFilled(canvas.canvasToScreen(glm::vec2(ra->min)),
                                                   canvas.canvasToScreen(glm::vec2(ra->max)), IM_COL32(0, 0, 0, (int)(ui->previewOpacity * 0.27f * 255)));
                        }
                    }

                    // ── Draw fixtures ──
                    if(ui->showFixtures){
                        Fixture::iterateWithDmx(selectedPatch,
                            [&](flecs::entity f, const Fixture::Layout& layout, const Fixture::DmxAddress&)
                        {
                            bool isSel   = (f == selectedFixture);
                            bool isMulti = msContains(selectedPatch, f);
                            uint32_t col = isSel ? 0xFF00FFFF : (isMulti ? 0xFF80FF80 : 0xFF0000FF);
                            auto st = f.target<Fixture::WithShape>();
                            if(st == f.world().id<Shape::Line>()){
                                const Shape::Line& l = f.get<Fixture::WithShape, Shape::Line>();
                                glm::vec2 pStart = projectPoint(l.start);
                                glm::vec2 pEnd = projectPoint(l.end);
                                drawing->AddLine(canvas.canvasToScreen(pStart), canvas.canvasToScreen(pEnd), col, 5.f);
                            } else if(st == f.world().id<Shape::Circle>()){
                                const Shape::Circle& c = f.get<Fixture::WithShape, Shape::Circle>();
                                if (ui->canvas3dMode) {
                                    std::vector<ImVec2> pts;
                                    for(int i = 0; i <= layout.pixelCount; i++) {
                                        float angle = (i % layout.pixelCount) * 2.0f * 3.14159265f / layout.pixelCount;
                                        glm::vec3 pos(
                                            c.center.x + std::cos(angle) * c.radius,
                                            c.center.y + std::sin(angle) * c.radius,
                                            c.center.z
                                        );
                                        glm::vec2 proj = projectPoint(pos);
                                        glm::vec2 scr = canvas.canvasToScreen(proj);
                                        pts.push_back({scr.x, scr.y});
                                    }
                                    drawing->AddPolyline(pts.data(), pts.size(), col, 0, 5.f);
                                } else {
                                    drawing->AddCircle(canvas.canvasToScreen(c.center),
                                                       canvas.canvasSizeToScreenSize(c.radius), col, layout.pixelCount, 5.f);
                                }
                            }
                        });
                    }

                    if(ui->showPixels){
                        std::vector<ColorRGBW> tempColors;
                        bool hasColors = false;
                        auto prog = std::atomic_load(&App::currentPatchProgram);
                        if (prog && prog->pixelColors) {
                            tempColors.assign(prog->pixelColors,
                                              prog->pixelColors + prog->pixelCount);
                            hasColors = true;
                        }

                        int pixelIndex = 0;
                        Fixture::iterateWithDmx(selectedPatch,
                            [&](flecs::entity fixture, const Fixture::Layout& layout, const Fixture::DmxAddress&){
                                const auto* pd = fixture.try_get<Fixture::PixelData>();
                                if (pd) {
                                    float hsz = ui->pixelSize * 0.5f;
                                    glm::vec2 sz(hsz);
                                    for(int i = 0; i < (int)pd->positions.size(); i++){
                                        glm::vec2 projected = projectPoint(pd->positions[i]);
                                        auto p = canvas.canvasToScreen(projected);
                                        ColorRGBW c{0, 0, 0, 255};
                                        if (hasColors && (pixelIndex + i < (int)tempColors.size())) {
                                            c = tempColors[pixelIndex + i];
                                        } else if (i < (int)pd->colors.size()) {
                                            c = pd->colors[i];
                                        }
                                        uint8_t displayR = (uint8_t)std::min(255, (int)c.r + (int)c.w);
                                        uint8_t displayG = (uint8_t)std::min(255, (int)c.g + (int)c.w);
                                        uint8_t displayB = (uint8_t)std::min(255, (int)c.b + (int)c.w);
                                        drawing->AddRectFilled(p-sz, p+sz, IM_COL32(displayR, displayG, displayB, 255));
                                    }
                                }
                                pixelIndex += layout.pixelCount;
                        });
                    }

                    // ── Double-click to add fixture ──
                    if (!ui->patchLocked && !ui->canvas3dMode) {
                        glm::vec2 clickPos;
                        if(canvas.isDoubleClicked(clickPos)){
                            Fixture::createLine(selectedPatch, {clickPos, 0}, {clickPos + glm::vec2(100,100), 0});
                        }
                        if(canvas.isDoubleClicked(clickPos, ImGuiMouseButton_Right)){
                            Fixture::createCircle(selectedPatch, {clickPos, 0}, 100);
                        }
                    }

                    // ── Drag handles for selected fixture ──
                    bool handleDragged = false;
                    bool handleActive = false;
                    static glm::vec3 prevDragAnchor{0};
                    static bool wasDragging = false;

                    if(!ui->patchLocked && !ui->canvas3dMode && selectedFixture.is_valid() && ui->showFixtures && !ImGui::IsKeyDown(ImGuiKey_Space) && !fixtureDragging){
                        ImGui::PushStyleColor(ImGuiCol_Button,        {0.2f, 0.6f, 1.0f, 0.8f});
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {1.0f, 1.0f, 1.0f, 1.0f});
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.2f, 1.0f, 0.4f, 1.0f});

                        auto st = selectedFixture.target<Fixture::WithShape>();
                        bool edited = false;
                        if(st == selectedFixture.world().id<Shape::Line>()){
                            Shape::Line l = selectedFixture.get<Fixture::WithShape, Shape::Line>();
                            bool d1 = canvas.dragHandle("##S", l.start, 10.f, &handleActive);
                            bool d2 = canvas.dragHandle("##E", l.end,   10.f, &handleActive);
                            edited = d1 || d2;
                            if(edited){ selectedFixture.get_mut<Fixture::WithShape, Shape::Line>() = l; selectedFixture.add<Fixture::PixelPositionsDirty>(); }
                            if(edited && d1) prevDragAnchor = l.start;
                        } else if(st == selectedFixture.world().id<Shape::Circle>()){
                            Shape::Circle c = selectedFixture.get<Fixture::WithShape, Shape::Circle>();
                            bool dc = canvas.dragHandle("##C", c.center, 10.f, &handleActive);
                            glm::vec3 rH = c.center + glm::vec3(c.radius, 0, 0);
                            bool dr = canvas.dragHandle("##R", rH, 10.f, &handleActive);
                            if(dr) c.radius = glm::distance(c.center, rH);
                            edited = dc || dr;
                            if(edited){ selectedFixture.get_mut<Fixture::WithShape, Shape::Circle>() = c; selectedFixture.add<Fixture::PixelPositionsDirty>(); }
                            if(edited && dc) prevDragAnchor = c.center;
                        }
                        handleDragged = edited;
                        ImGui::PopStyleColor(3);

                        // ── Group drag: propagate delta to multi-selected fixtures ──
                        if(handleDragged && msCount(selectedPatch) > 0){
                            auto st2 = selectedFixture.target<Fixture::WithShape>();
                            glm::vec3 newAnchor{0};
                            if(st2 == selectedFixture.world().id<Shape::Line>())
                                newAnchor = selectedFixture.get<Fixture::WithShape, Shape::Line>().start;
                            else if(st2 == selectedFixture.world().id<Shape::Circle>())
                                newAnchor = selectedFixture.get<Fixture::WithShape, Shape::Circle>().center;

                            if(!wasDragging){ prevDragAnchor = newAnchor; wasDragging = true; }
                            glm::vec3 delta = newAnchor - prevDragAnchor;
                            prevDragAnchor  = newAnchor;

                            if(glm::length(delta) > 0.001f){
                                const auto* ms = selectedPatch.try_get<Patch::MultiSelection>();
                                if(ms) for(auto fid : ms->ids){
                                    flecs::entity mf(selectedPatch.world(), fid);
                                    if(!mf.is_valid() || !mf.is_alive() || mf == selectedFixture) continue;
                                    auto mst = mf.target<Fixture::WithShape>();
                                    if(mst == mf.world().id<Shape::Line>()){
                                        Shape::Line ml = mf.get<Fixture::WithShape, Shape::Line>();
                                        ml.start += delta; ml.end += delta;
                                        mf.get_mut<Fixture::WithShape, Shape::Line>() = ml;
                                        mf.add<Fixture::PixelPositionsDirty>();
                                    } else if(mst == mf.world().id<Shape::Circle>()){
                                        Shape::Circle mc = mf.get<Fixture::WithShape, Shape::Circle>();
                                        mc.center += delta;
                                        mf.get_mut<Fixture::WithShape, Shape::Circle>() = mc;
                                        mf.add<Fixture::PixelPositionsDirty>();
                                    }
                                }
                            }
                        } else {
                            wasDragging = false;
                        }
                    }

                    // ── Click-to-select and Drag Start ──
                    if(!ui->canvas3dMode && canvasClicked && !handleDragged && !handleActive && ui->showFixtures && !ImGui::IsKeyDown(ImGuiKey_Space)){
                        float threshSq = canvas.screenSizeToCanvasSize(8.f);
                        threshSq *= threshSq;

                        flecs::entity hitFixture;
                        float bestSq = threshSq;
                        Fixture::iterateWithDmx(selectedPatch,
                            [&](flecs::entity f, const Fixture::Layout&, const Fixture::DmxAddress&)
                        {
                            auto st = f.target<Fixture::WithShape>();
                            float dSq = FLT_MAX;
                            if(st == f.world().id<Shape::Line>()){
                                const Shape::Line& l = f.get<Fixture::WithShape, Shape::Line>();
                                dSq = ptSegDistSq(mCanvas, l.start, l.end);
                            } else if(st == f.world().id<Shape::Circle>()){
                                const Shape::Circle& c = f.get<Fixture::WithShape, Shape::Circle>();
                                float diff = std::abs(glm::length(mCanvas - glm::vec2(c.center)) - c.radius);
                                dSq = diff * diff;
                            }
                            if(dSq < bestSq){ bestSq = dSq; hitFixture = f; }
                        });

                        if(hitFixture.is_valid()){
                            clickedFixture = hitFixture;
                            if (!ui->patchLocked) {
                                fixtureDragging = true;
                                fixtureHasDragged = false;
                                lastDragMouseCanvas = mCanvas;
                            }

                            bool isAlreadySelected = (hitFixture == selectedFixture || msContains(selectedPatch, hitFixture));
                            if(!isAlreadySelected){
                                if(shiftHeld){
                                    msAdd(selectedPatch, selectedFixture);
                                    msAdd(selectedPatch, hitFixture);
                                    Fixture::select(selectedPatch, hitFixture);
                                } else {
                                    Fixture::select(selectedPatch, hitFixture);
                                    msClear(selectedPatch);
                                }
                            }
                        } else if(!shiftHeld){
                            Fixture::clearSelection(selectedPatch);
                            msClear(selectedPatch);
                            marqueeActive = true;
                            marqueeStart  = mCanvas;
                            marqueeEnd    = mCanvas;
                        }
                    }

                    // ── Handle Fixture Dragging ──
                    if(!ui->canvas3dMode && fixtureDragging){
                        if(ImGui::IsMouseDown(ImGuiMouseButton_Left)){
                            glm::vec2 delta2d = mCanvas - lastDragMouseCanvas;
                            if(glm::length(delta2d) > 0.001f){
                                fixtureHasDragged = true;
                                lastDragMouseCanvas = mCanvas;

                                auto moveFixture = [](flecs::entity f, const glm::vec2& d2d) {
                                    if(!f.is_valid() || !f.is_alive()) return;
                                    glm::vec3 d3d{d2d.x, d2d.y, 0.0f};
                                    auto st = f.target<Fixture::WithShape>();
                                    if(st == f.world().id<Shape::Line>()){
                                        Shape::Line l = f.get<Fixture::WithShape, Shape::Line>();
                                        l.start += d3d;
                                        l.end += d3d;
                                        f.get_mut<Fixture::WithShape, Shape::Line>() = l;
                                        f.add<Fixture::PixelPositionsDirty>();
                                    } else if(st == f.world().id<Shape::Circle>()){
                                        Shape::Circle c = f.get<Fixture::WithShape, Shape::Circle>();
                                        c.center += d3d;
                                        f.get_mut<Fixture::WithShape, Shape::Circle>() = c;
                                        f.add<Fixture::PixelPositionsDirty>();
                                    }
                                };

                                const auto* ms = selectedPatch.try_get<Patch::MultiSelection>();
                                if(ms && !ms->ids.empty()){
                                    for(auto fid : ms->ids){
                                        moveFixture(flecs::entity(selectedPatch.world(), fid), delta2d);
                                    }
                                } else if(selectedFixture.is_valid()){
                                    moveFixture(selectedFixture, delta2d);
                                }
                            }
                        } else {
                            if(!fixtureHasDragged && clickedFixture.is_valid()){
                                if(shiftHeld){
                                    if(auto* ms = getOrCreateMultiSelection(selectedPatch)){
                                        if(ms->ids.count(clickedFixture.id())){
                                            ms->ids.erase(clickedFixture.id());
                                            if(selectedFixture == clickedFixture){
                                                if(!ms->ids.empty()){
                                                    flecs::entity nextSel(selectedPatch.world(), *ms->ids.begin());
                                                    Fixture::select(selectedPatch, nextSel);
                                                } else {
                                                    Fixture::clearSelection(selectedPatch);
                                                }
                                            }
                                        } else {
                                            msAdd(selectedPatch, selectedFixture);
                                            msAdd(selectedPatch, clickedFixture);
                                            Fixture::select(selectedPatch, clickedFixture);
                                        }
                                    }
                                } else {
                                    Fixture::select(selectedPatch, clickedFixture);
                                    msClear(selectedPatch);
                                }
                            }
                            fixtureDragging = false;
                            clickedFixture = flecs::entity::null();
                        }
                    }

                    // ── Marquee rubber-band ──
                    if(!ui->canvas3dMode && marqueeActive){
                        if(canvasActive || ImGui::IsMouseDown(ImGuiMouseButton_Left)){
                            marqueeEnd = mCanvas;
                        }

                        float rxMin = std::min(marqueeStart.x, marqueeEnd.x);
                        float rxMax = std::max(marqueeStart.x, marqueeEnd.x);
                        float ryMin = std::min(marqueeStart.y, marqueeEnd.y);
                        float ryMax = std::max(marqueeStart.y, marqueeEnd.y);
                        glm::vec2 sA = canvas.canvasToScreen({rxMin, ryMin});
                        glm::vec2 sB = canvas.canvasToScreen({rxMax, ryMax});

                        drawing->AddRect(sA, sB, IM_COL32(100,200,255,200), 0, 0, 1.5f);
                        drawing->AddRectFilled(sA, sB, IM_COL32(100,200,255,30));

                        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)){
                            bool first = true;
                            msClear(selectedPatch);
                            Fixture::iterateWithDmx(selectedPatch,
                                [&](flecs::entity f, const Fixture::Layout&, const Fixture::DmxAddress&)
                            {
                                glm::vec2 center{0};
                                auto st = f.target<Fixture::WithShape>();
                                if(st == f.world().id<Shape::Line>()){
                                    const Shape::Line& l = f.get<Fixture::WithShape, Shape::Line>();
                                    center = (glm::vec2(l.start) + glm::vec2(l.end)) * 0.5f;
                                } else if(st == f.world().id<Shape::Circle>()){
                                    center = glm::vec2(f.get<Fixture::WithShape, Shape::Circle>().center);
                                }
                                if(center.x >= rxMin && center.x <= rxMax &&
                                   center.y >= ryMin && center.y <= ryMax)
                                {
                                    msAdd(selectedPatch, f);
                                    if(first){ Fixture::select(selectedPatch, f); first = false; }
                                }
                            });
                            marqueeActive = false;
                        }
                    }

                    if(marqueeActive && !canvasActive && !ImGui::IsMouseDown(0))
                        marqueeActive = false;
                }

                canvas.end();
            }
        }
        ImGui::End();
    });

    // ─────────────── Artnet Data Window ──────────────────────────
    w.system<>("WindowArtnetData").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app             = App::get(it.world());
        auto* ui             = &app.get_mut<App::UIConfig>();
        if (!ui->showArtnetData) return;
        auto selectedPatch   = Patch::getSelected(app);
        auto selectedFixture = Fixture::getSelected(selectedPatch);
        auto selectedUniverse = Artnet::Universe::getSelected(selectedPatch);

        if(ImGui::Begin("Artnet Data", &ui->showArtnetData)){
            if(selectedPatch.is_valid()){
                std::vector<flecs::entity> univs;
                int currentUnivIndex = -1;
                Artnet::Universe::iterate(selectedPatch, [&](flecs::entity universe, Artnet::Universe::Properties& props){
                    if(universe == selectedUniverse){
                        currentUnivIndex = (int)univs.size();
                    }
                    univs.push_back(universe);
                });

                if (ImGui::Button("< Prev") && !univs.empty()) {
                    int prevIndex = currentUnivIndex - 1;
                    if (prevIndex < 0) prevIndex = (int)univs.size() - 1;
                    Artnet::Universe::select(selectedPatch, univs[prevIndex]);
                    selectedUniverse = univs[prevIndex];
                }
                ImGui::SameLine();

                ImGui::SetNextItemWidth(200.f);
                std::string comboLabel = selectedUniverse.is_valid() ? selectedUniverse.name().c_str() : "Select Universe...";
                if (ImGui::BeginCombo("##UniverseCombo", comboLabel.c_str())) {
                    for (int i = 0; i < (int)univs.size(); ++i) {
                        bool isSel = (univs[i] == selectedUniverse);
                        if (ImGui::Selectable(univs[i].name().c_str(), isSel)) {
                            Artnet::Universe::select(selectedPatch, univs[i]);
                            selectedUniverse = univs[i];
                        }
                        if (isSel) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();

                if (ImGui::Button("Next >") && !univs.empty()) {
                    int nextIndex = currentUnivIndex + 1;
                    if (nextIndex >= (int)univs.size()) nextIndex = 0;
                    Artnet::Universe::select(selectedPatch, univs[nextIndex]);
                    selectedUniverse = univs[nextIndex];
                }
                ImGui::Separator();
            }

            ImGui::BeginChild("##dmxHex", ImGui::GetContentRegionAvail());
            if(selectedUniverse.is_valid()){
                std::vector<MappedField> fields;
                uint32_t colors[2] = { IM_COL32(50,50,140,255), IM_COL32(30,30,70,255) };
                const auto* univProps = selectedUniverse.try_get<Artnet::Universe::Properties>();
                Fixture::iterateInDmxUniverse(selectedPatch, selectedUniverse,
                    [&](flecs::entity fixture, const Fixture::Layout& layout, const Fixture::DmxAddress& dmxAddress){
                        int offset = -1, count = 0;
                        if(dmxAddress.universe == univProps->universeId){
                            offset = dmxAddress.address;
                            count  = layout.pixelCount * layout.channelsPerPixel;
                            if(offset + count > 512) count = 512 - offset;
                        } else if(dmxAddress.universe < univProps->universeId){
                            offset = 0;
                            count  = layout.pixelCount * layout.channelsPerPixel - (512 - dmxAddress.address);
                            for(int i = dmxAddress.universe + 1; i < (int)univProps->universeId; i++) count -= 512;
                        }
                        if(count <= 0 || offset < 0) return;
                        fields.push_back({fixture.name().c_str(), offset, count, 0, (fixture == selectedFixture)});
                });
                std::sort(fields.begin(), fields.end(), [](const MappedField& a, const MappedField& b){ return a.Offset < b.Offset; });
                for(int i = 0; i < (int)fields.size(); i++)
                    fields[i].Color = fields[i].b_selected ? IM_COL32(127,127,0,255) : colors[i%2];
                uint8_t localChannels[512] = {0};
                bool channelsCopied = false;
                if (univProps) {
                    auto prog = std::atomic_load(&App::currentPatchProgram);
                    if (prog) {
                        for (int ui = 0; ui < prog->universeCount; ++ui) {
                            if (prog->universes[ui].id == univProps->universeId) {
                                std::memcpy(localChannels, prog->universes[ui].buffer, 512);
                                channelsCopied = true;
                                break;
                            }
                        }
                    }
                }

                if (!channelsCopied) {
                    const auto& channels = selectedUniverse.get<Artnet::Universe::Channels>();
                    std::memcpy(localChannels, channels.channels, 512);
                }

                const MappedField* clickedField = nullptr;
                if(DrawHexViewer(localChannels, 512, fields, &clickedField)){
                    if(clickedField){
                        auto cf = selectedPatch.target<Patch::FixtureFolder>().lookup(clickedField->Name.c_str());
                        Fixture::select(selectedPatch, cf);
                    } else Fixture::clearSelection(selectedPatch);
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    });

    // ─────────────── Patch & Network Settings ────────────────────
    w.system<>("WindowPatchSettings").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app          = App::get(it.world());
        auto* ui          = &app.get_mut<App::UIConfig>();
        if (!ui->showNetworkSettings) return;
        auto selectedPatch = Patch::getSelected(app);
        if(ImGui::Begin("Patch & Network Settings", &ui->showNetworkSettings)){
            if(selectedPatch.is_valid()){
                if(auto* s = selectedPatch.try_get_mut<Patch::Settings>()){
                    bool e = false;
                    ImGui::SeparatorText("Network");
                    e |= ImGui::Checkbox("Enable ArtNet Sending", &s->networkEnabled);
                    int sp = s->sourcePort;
                    if(ImGui::InputInt("Source Port", &sp)){ s->sourcePort = std::clamp(sp,1,65535); e = true; }
                    {
                        std::lock_guard<std::mutex> lock(App::rtNetworkStatusMutex);
                        ImGui::TextColored({0.7f, 0.7f, 1.0f, 1.0f}, "Status: %s", App::rtNetworkStatus);
                    }
                    ImGui::SeparatorText("Timing");
                    e |= ImGui::SliderFloat("Refresh Rate (Hz)", &s->refreshRate, 1.f, 120.f, "%.1f Hz");
                    
                    ImGui::SeparatorText("Identify / Find");
                    e |= ImGui::Checkbox("Highlight Selected Fixtures (Find)", &s->highlightSelected);
                    e |= ImGui::SliderFloat("Highlight Frequency", &s->highlightFrequency, 0.1f, 10.f, "%.1f Hz");
                    
                    ImGui::SeparatorText("White Channel (RGBW)");
                    const char* whiteModes[] = {"Auto (extract from RGB)", "Off (W=0)", "Pass-through (raw)"};
                    int wm = (int)s->whiteMode;
                    if (ImGui::Combo("White Mode", &wm, whiteModes, 3)) {
                        s->whiteMode = (Patch::WhiteMode)wm;
                        e = true;
                    }
                    if (s->whiteMode == Patch::WhiteMode::AUTO) {
                        ImGui::TextWrapped("W = min(R,G,B), RGB -= W");
                    } else if (s->whiteMode == Patch::WhiteMode::OFF) {
                        ImGui::TextWrapped("White LED disabled");
                    } else {
                        ImGui::TextWrapped("Shader alpha -> White channel");
                    }

                    
                    
                    ImGui::SeparatorText("Offline Preview");
                    int vfbRes = s->vfbResolution;
                    if (ImGui::SliderInt("Preview Resolution", &vfbRes, 16, 2048)) {
                        s->vfbResolution = vfbRes;
                        e = true;
                    }
                    
                    int wVal = vfbRes;
                    int hVal = vfbRes;
                    if (selectedPatch.has<Patch::RenderArea>()) {
                        const auto& ra = selectedPatch.get<Patch::RenderArea>();
                        float dx = ra.max.x - ra.min.x;
                        float dy = ra.max.y - ra.min.y;
                        if (dx > 0.001f || dy > 0.001f) {
                            if (dx >= dy) {
                                hVal = std::max(1, (int)std::round(vfbRes * dy / dx));
                            } else {
                                wVal = std::max(1, (int)std::round(vfbRes * dx / dy));
                            }
                        }
                    }
                    ImGui::Text("Actual FBO Size: %d x %d (W x H)", wVal, hVal);
                    
                    if(e) selectedPatch.add<Patch::ProgramDirty>();
                }
            } else ImGui::TextDisabled("No patch selected.");
        }
        ImGui::End();
    });

    // ─────────────── Artnet Devices Window ───────────────────────
    w.system<>("WindowArtnetDevices").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app               = App::get(it.world());
        auto* ui               = &app.get_mut<App::UIConfig>();
        if (!ui->showArtnetDevices) return;
        auto selectedPatch     = Patch::getSelected(app);
        auto selectedDev       = Artnet::Device::getSelected(selectedPatch);

        if(ImGui::Begin("Artnet Devices", &ui->showArtnetDevices)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            if(ImGui::Button("+ Add Device")){
                std::vector<flecs::entity> devs;
                Artnet::Device::iterateInPatch(selectedPatch, [&](flecs::entity d, const Artnet::Device::Settings&){ devs.push_back(d); });
                std::string devName = "Device " + std::to_string(devs.size() + 1);
                auto dev = Artnet::Device::create(selectedPatch);
                dev.set_name(devName.c_str());
                Artnet::Device::select(selectedPatch, dev);
                if(auto* s = dev.try_get_mut<Artnet::Device::Settings>()){
                    s->ipAddress = 0xFFFFFFFF; s->startUniverse = 0; s->universeCount = 1;
                }
                selectedPatch.add<Patch::ProgramDirty>();
            }
            ImGui::SameLine();
            bool hasDev = selectedDev.is_valid() && selectedDev.is_alive();
            if(!hasDev) ImGui::BeginDisabled();
            ImGui::PushStyleColor(ImGuiCol_Button,        {0.5f,0.1f,0.1f,1});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.7f,0.15f,0.15f,1});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.4f,0.05f,0.05f,1});
            if(ImGui::Button("Remove")){
                selectedDev.destruct();
                selectedPatch.remove<Patch::SelectedArtnetDevice>(flecs::Wildcard);
                selectedPatch.add<Patch::ProgramDirty>();
            }
            ImGui::PopStyleColor(3);
            if(!hasDev) ImGui::EndDisabled();
            ImGui::PopStyleVar();
            ImGui::Separator();

            if(ImGui::BeginTable("DevTable", 2, ImGuiTableFlags_Resizable)){
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if(ImGui::BeginListBox("##Devs", ImGui::GetContentRegionAvail())){
                    Artnet::Device::iterateInPatch(selectedPatch,
                        [&](flecs::entity d, const Artnet::Device::Settings&){
                            bool b = (d == selectedDev);
                            if(ImGui::Selectable(d.name(), b)) Artnet::Device::select(selectedPatch, d);
                    });
                    ImGui::EndListBox();
                }
                ImGui::TableSetColumnIndex(1);
                if(selectedDev.is_valid()){
                    if(auto* s = selectedDev.try_get_mut<Artnet::Device::Settings>()){
                        bool e = false;
                        ImGui::SeparatorText("Device Settings");
                        char devName[64] = {};
                        std::strncpy(devName, selectedDev.name().c_str(), sizeof(devName)-1);
                        if(ImGui::InputText("Name", devName, sizeof(devName))){
                            selectedDev.set_name(devName);
                        }
                        uint8_t* ip = reinterpret_cast<uint8_t*>(&s->ipAddress);
                        int ipb[4] = {ip[0],ip[1],ip[2],ip[3]};
                        if(ImGui::InputInt4("IP", ipb)){
                            for(int i=0;i<4;i++) ip[i] = (uint8_t)std::clamp(ipb[i],0,255); e = true;
                        }
                        int su = s->startUniverse, uc = s->universeCount;
                        if(ImGui::InputInt("Start Universe", &su)){ s->startUniverse = std::clamp(su,0,32767); e = true; }
                        if(ImGui::InputInt("Universe Count", &uc)){ s->universeCount  = std::clamp(uc,1,32768); e = true; }
                        if(e) selectedPatch.add<Patch::ProgramDirty>();
                    }
                } else ImGui::TextDisabled("Select a device.");
                ImGui::EndTable();
            }
        }
        ImGui::End();
    });

    // ─────────────── Effect Editor ──────────────────────
    w.system<>("WindowEffectEditor").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app          = App::get(it.world());
        auto* ui          = &app.get_mut<App::UIConfig>();
        if (!ui->showScriptEditor) return;
        auto selectedPatch = Patch::getSelected(app);

        if(!editorsInitialized){
            luaEditor  = std::make_unique<TextEditor>();
            glslEditor = std::make_unique<TextEditor>();
            luaEditor->SetLanguage(TextEditor::Language::Lua());
            glslEditor->SetLanguage(TextEditor::Language::Glsl());
            editorsInitialized = true;
        }

        if(ImGui::Begin("Effect Editor", &ui->showScriptEditor)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            auto* settings   = selectedPatch.try_get_mut<Patch::Settings>();
            auto* scriptData = selectedPatch.try_get_mut<Patch::ScriptData>();
            if(!settings || !scriptData){ ImGui::End(); return; }

            flecs::entity cueFolder = selectedPatch.target<CueList::CueFolder>();
            flecs::entity bankFolder = selectedPatch.target<EffectBank::EffectFolder>();

            static flecs::id_t lastPatchId = 0;
            static int lastEditingCueIndex = -99;
            static int lastEditingBankIndex = -99;
            static std::string lastEditorText = "";

            bool targetChanged = (selectedPatch.id() != lastPatchId) || 
                                 (ui->editingCueIndex != lastEditingCueIndex) || 
                                 (ui->editingBankIndex != lastEditingBankIndex);

            if (targetChanged) {
                lastPatchId = selectedPatch.id();
                lastEditingCueIndex = ui->editingCueIndex;
                lastEditingBankIndex = ui->editingBankIndex;

                auto p = std::atomic_load(&App::currentPatchProgram);
                if (p) {
                    p->editingCueIndex.store(ui->editingCueIndex);
                    p->editingBankIndex.store(ui->editingBankIndex);
                }
                
                std::string targetText = "";
                if (ui->editingCueIndex == -1) {
                    targetText = scriptData->glslSource;
                } else if (ui->editingCueIndex == -2 && ui->editingBankIndex >= 0) {
                    flecs::entity fxEnt = getEffectEntityByIndex(bankFolder, ui->editingBankIndex);
                    if (fxEnt.is_valid()) {
                        if (const auto* glsl = fxEnt.try_get<EffectBank::Effect::GlslSource>()) {
                            targetText = glsl->value;
                        }
                    }
                }
                glslEditor->SetText(targetText);
                lastEditorText = targetText;
                hasUncompiledChanges = false;
            }

            // ── Auto-compile debouncing logic ──
            static double lastChangeTime = 0.0;
            std::string currentText = glslEditor->GetText();
            if (!targetChanged && currentText != lastEditorText) {
                lastEditorText = currentText;
                hasUncompiledChanges = true;
                lastChangeTime = ImGui::GetTime();
            }

            if (hasUncompiledChanges && (ImGui::GetTime() - lastChangeTime > 0.3)) {
                std::string newSource = glslEditor->GetText();
                if (ui->editingCueIndex == -1) {
                    scriptData->glslSource = newSource;
                    if (settings) {
                        try {
                            std::ofstream f(settings->shaderPath);
                            if (f) f << newSource;
                        } catch (...) {}
                    }
                } else if (ui->editingCueIndex == -2 && ui->editingBankIndex >= 0) {
                    flecs::entity fxEnt = getEffectEntityByIndex(bankFolder, ui->editingBankIndex);
                    if (fxEnt.is_valid()) {
                        fxEnt.set<EffectBank::Effect::GlslSource>({newSource});
                    }
                }
                selectedPatch.add<Patch::ProgramDirty>();
                hasUncompiledChanges = false;
            }

            // ── Editor Header Row ──
            if (ui->editingCueIndex == -1) {
                ImGui::Text("Editing: Default Shader"); ImGui::SameLine();
                const char* modes[] = {"C++ Sine", "Lua Script", "GLSL Shader"};
                int mode = (int)settings->renderMode;
                ImGui::SetNextItemWidth(120); 
                if(ImGui::Combo("Mode", &mode, modes, 3)){ 
                    settings->renderMode = (Patch::RenderMode)mode; 
                    selectedPatch.add<Patch::ProgramDirty>(); 
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110);
                if(ImGui::InputInt("Preview Res", &settings->vfbResolution)) {
                    settings->vfbResolution = std::clamp(settings->vfbResolution, 16, 2048);
                    selectedPatch.add<Patch::ProgramDirty>();
                }
            } else if (ui->editingCueIndex == -2 && ui->editingBankIndex >= 0) {
                flecs::entity fxEnt = getEffectEntityByIndex(bankFolder, ui->editingBankIndex);
                if (fxEnt.is_valid()) {
                    ImGui::Text("Editing Effect:"); ImGui::SameLine();
                    char fxName[64];
                    std::strncpy(fxName, fxEnt.name().c_str(), sizeof(fxName)-1);
                    ImGui::SetNextItemWidth(150);
                    if (ImGui::InputText("Name##fx", fxName, sizeof(fxName))) {
                        fxEnt.set_name(fxName);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Add to Cue List")) {
                        int nextOrder = 0;
                        cueFolder.children([&](flecs::entity child) {
                            if (child.has<CueList::Cue::Is>()) {
                                if (const auto* ord = child.try_get<CueList::Cue::IndexOrder>()) {
                                    if (ord->value >= nextOrder) nextOrder = ord->value + 1;
                                }
                            }
                        });
                        std::string cueName = fxEnt.name().c_str();
                        auto newCue = it.world().entity()
                            .child_of(cueFolder)
                            .add<CueList::Cue::Is>()
                            .set<CueList::Cue::HoldDuration>({5.0f})
                            .set<CueList::Cue::FadeDuration>({2.0f})
                            .set<CueList::Cue::IndexOrder>({nextOrder})
                            .add<CueList::Cue::TargetEffect>(fxEnt);
                        newCue.set_name(cueName.c_str());
                        selectedPatch.add<Patch::ProgramDirty>();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Release")) {
                        setEditingCueIndex(app, -1);
                    }
                }
            }

            ImGui::SameLine();
            static int presetIdx = -1;
            auto presetGetter = [](void*, int i, const char** out) -> bool { *out = kPresets[i].name; return true; };
            ImGui::SetNextItemWidth(130);
            if(ImGui::Combo("Preset", &presetIdx, presetGetter, nullptr, kPresetCount) && presetIdx >= 0){
                std::string presetGlsl = kPresets[presetIdx].glsl;
                glslEditor->SetText(presetGlsl);
                if (ui->editingCueIndex == -1) {
                    scriptData->glslSource = presetGlsl;
                    if (settings) {
                        try {
                            std::ofstream f(settings->shaderPath);
                            if (f) f << presetGlsl;
                        } catch (...) {}
                    }
                } else if (ui->editingCueIndex == -2 && ui->editingBankIndex >= 0) {
                    flecs::entity fxEnt = getEffectEntityByIndex(bankFolder, ui->editingBankIndex);
                    if (fxEnt.is_valid()) {
                        fxEnt.set<EffectBank::Effect::GlslSource>({presetGlsl});
                    }
                }
                selectedPatch.add<Patch::ProgramDirty>();
                hasUncompiledChanges = false;
            }

            ImGui::SameLine();
            static bool showShaderHelp = false;
            if (ImGui::Button("Help")) {
                showShaderHelp = !showShaderHelp;
            }

            ImGui::Separator();

            auto pProg = std::atomic_load(&App::currentPatchProgram);
            bool showZSlice = (pProg && settings && settings->renderMode == Patch::RenderMode::GLSL);
 
            // --- Editor Preview Controls (Placed prominently at the top!) ---
            if (showZSlice || pProg) {
                if (showZSlice) {
                    float slice = pProg->zSlice.load();
                    if (ImGui::SliderFloat("Z-Slice (Depth)", &slice, 0.0f, 1.0f, "%.3f")) {
                        pProg->zSlice.store(slice);
                    }
                }
                if (pProg) {
                    bool overrideActive = ui->editorPreviewOverrideActive;
                    if (ImGui::Checkbox("Override Generative Data for Preview", &overrideActive)) {
                        ui->editorPreviewOverrideActive = overrideActive;
                        std::lock_guard<std::mutex> lock(pProg->generativeMutex);
                        pProg->editorPreviewOverrideActive = overrideActive;
                        if (overrideActive) {
                            if (pProg->generativeRuntime) {
                                pProg->editorPreviewOverrideUbo = pProg->generativeRuntime->getUboState();
                            } else {
                                std::memset(&pProg->editorPreviewOverrideUbo, 0, sizeof(pProg->editorPreviewOverrideUbo));
                                pProg->editorPreviewOverrideUbo.activeStops = 0;
                                pProg->editorPreviewOverrideUbo.velocity = 0.5f;
                                pProg->editorPreviewOverrideUbo.complexity = 0.5f;
                                pProg->editorPreviewOverrideUbo.scale = 0.5f;
                                pProg->editorPreviewOverrideUbo.distortion = 0.5f;
                                pProg->editorPreviewOverrideUbo.asymmetry = 0.5f;
                                pProg->editorPreviewOverrideUbo.intensity = 0.5f;
                            }
                        }
                    }
                    
                    if (overrideActive) {
                        ImGui::Indent();
                        
                        std::lock_guard<std::mutex> lock(pProg->generativeMutex);
                        
                        // Palette selector
                        std::vector<std::string> palNames;
                        std::vector<const char*> palPtrs;
                        for (const auto& pal : pProg->palettePool) {
                            palNames.push_back(pal.name);
                        }
                        for (const auto& n : palNames) palPtrs.push_back(n.c_str());
                        
                        int currentPalIdx = ui->editorPreviewOverridePaletteIdx;
                        if (ImGui::Combo("Preview Palette", &currentPalIdx, palPtrs.data(), (int)palPtrs.size())) {
                            ui->editorPreviewOverridePaletteIdx = currentPalIdx;
                            if (currentPalIdx >= 0 && currentPalIdx < (int)pProg->palettePool.size()) {
                                const auto& pal = pProg->palettePool[currentPalIdx];
                                pProg->editorPreviewOverrideUbo.activeStops = std::min((int)pal.stops.size(), 16);
                                for (int i = 0; i < pProg->editorPreviewOverrideUbo.activeStops; i++) {
                                    pProg->editorPreviewOverrideUbo.palette[i] = pal.stops[i];
                                }
                            }
                        }
                        
                        // 6 Motive control sliders
                        ImGui::SliderFloat("Velocity",   &pProg->editorPreviewOverrideUbo.velocity,   0.0f, 1.0f, "%.2f");
                        ImGui::SliderFloat("Complexity", &pProg->editorPreviewOverrideUbo.complexity, 0.0f, 1.0f, "%.2f");
                        ImGui::SliderFloat("Scale",      &pProg->editorPreviewOverrideUbo.scale,      0.0f, 1.0f, "%.2f");
                        ImGui::SliderFloat("Distortion", &pProg->editorPreviewOverrideUbo.distortion, 0.0f, 1.0f, "%.2f");
                        ImGui::SliderFloat("Asymmetry",  &pProg->editorPreviewOverrideUbo.asymmetry,  0.0f, 1.0f, "%.2f");
                        ImGui::SliderFloat("Intensity",  &pProg->editorPreviewOverrideUbo.intensity,  0.0f, 1.0f, "%.2f");
                        
                        ImGui::Unindent();
                    }
                }
                ImGui::Separator();
            }
 
            const float kLogHeight = 100.f;
            const float kSepHeight = ImGui::GetStyle().ItemSpacing.y + 1.f;
            float extraSpacing = ImGui::GetTextLineHeightWithSpacing();
            if (showZSlice) {
                extraSpacing += ImGui::GetTextLineHeightWithSpacing() + kSepHeight;
            }
            if (pProg) {
                extraSpacing += ImGui::GetTextLineHeightWithSpacing() + kSepHeight; // Checkbox
                if (ui->editorPreviewOverrideActive) {
                    extraSpacing += (ImGui::GetTextLineHeightWithSpacing() + kSepHeight) * 7.f; // 1 combo + 6 sliders
                }
            }
            float editorHeight = ImGui::GetContentRegionAvail().y - kLogHeight - kSepHeight * 3.f - extraSpacing;
            if(editorHeight < 80.f) editorHeight = 80.f;
 
            glslEditor->Render("GlslEd", ImVec2(0, editorHeight));

            ImGui::Separator();
            if (hasUncompiledChanges) {
                ImGui::TextColored({1.0f, 0.5f, 0.0f, 1.0f}, "* Unsaved Changes (compiling...)");
            } else {
                ImGui::TextColored({0.2f, 1.0f, 0.2f, 1.0f}, "Compiled & Saved");
            }

            ImGui::Separator();
            ImGui::Text("Compilation Log:");
            ImGui::BeginChild("##Log", ImVec2(0, kLogHeight), true);
            std::string displayLog = "";
            auto prog = std::atomic_load(&App::currentPatchProgram);
            if (prog) {
                if (ui->editingCueIndex == -1) {
                    displayLog = prog->defaultCompilerLog;
                } else if (ui->editingCueIndex == -2 && ui->editingBankIndex >= 0 && ui->editingBankIndex < (int)prog->compiledBankEffects.size()) {
                    displayLog = prog->compiledBankEffects[ui->editingBankIndex].compilerLog;
                }
            }

            bool ok = displayLog == "Compile successful!";
            if(ok)
                ImGui::TextColored({0.2f,1,0.2f,1}, "%s", displayLog.c_str());
            else if(!displayLog.empty())
                ImGui::TextColored({1,0.3f,0.3f,1}, "%s", displayLog.c_str());
            else
                ImGui::TextDisabled("No log yet.");
            ImGui::EndChild();

            if (showShaderHelp) {
                ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Shader Documentation & Guides", &showShaderHelp)) {
                    ImGui::TextWrapped("This reference guide covers the shader APIs and parameters available in PixelMapper.");
                    ImGui::Separator();
                    
                    if (ImGui::CollapsingHeader("1. Coordinate Systems (vPixelPos2D / vPixelPos3D)")) {
                        ImGui::BulletText("vPixelPos2D (or iPixelPos2D): vec2");
                        ImGui::Indent();
                        ImGui::TextWrapped("Normalized [0.0, 1.0] coordinates across the XY projection canvas of all active fixtures. "
                                           "Calculated by projecting the coordinates to 2D and dividing by the layout width/height. "
                                           "Shaders using this automatically scale to fill the layout regardless of its physical size.");
                        ImGui::Unindent();
                        
                        ImGui::BulletText("vPixelPos3D (or iPixelPos3D): vec3");
                        ImGui::Indent();
                        ImGui::TextWrapped("Raw, unscaled physical 3D coordinates (in millimeters, e.g. [-500.0, 500.0]). "
                                           "Sizing, bounding, and distances are specified in real-world dimensions. "
                                           "In 2D offline preview mode, the physical coordinate is reconstructed dynamically using pixelPosMin/Max and the zSlice.");
                        ImGui::Unindent();
                    }
                    
                    if (ImGui::CollapsingHeader("2. Input Uniforms")) {
                        ImGui::Text("The following uniforms are automatically populated per frame:");
                        ImGui::BulletText("time / iTime (float) - Playback time in seconds.");
                        ImGui::BulletText("resolution / iResolution (vec2/vec3) - Viewport dimensions.");
                        ImGui::BulletText("pixelCount (float) - Total number of physical output pixels.");
                        ImGui::BulletText("pixelPosMin (vec3) - Minimum XYZ bounding box of the active fixtures.");
                        ImGui::BulletText("pixelPosMax (vec3) - Maximum XYZ bounding box of the active fixtures.");
                        ImGui::BulletText("zSlice (float) - Depth slider value [0, 1] during editor preview.");
                    }
                    
                    if (ImGui::CollapsingHeader("3. Shadertoy Local Runner")) {
                        ImGui::TextWrapped("PixelMapper compiles unmodified Shadertoy fragment shaders automatically. Copy-paste code directly from shadertoy.com. The editor wraps the mainImage function:");
                        ImGui::TextDisabled("void mainImage(out vec4 fragColor, in vec2 fragCoord)");
                        ImGui::TextWrapped("Mouse coordinates are bound to iMouse (xy: drag, zw: click).");
                    }
                    
                    if (ImGui::CollapsingHeader("4. Volumetric Code Example")) {
                        ImGui::Text("Here is a boilerplate volumetric shader template:");
                        ImGui::Separator();
                        ImGui::TextDisabled(
                            "#version 150\n"
                            "in vec3 vPixelPos3D;\n"
                            "in vec2 vPixelPos2D;\n"
                            "out vec4 fragColor;\n"
                            "#define iPixelPos3D vPixelPos3D\n"
                            "#define iPixelPos2D vPixelPos2D\n"
                            "uniform float time;\n"
                            "uniform vec3 pixelPosMin;\n"
                            "uniform vec3 pixelPosMax;\n"
                            "\n"
                            "void main() {\n"
                            "    // Create a wave that sweeps physical Z coords\n"
                            "    float zMin = pixelPosMin.z;\n"
                            "    float zMax = pixelPosMax.z;\n"
                            "    float centerZ = mix(zMin, zMax, 0.5 + 0.5 * sin(time * 2.0));\n"
                            "    \n"
                            "    float edge = smoothstep(30.0, 0.0, abs(iPixelPos3D.z - centerZ));\n"
                            "    fragColor = vec4(edge * 0.1, edge * 0.8, edge, 1.0);\n"
                        );
                    }

                    if (ImGui::CollapsingHeader("5. Generative Visual Engine (V3)")) {
                        ImGui::TextWrapped("The Generative Visual Engine synchronizes a dynamic, non-repeating show control loop with the GPU. "
                                           "All shaders automatically have the active color stops and motive parameters injected by name.");
                        ImGui::BulletText("Motive Uniforms (float):");
                        ImGui::Indent();
                        ImGui::BulletText("velocity - Active kinematic movement rate.");
                        ImGui::BulletText("complexity - Scene pattern density.");
                        ImGui::BulletText("scale - Spatial coordinate zoom/scaling.");
                        ImGui::BulletText("distortion - Noise warp/displacement amount.");
                        ImGui::BulletText("asymmetry - Skeletal pattern offset/tilt.");
                        ImGui::BulletText("intensity - Global brightness multiplier.");
                        ImGui::Unindent();

                        ImGui::BulletText("Gradient Sampler Helpers:");
                        ImGui::Indent();
                        ImGui::BulletText("vec4 samplePalette(float pos)");
                        ImGui::Indent();
                        ImGui::TextWrapped("Returns color at pos clamped to [0.0, 1.0]. Blends stop colors with adjustable softness.");
                        ImGui::Unindent();
                        ImGui::BulletText("vec4 samplePaletteWrapped(float pos)");
                        ImGui::Indent();
                        ImGui::TextWrapped("Returns color at fract(pos). Useful for looping or tileable coordinate mappings.");
                        ImGui::Unindent();
                        ImGui::Unindent();

                        ImGui::Spacing();
                        ImGui::Text("Example:");
                        ImGui::TextDisabled(
                            "void main() {\n"
                            "    // Scale & animate XY coordinates using motives\n"
                            "    float coord = iPixelPos2D.x * scale + time * velocity;\n"
                            "    // Sample gradient continuously\n"
                            "    vec4 col = samplePaletteWrapped(coord);\n"
                            "    fragColor = col * intensity;\n"
                            "}"
                        );
                    }
                }
                ImGui::End();
            }
        }
        ImGui::End();
    });



    // ─────────────── WindowCues ─────────────────────────────
    w.system<>("WindowCues").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app          = App::get(it.world());
        auto* ui          = &app.get_mut<App::UIConfig>();
        if (!ui->showCuesWindow) return;
        auto selectedPatch = Patch::getSelected(app);

        if(ImGui::Begin("Cue List", &ui->showCuesWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            flecs::entity cueFolder = selectedPatch.target<CueList::CueFolder>();
            flecs::entity bankFolder = selectedPatch.target<EffectBank::EffectFolder>();
            if (!cueFolder.is_valid() || !bankFolder.is_valid()) { ImGui::TextDisabled("No folders."); ImGui::End(); return; }
            auto* session = cueFolder.try_get_mut<CueList::SessionState>();
            if(!session){ ImGui::End(); return; }

            // ── Transport ──
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            ImGui::Checkbox("Auto-advance", &session->autoAdvance); ImGui::SameLine();
            ImGui::Checkbox("Loop",         &session->loop);

            auto activeProg = std::atomic_load(&App::currentPatchProgram);
            bool isGenPlayback = activeProg && activeProg->generativeSettings.masterEnabled && 
                                 activeProg->generativeRuntime && (activeProg->activeCueIndex.load() < 0);
            
            ImGui::SameLine(0, 20.0f);
            if (isGenPlayback) {
                ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "[Generative Playback Active]");
            } else if (activeProg && activeProg->activeCueIndex.load() >= 0) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[Cue Playback Active]");
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[Standby / Stopped]");
            }

            struct CueEntry {
                flecs::entity entity;
                int order = 0;
                float hold = 5.0f;
                float fade = 2.0f;
            };
            std::vector<CueEntry> cues;

            cueFolder.children([&](flecs::entity child) {
                if (child.has<CueList::Cue::Is>()) {
                    CueEntry entry;
                    entry.entity = child;
                    if (const auto* ord = child.try_get<CueList::Cue::IndexOrder>()) entry.order = ord->value;
                    if (const auto* h = child.try_get<CueList::Cue::HoldDuration>()) entry.hold = h->value;
                    if (const auto* f = child.try_get<CueList::Cue::FadeDuration>()) entry.fade = f->value;
                    cues.push_back(entry);
                }
            });

            std::sort(cues.begin(), cues.end(), [](const CueEntry& a, const CueEntry& b) {
                return a.order < b.order;
            });

            auto triggerCue = [&](int idx){
                if(idx < 0 || idx >= (int)cues.size()) return;
                const auto& cueEntry = cues[idx];
                session->activeIndex = idx;
                session->holdTimer   = 0.f;
                auto program = std::atomic_load(&App::currentPatchProgram);
                if (program) {
                    program->pendingCrossfadeDuration.store(cueEntry.fade);
                    program->activeCueIndex.store(idx);
                }
            };

            ImGui::SameLine();
            if(ImGui::Button("<< Prev")){
                if(!cues.empty()){
                    int prev = session->activeIndex - 1;
                    if(prev < 0) prev = session->loop ? (int)cues.size()-1 : 0;
                    triggerCue(prev);
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Next >>")){
                if(!cues.empty()){
                    int next = session->activeIndex + 1;
                    if(next >= (int)cues.size()) next = session->loop ? 0 : (int)cues.size()-1;
                    triggerCue(next);
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Stop")){
                session->activeIndex = -1;
                session->holdTimer = 0.f;
                auto program = std::atomic_load(&App::currentPatchProgram);
                if (program) {
                    program->activeCueIndex.store(-1);
                }
            }

            ImGui::SameLine();
            if(ImGui::Button("+ Add Cue")){
                int nextOrder = 0;
                for (const auto& cue : cues) {
                    if (cue.order >= nextOrder) {
                        nextOrder = cue.order + 1;
                    }
                }
                std::string cueName = "Cue " + std::to_string(cues.size() + 1);

                flecs::entity bankFolder = selectedPatch.target<EffectBank::EffectFolder>();
                flecs::entity targetFx;
                
                // Find first effect in bank
                bankFolder.children([&](flecs::entity child) {
                    if (child.has<EffectBank::Effect::Is>() && !targetFx.is_valid()) {
                        targetFx = child;
                    }
                });

                // If no effects in bank, create a default one
                if (!targetFx.is_valid()) {
                    auto* scriptData = selectedPatch.try_get<Patch::ScriptData>();
                    std::string glsl = scriptData ? scriptData->glslSource : 
                        "#version 150\n"
                        "in vec2 uv;\n"
                        "out vec4 fragColor;\n"
                        "uniform float time;\n"
                        "uniform vec2 resolution;\n"
                        "void main() {\n"
                        "    fragColor = vec4(uv.x, uv.y, sin(time)*0.5+0.5, 1.0);\n"
                        "}\n";
                    targetFx = it.world().entity()
                        .child_of(bankFolder)
                        .add<EffectBank::Effect::Is>()
                        .set<EffectBank::Effect::GlslSource>({glsl})
                        .set<Patch::GPUProgram>({});
                    targetFx.set_name("Effect 1");
                }

                auto newCue = it.world().entity()
                    .child_of(cueFolder)
                    .add<CueList::Cue::Is>()
                    .set<CueList::Cue::HoldDuration>({5.f})
                    .set<CueList::Cue::FadeDuration>({2.f})
                    .set<CueList::Cue::IndexOrder>({nextOrder})
                    .add<CueList::Cue::TargetEffect>(targetFx);
                newCue.set_name(cueName.c_str());

                if (session->activeIndex < 0) session->activeIndex = 0;
                selectedPatch.add<Patch::ProgramDirty>();
            }
            ImGui::PopStyleVar();

            if (ImGui::BeginChild("##CueItems", ImVec2(0, 0), true)) {
                if (ImGui::BeginTable("CueTable", 6, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.5f);
                    ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                    ImGui::TableSetupColumn("Hold", ImGuiTableColumnFlags_WidthFixed, 80.f);
                    ImGui::TableSetupColumn("Fade", ImGuiTableColumnFlags_WidthFixed, 80.f);
                    ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < (int)cues.size(); i++) {
                        auto& cueEntry = cues[i];
                        
                        bool isActive = false;
                        float progress = 0.0f;
                        ImU32 progressColor = IM_COL32(100, 255, 100, 255);
                        bool showProgress = false;

                        float crossfadeProgress = 1.0f;
                        int previousCueIndex = -2;
                        int currentCueIndex = -1;
                        bool isGenPlayback = false;

                        auto activeProg = std::atomic_load(&App::currentPatchProgram);
                        if (activeProg) {
                            isGenPlayback = activeProg->generativeSettings.masterEnabled && 
                                            activeProg->generativeRuntime && 
                                            (activeProg->activeCueIndex.load() < 0);
                            if (isGenPlayback) {
                                auto rt = activeProg->generativeRuntime;
                                int targetIdx = rt->getTargetShaderIndex();
                                int activeIdx = rt->getActiveShaderIndex();
                                float fadeProg = rt->getShaderFadeProgress();

                                if (targetIdx >= 0) {
                                    currentCueIndex = targetIdx;
                                    previousCueIndex = activeIdx;
                                    crossfadeProgress = (fadeProg >= 1.0f) ? 0.0f : fadeProg;
                                } else {
                                    currentCueIndex = activeIdx;
                                    previousCueIndex = -2;
                                    crossfadeProgress = 1.0f;
                                }
                            } else {
                                currentCueIndex = session->activeIndex;
                                previousCueIndex = activeProg->previousCueIndex;
                                crossfadeProgress = activeProg->crossfadeProgress.load();
                            }
                        } else {
                            currentCueIndex = session->activeIndex;
                        }

                        isActive = (i == currentCueIndex);

                        if (isActive) {
                            if (!isGenPlayback && session->autoAdvance && cueEntry.hold > 0.001f) {
                                progress = std::clamp(session->holdTimer / cueEntry.hold, 0.0f, 1.0f);
                                progressColor = IM_COL32(100, 255, 100, 255); // Green for hold
                                showProgress = true;
                            }
                        } else if (i == previousCueIndex && crossfadeProgress < 1.0f) {
                            progress = crossfadeProgress;
                            progressColor = IM_COL32(100, 200, 255, 255); // Blue for crossfade
                            showProgress = true;
                        }

                        flecs::entity targetEffect = cueEntry.entity.target<CueList::Cue::TargetEffect>();
                        bool isEditingThis = false;
                        int targetFxIdx = -1;
                        if (targetEffect.is_valid()) {
                            // Find the index of targetEffect in the bank folder children
                            int idxCounter = 0;
                            bankFolder.children([&](flecs::entity child) {
                                if (child.has<EffectBank::Effect::Is>()) {
                                    if (child == targetEffect) {
                                        targetFxIdx = idxCounter;
                                    }
                                    idxCounter++;
                                }
                            });
                            isEditingThis = (ui->editingCueIndex == -2 && ui->editingBankIndex == targetFxIdx);
                        }

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();

                        ImGui::PushID(cueEntry.entity.id());

                        if (showProgress) {
                            ImGui::Dummy(ImVec2(14, 14));
                            ImVec2 pMin = ImGui::GetItemRectMin();
                            DrawCompactProgress(ImGui::GetWindowDrawList(), pMin, 6.0f, progress, IM_COL32(100, 100, 100, 255), progressColor);
                            ImGui::SameLine();
                        } else {
                            if (isActive) {
                                ImGui::TextColored({0.2f, 1.0f, 0.2f, 1.0f}, "> ");
                                ImGui::SameLine();
                            } else {
                                ImGui::Text("  ");
                                ImGui::SameLine();
                            }
                        }

                        char label[128];
                        std::snprintf(label, sizeof(label), "Cue %d##select", i + 1);
                        if (ImGui::Selectable(label, isActive)) {
                            triggerCue(i);
                        }
                        if (isEditingThis) {
                            float pulse = std::sin((float)ImGui::GetTime() * 6.0f) * 0.5f + 0.5f;
                            ImU32 flashColor = IM_COL32(230, 140, 20, (int)(pulse * 60.f + 15.f)); // Pulsing orange background
                            ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), flashColor, 2.0f);
                        }

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            uint64_t dragCid = cueEntry.entity.id();
                            ImGui::SetDragDropPayload("DND_CUE_ORDER", &dragCid, sizeof(dragCid));
                            ImGui::Text("Move Cue %d", i + 1);
                            ImGui::EndDragDropSource();
                        }

                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_CUE_ORDER")) {
                                uint64_t dragCid = *(const uint64_t*)payload->Data;
                                flecs::entity dragCue(it.world(), dragCid);
                                reorderCues(selectedPatch, dragCue, cueEntry.entity);
                            }
                            ImGui::EndDragDropTarget();
                        }

                        ImGui::TableNextColumn();
                        std::string comboLabel = targetEffect.is_valid() ? targetEffect.name().c_str() : "None";
                        ImGui::SetNextItemWidth(-1);
                        if (ImGui::BeginCombo("##EffectCombo", comboLabel.c_str())) {
                            std::vector<flecs::entity> fxList;
                            bankFolder.children([&](flecs::entity child) {
                                if (child.has<EffectBank::Effect::Is>()) {
                                    fxList.push_back(child);
                                }
                            });
                            for (auto& fx : fxList) {
                                bool isSel = (fx == targetEffect);
                                if (ImGui::Selectable(fx.name().c_str(), isSel)) {
                                    cueEntry.entity.remove<CueList::Cue::TargetEffect>(flecs::Wildcard);
                                    cueEntry.entity.add<CueList::Cue::TargetEffect>(fx);
                                    selectedPatch.add<Patch::ProgramDirty>();
                                }
                                if (isSel) {
                                    ImGui::SetItemDefaultFocus();
                                }
                            }
                            ImGui::EndCombo();
                        }

                        ImGui::TableNextColumn();
                        float hold = cueEntry.hold;
                        ImGui::SetNextItemWidth(70);
                        if (ImGui::InputFloat("##h", &hold, 0.0f, 0.0f, "%.1fs")) {
                            cueEntry.entity.set<CueList::Cue::HoldDuration>({std::max(0.0f, hold)});
                        }

                        ImGui::TableNextColumn();
                        float fade = cueEntry.fade;
                        ImGui::SetNextItemWidth(70);
                        if (ImGui::InputFloat("##f", &fade, 0.0f, 0.0f, "%.1fs")) {
                            cueEntry.entity.set<CueList::Cue::FadeDuration>({std::max(0.0f, fade)});
                        }

                        ImGui::TableNextColumn();
                        if (isEditingThis) {
                            ImGui::PushStyleColor(ImGuiCol_Button, {0.8f, 0.5f, 0.1f, 1.0f});
                            if (ImGui::Button("Edit")) {
                                setEditingCueIndex(app, -1);
                            }
                            ImGui::PopStyleColor();
                        } else {
                            if (ImGui::Button("Edit") && targetFxIdx >= 0) {
                                setEditingBankIndex(app, targetFxIdx);
                                ui->showScriptEditor = true;
                            }
                        }

                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Button, {0.6f, 0.1f, 0.1f, 1.0f});
                        if (ImGui::Button("Remove")) {
                            cueEntry.entity.destruct();
                            if (session->activeIndex == i) {
                                session->activeIndex = std::clamp(session->activeIndex, -1, (int)cues.size() - 2);
                            } else if (session->activeIndex > i) {
                                session->activeIndex--;
                            }
                            i--;
                            selectedPatch.add<Patch::ProgramDirty>();
                        }
                        ImGui::PopStyleColor();

                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    });

    // ─────────────── Effect Bank Window ───────────────────────────
    w.system<>("WindowEffectBank").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app          = App::get(it.world());
        auto* ui          = &app.get_mut<App::UIConfig>();
        if (!ui->showEffectBankWindow) return;
        auto selectedPatch = Patch::getSelected(app);

        if(ImGui::Begin("Effect Bank", &ui->showEffectBankWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            flecs::entity bankFolder = selectedPatch.target<EffectBank::EffectFolder>();
            if (!bankFolder.is_valid()) { ImGui::TextDisabled("No bank folder."); ImGui::End(); return; }
            auto* bankSession = bankFolder.try_get_mut<EffectBank::SessionState>();
            flecs::entity cueFolder = selectedPatch.target<CueList::CueFolder>();
            if (!cueFolder.is_valid() || !bankSession) { ImGui::End(); return; }

            if(ImGui::Button("+ Add Effect")){
                std::vector<flecs::entity> fxList;
                bankFolder.children([&](flecs::entity child) {
                    if (child.has<EffectBank::Effect::Is>()) {
                        fxList.push_back(child);
                    }
                });
                std::string fxName = "Effect " + std::to_string(fxList.size() + 1);
                std::string glsl = 
                    "#version 150\n"
                    "in vec2 uv;\n"
                    "out vec4 fragColor;\n"
                    "uniform float time;\n"
                    "uniform vec2 resolution;\n"
                    "void main() {\n"
                    "    fragColor = vec4(uv.x, uv.y, sin(time)*0.5+0.5, 1.0);\n"
                    "}\n";
                auto newFx = it.world().entity()
                    .child_of(bankFolder)
                    .add<EffectBank::Effect::Is>()
                    .set<EffectBank::Effect::GlslSource>({glsl})
                    .set<Patch::GPUProgram>({});
                newFx.set_name(fxName.c_str());
                
                // Immediately select for editing and open editor window
                setEditingBankIndex(app, (int)fxList.size());
                ui->showScriptEditor = true;

                selectedPatch.add<Patch::ProgramDirty>();
            }

            ImGui::SameLine();
            static char filterBuf[128] = "";
            ImGui::SetNextItemWidth(180.f);
            ImGui::InputTextWithHint("##EffectFilter", "Search effects...", filterBuf, sizeof(filterBuf));

            ImGui::Separator();

            if (ImGui::BeginChild("##BankItems", ImVec2(0, 0), true)) {
                if (ImGui::BeginTable("BankTable", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 110.f);
                    ImGui::TableSetupColumn("Duplicate", ImGuiTableColumnFlags_WidthFixed, 80.f);
                    ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableHeadersRow();

                    std::vector<flecs::entity> fxList;
                    std::string filterStr = filterBuf;
                    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

                    bankFolder.children([&](flecs::entity child) {
                        if (child.has<EffectBank::Effect::Is>()) {
                            if (!filterStr.empty()) {
                                std::string name = child.name().c_str();
                                std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                                if (name.find(filterStr) == std::string::npos) {
                                    return;
                                }
                            }
                            fxList.push_back(child);
                        }
                    });

                    for (int i = 0; i < (int)fxList.size(); i++) {
                        flecs::entity fx = fxList[i];
                        bool isEditing = (ui->editingCueIndex == -2 && ui->editingBankIndex == i);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();

                        ImGui::PushID(fx.id());

                        if (isEditing) {
                            ImGui::TextColored({1.0f, 0.7f, 0.2f, 1.0f}, "* %s", fx.name().c_str());
                        } else {
                            ImGui::Text("%s", fx.name().c_str());
                        }

                        ImGui::TableNextColumn();
                        if (ImGui::Button("Add to Cue List")) {
                            int nextOrder = 0;
                            cueFolder.children([&](flecs::entity child) {
                                if (child.has<CueList::Cue::Is>()) {
                                    if (const auto* ord = child.try_get<CueList::Cue::IndexOrder>()) {
                                        if (ord->value >= nextOrder) nextOrder = ord->value + 1;
                                    }
                                }
                            });
                            
                            auto newCue = it.world().entity()
                                .child_of(cueFolder)
                                .add<CueList::Cue::Is>()
                                .set<CueList::Cue::HoldDuration>({5.0f})
                                .set<CueList::Cue::FadeDuration>({2.0f})
                                .set<CueList::Cue::IndexOrder>({nextOrder})
                                .add<CueList::Cue::TargetEffect>(fx);
                            newCue.set_name(fx.name().c_str());
                            selectedPatch.add<Patch::ProgramDirty>();
                        }

                        ImGui::TableNextColumn();
                        if (ImGui::Button("Duplicate")) {
                            std::string glsl = "";
                            if (const auto* src = fx.try_get<EffectBank::Effect::GlslSource>()) {
                                glsl = src->value;
                            }
                            std::string newName = std::string(fx.name().c_str()) + " Copy";
                            auto newFx = it.world().entity()
                                .child_of(bankFolder)
                                .add<EffectBank::Effect::Is>()
                                .set<EffectBank::Effect::GlslSource>({glsl})
                                .set<Patch::GPUProgram>({});
                            newFx.set_name(newName.c_str());
                            selectedPatch.add<Patch::ProgramDirty>();
                        }

                        ImGui::TableNextColumn();
                        if (isEditing) {
                            ImGui::PushStyleColor(ImGuiCol_Button, {0.8f, 0.5f, 0.1f, 1.0f});
                            if (ImGui::Button("Edit")) {
                                setEditingCueIndex(app, -1);
                            }
                            ImGui::PopStyleColor();
                        } else {
                            if (ImGui::Button("Edit")) {
                                setEditingBankIndex(app, i);
                            }
                        }

                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Button, {0.6f, 0.1f, 0.1f, 1.0f});
                        if (ImGui::Button("Remove")) {
                            fx.destruct();
                            if (ui->editingCueIndex == -2 && ui->editingBankIndex == i) {
                                setEditingCueIndex(app, -1);
                            } else if (ui->editingCueIndex == -2 && ui->editingBankIndex > i) {
                                setEditingBankIndex(app, ui->editingBankIndex - 1);
                            }
                            i--;
                            selectedPatch.add<Patch::ProgramDirty>();
                        }
                        ImGui::PopStyleColor();

                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
    });

    // ─────────────── Offline Preview Window ───────────────────────
struct SetupScriptPreset {
    const char* name;
    const char* script;
};

static const SetupScriptPreset kSetupPresets[] = {
    { "2D: Grid", 
      "-- 2D Grid Layout\n"
      "patch:clear_fixtures()\n"
      "local cols = 5\n"
      "local rows = 5\n"
      "local index = 0\n"
      "for r = 0, rows - 1 do\n"
      "    for c = 0, cols - 1 do\n"
      "        local x = c * 40\n"
      "        local y = r * 40\n"
      "        local f = patch:create_line(\"Grid_\"..r..\"_\"..c, x, y, 0, x + 20, y, 0, 8, 4)\n"
      "        f:set_dmx(0, index * 32)\n"
      "        index = index + 1\n"
      "    end\n"
      "end\n" },
    { "2D: Starburst / Radial",
      "-- 2D Starburst Layout\n"
      "patch:clear_fixtures()\n"
      "local count = 12\n"
      "local radius = 80\n"
      "local centerX = 100\n"
      "local centerY = 100\n"
      "for i = 0, count - 1 do\n"
      "    local angle = i * (2 * math.pi / count)\n"
      "    local sx = centerX\n"
      "    local sy = centerY\n"
      "    local ex = centerX + math.cos(angle) * radius\n"
      "    local ey = centerY + math.sin(angle) * radius\n"
      "    local f = patch:create_line(\"Ray_\"..i, sx, sy, 0, ex, ey, 0, 16, 4)\n"
      "    f:set_dmx(0, i * 64)\n"
      "end\n" },
    { "2D: Spiral",
      "-- 2D Spiral Layout\n"
      "patch:clear_fixtures()\n"
      "local count = 15\n"
      "local centerX = 100\n"
      "local centerY = 100\n"
      "for i = 1, count do\n"
      "    local angle = i * 0.8\n"
      "    local radius = i * 8\n"
      "    local sx = centerX + math.cos(angle) * radius\n"
      "    local sy = centerY + math.sin(angle) * radius\n"
      "    local ex = centerX + math.cos(angle + 0.4) * (radius + 6)\n"
      "    local ey = centerY + math.sin(angle + 0.4) * (radius + 6)\n"
      "    local f = patch:create_line(\"Spiral_\"..i, sx, sy, 0, ex, ey, 0, 8, 4)\n"
      "    f:set_dmx(0, (i - 1) * 32)\n"
      "end\n" },
    { "2D: Concentric Squares",
      "-- 2D Concentric Squares\n"
      "patch:clear_fixtures()\n"
      "local centerX = 100\n"
      "local centerY = 100\n"
      "local index = 0\n"
      "for size = 20, 100, 20 do\n"
      "    local half = size / 2\n"
      "    local f1 = patch:create_line(\"Square_Top_\"..size, centerX - half, centerY - half, 0, centerX + half, centerY - half, 0, 8, 4)\n"
      "    f1:set_dmx(0, index * 32)\n"
      "    index = index + 1\n"
      "    local f2 = patch:create_line(\"Square_Bot_\"..size, centerX - half, centerY + half, 0, centerX + half, centerY + half, 0, 8, 4)\n"
      "    f2:set_dmx(0, index * 32)\n"
      "    index = index + 1\n"
      "end\n" },
    { "2D: Cross / X Pattern",
      "-- 2D Cross Pattern\n"
      "patch:clear_fixtures()\n"
      "local centerX = 100\n"
      "local centerY = 100\n"
      "local size = 80\n"
      "local index = 0\n"
      "for i = 1, 4 do\n"
      "    local step = size / 4\n"
      "    local f1 = patch:create_line(\"Diag1_\"..i, centerX - size/2 + (i-1)*step, centerY - size/2 + (i-1)*step, 0, centerX - size/2 + i*step, centerY - size/2 + i*step, 0, 8, 4)\n"
      "    f1:set_dmx(0, index * 32)\n"
      "    index = index + 1\n"
      "    local f2 = patch:create_line(\"Diag2_\"..i, centerX - size/2 + (i-1)*step, centerY + size/2 - (i-1)*step, 0, centerX - size/2 + i*step, centerY + size/2 - i*step, 0, 8, 4)\n"
      "    f2:set_dmx(0, index * 32)\n"
      "    index = index + 1\n"
      "end\n" },
    { "3D: Cube Grid",
      "-- 3D Volumetric Cube Grid\n"
      "patch:clear_fixtures()\n"
      "local index = 0\n"
      "for z = 0, 2 do\n"
      "    local zPos = z * 40\n"
      "    for y = 0, 2 do\n"
      "        local yPos = y * 40\n"
      "        local f = patch:create_line(\"Cube_X_\"..z..\"_\"..y, 0, yPos, zPos, 80, yPos, zPos, 16, 4)\n"
      "        f:set_dmx(0, index * 64)\n"
      "        index = index + 1\n"
      "    end\n"
      "end\n" },
    { "3D: Helix / Cylinder",
      "-- 3D Volumetric Helix\n"
      "patch:clear_fixtures()\n"
      "local steps = 16\n"
      "local radius = 60\n"
      "local centerX = 100\n"
      "local centerY = 100\n"
      "for i = 0, steps - 1 do\n"
      "    local angle = i * (4 * math.pi / steps)\n"
      "    local z = i * 10\n"
      "    local sx = centerX + math.cos(angle) * radius\n"
      "    local sy = centerY + math.sin(angle) * radius\n"
      "    local ex = centerX + math.cos(angle + 0.5) * radius\n"
      "    local ey = centerY + math.sin(angle + 0.5) * radius\n"
      "    local f = patch:create_line(\"Helix_\"..i, sx, sy, z, ex, ey, z + 8, 8, 4)\n"
      "    f:set_dmx(0, i * 32)\n"
      "end\n" },
    { "3D: Pyramid",
      "-- 3D Volumetric Pyramid\n"
      "patch:clear_fixtures()\n"
      "local centerX = 100\n"
      "local centerY = 100\n"
      "local baseSize = 80\n"
      "local apexZ = 120\n"
      "local f1 = patch:create_line(\"Pyr_Base1\", centerX - baseSize/2, centerY - baseSize/2, 0, centerX + baseSize/2, centerY - baseSize/2, 0, 8, 4)\n"
      "f1:set_dmx(0, 0)\n"
      "local f2 = patch:create_line(\"Pyr_Base2\", centerX + baseSize/2, centerY - baseSize/2, 0, centerX + baseSize/2, centerY + baseSize/2, 0, 8, 4)\n"
      "f2:set_dmx(0, 32)\n"
      "local f3 = patch:create_line(\"Pyr_Base3\", centerX + baseSize/2, centerY + baseSize/2, 0, centerX - baseSize/2, centerY + baseSize/2, 0, 8, 4)\n"
      "f3:set_dmx(0, 64)\n"
      "local f4 = patch:create_line(\"Pyr_Base4\", centerX - baseSize/2, centerY + baseSize/2, 0, centerX - baseSize/2, centerY - baseSize/2, 0, 8, 4)\n"
      "f4:set_dmx(0, 96)\n"
      "local e1 = patch:create_line(\"Pyr_Edge1\", centerX - baseSize/2, centerY - baseSize/2, 0, centerX, centerY, apexZ, 16, 4)\n"
      "e1:set_dmx(0, 128)\n"
      "local e2 = patch:create_line(\"Pyr_Edge2\", centerX + baseSize/2, centerY - baseSize/2, 0, centerX, centerY, apexZ, 16, 4)\n"
      "e2:set_dmx(0, 192)\n"
      "local e3 = patch:create_line(\"Pyr_Edge3\", centerX + baseSize/2, centerY + baseSize/2, 0, centerX, centerY, apexZ, 16, 4)\n"
      "e3:set_dmx(0, 256)\n"
      "local e4 = patch:create_line(\"Pyr_Edge4\", centerX - baseSize/2, centerY + baseSize/2, 0, centerX, centerY, apexZ, 16, 4)\n"
      "e4:set_dmx(0, 320)\n" },
    { "3D: Wave Ribbon",
      "-- 3D Volumetric Wave Ribbon\n"
      "patch:clear_fixtures()\n"
      "local count = 12\n"
      "for i = 0, count - 1 do\n"
      "    local x = i * 20\n"
      "    local y = 50 + math.sin(i * 0.5) * 30\n"
      "    local z = math.cos(i * 0.5) * 40\n"
      "    local f = patch:create_line(\"Wave_\"..i, x, y, z, x + 15, y, z + 10, 8, 4)\n"
      "    f:set_dmx(0, i * 32)\n"
      "end\n" },
    { "3D: Sphere Projection",
      "-- 3D Volumetric Sphere Projection\n"
      "patch:clear_fixtures()\n"
      "local count = 12\n"
      "local radius = 70\n"
      "local centerX = 100\n"
      "local centerY = 100\n"
      "local centerZ = 50\n"
      "local index = 0\n"
      "for i = 0, count - 1 do\n"
      "    local theta = i * math.pi * (3.0 - math.sqrt(5.0))\n"
      "    local z = (i / (count - 1)) * 2.0 - 1.0\n"
      "    local r = math.sqrt(1.0 - z * z)\n"
      "    local ex = centerX + math.cos(theta) * r * radius\n"
      "    local ey = centerY + math.sin(theta) * r * radius\n"
      "    local ez = centerZ + z * radius\n"
      "    local f = patch:create_line(\"Sphere_Ray_\"..i, centerX, centerY, centerZ, ex, ey, ez, 8, 4)\n"
      "    f:set_dmx(0, index * 32)\n"
      "    index = index + 1\n"
      "end\n" },
    { "3D: Fractal Tree",
      "-- 3D Fractal Tree Layout\n"
      "patch:clear_fixtures()\n"
      "local count = 0\n"
      "\n"
      "function make_branch(x, y, z, dx, dy, dz, length, depth)\n"
      "    if depth <= 0 or count >= 50 then return end\n"
      "    \n"
      "    local ex = x + dx * length\n"
      "    local ey = y + dy * length\n"
      "    local ez = z + dz * length\n"
      "    \n"
      "    local f = patch:create_line(\"Branch_\"..count, x, y, z, ex, ey, ez, 8, 4)\n"
      "    f:set_dmx(0, count * 32)\n"
      "    count = count + 1\n"
      "    \n"
      "    local scale = 0.7\n"
      "    local angle_step = 2 * math.pi / 3\n"
      "    \n"
      "    local px, py, pz\n"
      "    if math.abs(dx) < 0.9 then\n"
      "        px, py, pz = 1, 0, 0\n"
      "    else\n"
      "        px, py, pz = 0, 1, 0\n"
      "    end\n"
      "    local tx = dy * pz - dz * py\n"
      "    local ty = dz * px - dx * pz\n"
      "    local tz = dx * py - dy * px\n"
      "    local len = math.sqrt(tx*tx + ty*ty + tz*tz)\n"
      "    tx, ty, tz = tx/len, ty/len, tz/len\n"
      "    \n"
      "    local ux = dy * tz - dz * ty\n"
      "    local uy = dz * tx - dx * tz\n"
      "    local uz = dx * ty - dy * tx\n"
      "    \n"
      "    for i = 0, 2 do\n"
      "        local angle = i * angle_step + depth * 0.5\n"
      "        local cx = dx * 0.7 + (math.cos(angle) * tx + math.sin(angle) * ux) * 0.5\n"
      "        local cy = dy * 0.7 + (math.cos(angle) * ty + math.sin(angle) * uy) * 0.5\n"
      "        local cz = dz * 0.7 + (math.cos(angle) * tz + math.sin(angle) * uz) * 0.5\n"
      "        local clen = math.sqrt(cx*cx + cy*cy + cz*cz)\n"
      "        cx, cy, cz = cx/clen, cy/clen, cz/clen\n"
      "        \n"
      "        make_branch(ex, ey, ez, cx, cy, cz, length * scale, depth - 1)\n"
      "    end\n"
      "end\n"
      "\n"
      "make_branch(100, 100, 0, 0, 0, 1, 40, 4)\n" },
    { "3D: Iso Sphere (Icosahedron)",
      "-- 3D Triangular Ico Sphere Outline\n"
      "patch:clear_fixtures()\n"
      "local phi = (1 + math.sqrt(5)) / 2\n"
      "local r = 50\n"
      "local cx, cy, cz = 100, 100, 50\n"
      "\n"
      "local vertices = {\n"
      "    {-1,  phi, 0}, { 1,  phi, 0}, {-1, -phi, 0}, { 1, -phi, 0},\n"
      "    {0, -1,  phi}, {0,  1,  phi}, {0, -1, -phi}, {0,  1, -phi},\n"
      "    { phi, 0, -1}, { phi, 0,  1}, {-phi, 0, -1}, {-phi, 0,  1}\n"
      "}\n"
      "\n"
      "for i = 1, #vertices do\n"
      "    local v = vertices[i]\n"
      "    local len = math.sqrt(v[1]*v[1] + v[2]*v[2] + v[3]*v[3])\n"
      "    v[1] = cx + (v[1] / len) * r\n"
      "    v[2] = cy + (v[2] / len) * r\n"
      "    v[3] = cz + (v[3] / len) * r\n"
      "end\n"
      "\n"
      "local edges = {\n"
      "    {1,2}, {1,6}, {1,8}, {1,11}, {1,12},\n"
      "    {2,6}, {2,8}, {2,9}, {2,10},\n"
      "    {3,4}, {3,5}, {3,7}, {3,11}, {3,12},\n"
      "    {4,5}, {4,7}, {4,9}, {4,10},\n"
      "    {5,6}, {5,10}, {5,12},\n"
      "    {6,10}, {6,12},\n"
      "    {7,8}, {7,9}, {7,11},\n"
      "    {8,9}, {8,11},\n"
      "    {9,10}, {11,12}\n"
      "}\n"
      "\n"
      "for i = 1, #edges do\n"
      "    local e = edges[i]\n"
      "    local v1 = vertices[e[1]]\n"
      "    local v2 = vertices[e[2]]\n"
      "    local f = patch:create_line(\"IsoEdge_\"..i, v1[1], v1[2], v1[3], v2[1], v2[2], v2[3], 8, 4)\n"
      "    f:set_dmx(0, (i - 1) * 32)\n"
      "end\n" },
    { "2D: Hilbert Curve",
      "-- 2D Hilbert Curve L-System\n"
      "patch:clear_fixtures()\n"
      "local angle = 0\n"
      "local x, y = 30, 30\n"
      "local step = 20\n"
      "local index = 0\n"
      "\n"
      "local function forward()\n"
      "    local rad = angle * math.pi / 180\n"
      "    local nx = x + math.cos(rad) * step\n"
      "    local ny = y + math.sin(rad) * step\n"
      "    local f = patch:create_line(\"Hil_\"..index, x, y, 0, nx, ny, 0, 8, 4)\n"
      "    f:set_dmx(0, index * 32)\n"
      "    index = index + 1\n"
      "    x, y = nx, ny\n"
      "end\n"
      "\n"
      "local function turn(a)\n"
      "    angle = (angle + a) % 360\n"
      "end\n"
      "\n"
      "local function A(d)\n"
      "    if d <= 0 then return end\n"
      "    turn(90)\n"
      "    B(d - 1)\n"
      "    forward()\n"
      "    turn(-90)\n"
      "    A(d - 1)\n"
      "    forward()\n"
      "    A(d - 1)\n"
      "    turn(-90)\n"
      "    forward()\n"
      "    B(d - 1)\n"
      "    turn(90)\n"
      "end\n"
      "\n"
      "function B(d)\n"
      "    if d <= 0 then return end\n"
      "    turn(-90)\n"
      "    A(d - 1)\n"
      "    forward()\n"
      "    turn(90)\n"
      "    B(d - 1)\n"
      "    forward()\n"
      "    B(d - 1)\n"
      "    turn(90)\n"
      "    forward()\n"
      "    A(d - 1)\n"
      "    turn(-90)\n"
      "end\n"
      "\n"
      "A(3)\n" },
    { "2D: Spongebob Outline",
      "-- 2D Spongebob Outline Drawing\n"
      "patch:clear_fixtures()\n"
      "local index = 0\n"
      "\n"
      "local function add_seg(name, x1, y1, x2, y2)\n"
      "    local f = patch:create_line(name, x1, y1, 0, x2, y2, 0, 8, 4)\n"
      "    f:set_dmx(0, index * 32)\n"
      "    index = index + 1\n"
      "end\n"
      "\n"
      "add_seg(\"Head_Top\", 40, 40, 120, 40)\n"
      "add_seg(\"Head_Right\", 120, 40, 120, 110)\n"
      "add_seg(\"Head_Left\", 40, 40, 40, 110)\n"
      "add_seg(\"Shirt_Top\", 40, 110, 120, 110)\n"
      "add_seg(\"Pants_Bot\", 40, 125, 120, 125)\n"
      "add_seg(\"Pants_Left\", 40, 110, 40, 125)\n"
      "add_seg(\"Pants_Right\", 120, 110, 120, 125)\n"
      "add_seg(\"Tie_1\", 80, 110, 77, 118)\n"
      "add_seg(\"Tie_2\", 80, 110, 83, 118)\n"
      "add_seg(\"Tie_3\", 77, 118, 80, 123)\n"
      "add_seg(\"Tie_4\", 83, 118, 80, 123)\n"
      "add_seg(\"Belt_1\", 50, 118, 60, 118)\n"
      "add_seg(\"Belt_2\", 70, 118, 75, 118)\n"
      "add_seg(\"Belt_3\", 85, 118, 90, 118)\n"
      "add_seg(\"Belt_4\", 100, 118, 110, 118)\n"
      "local cx1, cy1, cx2, cy2 = 65, 65, 95, 65\n"
      "local r = 10\n"
      "for i = 0, 7 do\n"
      "    local a1 = i * math.pi / 4\n"
      "    local a2 = (i + 1) * math.pi / 4\n"
      "    add_seg(\"Eye1_\"..i, cx1 + math.cos(a1)*r, cy1 + math.sin(a1)*r, cx1 + math.cos(a2)*r, cy1 + math.sin(a2)*r)\n"
      "    add_seg(\"Eye2_\"..i, cx2 + math.cos(a1)*r, cy2 + math.sin(a1)*r, cx2 + math.cos(a2)*r, cy2 + math.sin(a2)*r)\n"
      "end\n"
      "add_seg(\"Nose_1\", 80, 68, 85, 75)\n"
      "add_seg(\"Nose_2\", 85, 75, 80, 77)\n"
      "add_seg(\"Smile_1\", 55, 85, 65, 95)\n"
      "add_seg(\"Smile_2\", 65, 95, 95, 95)\n"
      "add_seg(\"Smile_3\", 95, 95, 105, 85)\n"
      "add_seg(\"Tooth1_L\", 73, 95, 73, 103)\n"
      "add_seg(\"Tooth1_B\", 73, 103, 78, 103)\n"
      "add_seg(\"Tooth1_R\", 78, 103, 78, 95)\n"
      "add_seg(\"Tooth2_L\", 82, 95, 82, 103)\n"
      "add_seg(\"Tooth2_B\", 82, 103, 87, 103)\n"
      "add_seg(\"Tooth2_R\", 87, 103, 87, 95)\n"
      "add_seg(\"Leg_L\", 60, 125, 60, 150)\n"
      "add_seg(\"Leg_R\", 100, 125, 100, 150)\n"
      "add_seg(\"Shoe_L\", 55, 150, 65, 150)\n"
      "add_seg(\"Shoe_R\", 95, 150, 105, 150)\n"
      "add_seg(\"Arm_L\", 40, 85, 20, 100)\n"
      "add_seg(\"Arm_R\", 120, 85, 140, 100)\n" },
    { "2D: Spongebob Wireframe",
      "-- 2D Spongebob Wireframe Face Mesh\n"
      "patch:clear_fixtures()\n"
      "local index = 0\n"
      "\n"
      "local function add_seg(name, x1, y1, x2, y2)\n"
      "    local f = patch:create_line(name, x1, y1, 0, x2, y2, 0, 8, 4)\n"
      "    f:set_dmx(0, index * 32)\n"
      "    index = index + 1\n"
      "end\n"
      "\n"
      "add_seg(\"Mesh_Border_T\", 40, 40, 120, 40)\n"
      "add_seg(\"Mesh_Border_R\", 120, 40, 120, 120)\n"
      "add_seg(\"Mesh_Border_B\", 120, 120, 40, 120)\n"
      "add_seg(\"Mesh_Border_L\", 40, 120, 40, 40)\n"
      "add_seg(\"Mesh_Diag_TL_Center\", 40, 40, 80, 80)\n"
      "add_seg(\"Mesh_Diag_TR_Center\", 120, 40, 80, 80)\n"
      "add_seg(\"Mesh_Diag_BL_Center\", 40, 120, 80, 80)\n"
      "add_seg(\"Mesh_Diag_BR_Center\", 120, 120, 80, 80)\n"
      "add_seg(\"Mesh_TL_EyeL\", 40, 40, 65, 65)\n"
      "add_seg(\"Mesh_TR_EyeR\", 120, 40, 95, 65)\n"
      "add_seg(\"Mesh_BL_MouthL\", 40, 120, 55, 90)\n"
      "add_seg(\"Mesh_BR_MouthR\", 120, 120, 105, 90)\n"
      "add_seg(\"Mesh_Center_EyeL\", 80, 80, 65, 65)\n"
      "add_seg(\"Mesh_Center_EyeR\", 80, 80, 95, 65)\n"
      "add_seg(\"Mesh_Center_MouthL\", 80, 80, 55, 90)\n"
      "add_seg(\"Mesh_Center_MouthR\", 80, 80, 105, 90)\n"
      "add_seg(\"Mesh_EyeL_EyeR\", 65, 65, 95, 65)\n"
      "add_seg(\"Mesh_MouthL_MouthR\", 55, 90, 105, 90)\n"
      "add_seg(\"Mesh_EyeL_MouthL\", 65, 65, 55, 90)\n"
      "add_seg(\"Mesh_EyeR_MouthR\", 95, 65, 105, 90)\n"
      "add_seg(\"Mesh_MouthL_ToothL\", 55, 90, 75, 100)\n"
      "add_seg(\"Mesh_MouthR_ToothR\", 105, 90, 85, 100)\n"
      "add_seg(\"Mesh_ToothL_ToothR\", 75, 100, 85, 100)\n"
      "add_seg(\"Mesh_Center_ToothL\", 80, 80, 75, 100)\n"
      "add_seg(\"Mesh_Center_ToothR\", 80, 80, 85, 100)\n"
      "add_seg(\"Mesh_BL_ToothL\", 40, 120, 75, 100)\n"
      "add_seg(\"Mesh_BR_ToothR\", 120, 120, 85, 100)\n" },
    { "Custom: Hanging Volumetric Branches",
      "-- Custom: Hanging Volumetric Branches\n"
      "patch:clear_fixtures()\n"
      "\n"
      "-- Editable Parameters (in millimeters)\n"
      "local fixtureLength = 1500 -- 150cm\n"
      "local pixelsPerFixture = 36\n"
      "local channelsPerPixel = 4\n"
      "local numCircles = 4\n"
      "local branchesPerCircle = 6\n"
      "\n"
      "local distBaseToMid = 3000  -- 300cm from branch base to middle fixture\n"
      "local distMidToTip = 2000   -- 200cm from middle fixture to tip fixture\n"
      "local heights = { 20, 420, 820, 1420 } -- heights of each floor circle\n"
      "local radii = { 680, 550, 450, 400 }   -- radii of each floor circle\n"
      "local angles = { 30, 22, 15, 5 }         -- outward angle of branch (degrees)\n"
      "local offsets = { 0, 25, 50, 75 }         -- rotational start offset of circle (degrees)\n"
      "\n"
      "local dmxUniverse = 0\n"
      "local dmxAddress = 0\n"
      "\n"
      "local generateHelpers = true -- Set to false to disable helper structures (circles/branches)\n"
      "\n"
      "local function patch_fixture(f)\n"
      "    f:set_dmx(dmxUniverse, dmxAddress)\n"
      "    dmxAddress = dmxAddress + (pixelsPerFixture * channelsPerPixel)\n"
      "    if dmxAddress >= 512 then\n"
      "        dmxUniverse = dmxUniverse + 1\n"
      "        dmxAddress = 0\n"
      "    end\n"
      "end\n"
      "\n"
      "for c = 1, numCircles do\n"
      "    local h = heights[c]\n"
      "    local r = radii[c]\n"
      "    local alpha = angles[c] * math.pi / 180\n"
      "    local offset = offsets[c] * math.pi / 180\n"
      "    \n"
      "    for b = 1, branchesPerCircle do\n"
      "        local theta = (b - 1) * (2 * math.pi / branchesPerCircle) + offset\n"
      "        local dx = math.cos(theta)\n"
      "        local dz = math.sin(theta)\n"
      "        \n"
      "        -- Plant base coordinate\n"
      "        local plantX = r * dx\n"
      "        local plantY = -h\n"
      "        local plantZ = r * dz\n"
      "        \n"
      "        -- Branch direction vector (angled outwards)\n"
      "        local bx = math.sin(alpha) * dx\n"
      "        local by = -math.cos(alpha)\n"
      "        local bz = math.sin(alpha) * dz\n"
      "        \n"
      "        -- Middle attachment point\n"
      "        local midX = plantX + distBaseToMid * bx\n"
      "        local midY = plantY + distBaseToMid * by\n"
      "        local midZ = plantZ + distBaseToMid * bz\n"
      "        \n"
      "        -- Tip attachment point\n"
      "        local tipX = plantX + (distBaseToMid + distMidToTip) * bx\n"
      "        local tipY = plantY + (distBaseToMid + distMidToTip) * by\n"
      "        local tipZ = plantZ + (distBaseToMid + distMidToTip) * bz\n"
      "        \n"
      "        -- Create middle fixture hanging vertically downwards along positive Y\n"
      "        local midName = \"Floor_\"..c..\"_Branch_\"..b..\"_Mid\"\n"
      "        local fMid = patch:create_line(midName, midX, midY, midZ, midX, midY + fixtureLength, midZ, pixelsPerFixture, channelsPerPixel)\n"
      "        patch_fixture(fMid)\n"
      "        \n"
      "        -- Create tip fixture hanging vertically downwards along positive Y\n"
      "        local tipName = \"Floor_\"..c..\"_Branch_\"..b..\"_Tip\"\n"
      "        local fTip = patch:create_line(tipName, tipX, tipY, tipZ, tipX, tipY + fixtureLength, tipZ, pixelsPerFixture, channelsPerPixel)\n"
      "        patch_fixture(fTip)\n"
      "        \n"
      "        if generateHelpers then\n"
      "            -- DRAW DUMMY BRANCH STRUCTURE (Base to Tip)\n"
      "            local branchStructName = \"Floor_\"..c..\"_Struct_Branch_\"..b\n"
      "            local fStruct = patch:create_line(branchStructName, plantX, plantY, plantZ, tipX, tipY, tipZ, 2, 1)\n"
      "            fStruct:set_dmx(98, b)\n"
      "            \n"
      "            -- DRAW DUMMY BASE RING (Base to Next Base forming a hexagon)\n"
      "            local nextB = (b % branchesPerCircle) + 1\n"
      "            local nextTheta = (nextB - 1) * (2 * math.pi / branchesPerCircle) + offset\n"
      "            local nextX = r * math.cos(nextTheta)\n"
      "            local nextY = -h\n"
      "            local nextZ = r * math.sin(nextTheta)\n"
      "            \n"
      "            local ringName = \"Floor_\"..c..\"_Struct_Ring_\"..b\n"
      "            local fRing = patch:create_line(ringName, plantX, plantY, plantZ, nextX, nextY, nextZ, 2, 1)\n"
      "            fRing:set_dmx(99, b)\n"
      "        end\n"
      "    end\n"
      "end\n" }
};
static const int kSetupPresetsCount = sizeof(kSetupPresets) / sizeof(kSetupPresets[0]);

class LuaFixture {
public:
    flecs::entity entity;
    LuaFixture(flecs::entity e) : entity(e) {}

    uint64_t id() const { return entity.id(); }
    std::string name() const {
        if (!entity.is_valid() || !entity.is_alive()) return "";
        const char* n = entity.name();
        return n ? n : "";
    }
    void set_name(const std::string& name) {
        if (!entity.is_valid() || !entity.is_alive()) return;
        entity.set_name(name.c_str());
    }
    void remove() {
        if (!entity.is_valid() || !entity.is_alive()) return;
        flecs::entity patch = Fixture::getPatch(entity);
        entity.destruct();
        if (patch.is_valid()) {
            patch.add<Patch::DmxMapDirty>();
            patch.add<Patch::RenderAreaDirty>();
        }
    }
    std::string get_shape_type() const {
        if (!entity.is_valid() || !entity.is_alive()) return "None";
        flecs::entity shapeType = entity.target<Fixture::WithShape>();
        if (shapeType == entity.world().id<Shape::Line>()) return "Line";
        if (shapeType == entity.world().id<Shape::Circle>()) return "Circle";
        return "None";
    }
    sol::object get_line_properties(sol::this_state s) const {
        sol::state_view lua(s);
        if (!entity.is_valid() || !entity.is_alive()) return sol::lua_nil;
        flecs::entity shapeType = entity.target<Fixture::WithShape>();
        if (shapeType == entity.world().id<Shape::Line>()) {
            if (const auto* l = entity.try_get<Fixture::WithShape, Shape::Line>()) {
                return sol::make_object(lua, std::make_tuple(l->start.x, l->start.y, l->start.z, l->end.x, l->end.y, l->end.z));
            }
        }
        return sol::lua_nil;
    }
    void set_line_properties(float sx, float sy, float sz, float ex, float ey, float ez) {
        if (!entity.is_valid() || !entity.is_alive()) return;
        entity.set<Fixture::WithShape, Shape::Line>({{sx, sy, sz}, {ex, ey, ez}});
        entity.add<Fixture::PixelPositionsDirty>();
        flecs::entity patch = Fixture::getPatch(entity);
        if (patch.is_valid()) {
            patch.add<Patch::RenderAreaDirty>();
        }
    }
    sol::object get_layout(sol::this_state s) const {
        sol::state_view lua(s);
        if (!entity.is_valid() || !entity.is_alive()) return sol::lua_nil;
        if (const auto* l = entity.try_get<Fixture::Layout>()) {
            return sol::make_object(lua, std::make_tuple(l->pixelCount, l->channelsPerPixel));
        }
        return sol::lua_nil;
    }
    void set_layout(int pixelCount, int channels) {
        if (!entity.is_valid() || !entity.is_alive()) return;
        Fixture::Layout layout{pixelCount, channels};
        entity.set<Fixture::Layout>(layout);
    }
    sol::object get_dmx(sol::this_state s) const {
        sol::state_view lua(s);
        if (!entity.is_valid() || !entity.is_alive()) return sol::lua_nil;
        if (const auto* dmx = entity.try_get<Fixture::DmxAddress>()) {
            return sol::make_object(lua, std::make_tuple(dmx->universe, dmx->address));
        }
        return sol::lua_nil;
    }
    void set_dmx(int universe, int address) {
        if (!entity.is_valid() || !entity.is_alive()) return;
        Fixture::setDmxProperties(entity, universe, address);
    }
};

class LuaPatch {
public:
    flecs::entity patch;
    LuaPatch(flecs::entity p) : patch(p) {}

    void clear_fixtures() {
        if (!patch.is_valid() || !patch.is_alive()) return;
        auto fixtureFolder = patch.target<Patch::FixtureFolder>();
        if (!fixtureFolder.is_valid()) return;

        std::vector<flecs::entity> toDelete;
        fixtureFolder.children([&](flecs::entity child){
            if (child.has<Fixture::Is>()) {
                toDelete.push_back(child);
            }
        });

        for (auto f : toDelete) {
            if (f.name()) {
                std::string tempName = std::string(f.name()) + "_dying_" + std::to_string(f.id());
                f.set_name(tempName.c_str());
            }
            f.destruct();
        }
        patch.add<Patch::DmxMapDirty>();
        patch.add<Patch::RenderAreaDirty>();
    }

    LuaFixture create_line(const std::string& name, float sx, float sy, float sz, float ex, float ey, float ez, int numPixels, int channels) {
        if (!patch.is_valid() || !patch.is_alive()) return LuaFixture(flecs::entity::null());
        flecs::entity f = Fixture::createLine(patch, {sx, sy, sz}, {ex, ey, ez}, numPixels, channels);
        if (f.is_valid() && !name.empty()) {
            f.set_name(name.c_str());
        }
        return LuaFixture(f);
    }

    std::vector<LuaFixture> get_fixtures() {
        std::vector<LuaFixture> res;
        if (!patch.is_valid() || !patch.is_alive()) return res;
        Fixture::iterateWithDmx(patch, [&](flecs::entity f, const Fixture::Layout&, const Fixture::DmxAddress&){
            res.push_back(LuaFixture(f));
        });
        return res;
    }
};

    w.system<>("WindowFixtureSetupScript").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app          = App::get(it.world());
        auto* ui          = &app.get_mut<App::UIConfig>();
        if (!ui->showFixtureSetupScriptWindow) return;
        auto selectedPatch = Patch::getSelected(app);

        if(!setupEditorInitialized){
            setupScriptEditor = std::make_unique<TextEditor>();
            setupScriptEditor->SetLanguage(TextEditor::Language::Lua());
            setupEditorInitialized = true;
        }

        if(ImGui::Begin("Fixture Setup Script", &ui->showFixtureSetupScriptWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch selected."); ImGui::End(); return; }

            auto* setupScript = selectedPatch.try_get_mut<Patch::FixtureSetupScript>();
            if(!setupScript){ ImGui::End(); return; }

            static flecs::id_t lastPatchId = 0;
            static std::string lastSetupEditorText = "";

            bool setupTargetChanged = (selectedPatch.id() != lastPatchId);

            if (setupTargetChanged) {
                lastPatchId = selectedPatch.id();
                setupScriptEditor->SetText(setupScript->source);
                lastSetupEditorText = setupScript->source;
                setupScriptHasUncompiledChanges = false;
            }

            // ── Auto-compile debouncing logic ──
            static double lastSetupChangeTime = 0.0;
            std::string currentSetupText = setupScriptEditor->GetText();
            if (!setupTargetChanged && currentSetupText != lastSetupEditorText) {
                lastSetupEditorText = currentSetupText;
                setupScriptHasUncompiledChanges = true;
                lastSetupChangeTime = ImGui::GetTime();
            }

            if (setupScriptHasUncompiledChanges && (ImGui::GetTime() - lastSetupChangeTime > 0.3)) {
                std::string newSource = setupScriptEditor->GetText();
                setupScript->source = newSource;
                
                sol::state valLua;
                valLua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
                sol::load_result loadRes = valLua.load(newSource);
                if (!loadRes.valid()) {
                    sol::error err = loadRes;
                    setupScript->compilerLog = err.what();
                } else {
                    setupScript->compilerLog = "Syntax OK!";
                }
                setupScriptHasUncompiledChanges = false;
            }

            // Buttons row
            if (ImGui::Button("Run")) {
                sol::state runLua;
                runLua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
                
                runLua.new_usertype<LuaFixture>("Fixture",
                    "id", &LuaFixture::id,
                    "name", &LuaFixture::name,
                    "set_name", &LuaFixture::set_name,
                    "remove", &LuaFixture::remove,
                    "get_shape_type", &LuaFixture::get_shape_type,
                    "get_line_properties", &LuaFixture::get_line_properties,
                    "set_line_properties", &LuaFixture::set_line_properties,
                    "get_layout", &LuaFixture::get_layout,
                    "set_layout", &LuaFixture::set_layout,
                    "get_dmx", &LuaFixture::get_dmx,
                    "set_dmx", &LuaFixture::set_dmx
                );

                runLua.new_usertype<LuaPatch>("Patch",
                    "clear_fixtures", &LuaPatch::clear_fixtures,
                    "create_line", &LuaPatch::create_line,
                    "get_fixtures", &LuaPatch::get_fixtures
                );

                runLua["patch"] = LuaPatch(selectedPatch);
                
                std::string sourceCode = setupScriptEditor->GetText();
                setupScript->source = sourceCode;

                sol::protected_function_result result = runLua.safe_script(sourceCode, sol::script_pass_on_error);
                if (!result.valid()) {
                    sol::error err = result;
                    setupScript->compilerLog = "Runtime Error: " + std::string(err.what());
                } else {
                    setupScript->compilerLog = "Run successful!";
                }
                
                selectedPatch.add<Patch::DmxMapDirty>();
                selectedPatch.add<Patch::RenderAreaDirty>();
                selectedPatch.add<Patch::ProgramDirty>();
            }
            ImGui::SameLine();
            if (ImGui::Button("Help")) {
                showSetupHelpWindow = !showSetupHelpWindow;
            }
            ImGui::SameLine();
            static int setupPresetIdx = -1;
            auto setupPresetGetter = [](void*, int i, const char** out) -> bool { *out = kSetupPresets[i].name; return true; };
            ImGui::SetNextItemWidth(180);
            if (ImGui::Combo("Presets", &setupPresetIdx, setupPresetGetter, nullptr, kSetupPresetsCount) && setupPresetIdx >= 0) {
                std::string presetCode = kSetupPresets[setupPresetIdx].script;
                setupScriptEditor->SetText(presetCode);
                setupScript->source = presetCode;
                setupScript->compilerLog = "Syntax OK!";
                setupScriptHasUncompiledChanges = false;
            }

            ImGui::Separator();

            const float kLogHeight = 100.f;
            const float kSepHeight = ImGui::GetStyle().ItemSpacing.y + 1.f;
            float editorHeight = ImGui::GetContentRegionAvail().y - kLogHeight - kSepHeight * 3.f;
            if(editorHeight < 80.f) editorHeight = 80.f;

            setupScriptEditor->Render("SetupScriptEd", ImVec2(0, editorHeight));

            ImGui::Separator();
            if (setupScriptHasUncompiledChanges) {
                ImGui::TextColored({1.0f, 0.5f, 0.0f, 1.0f}, "* Unsaved Changes (validating...)");
            } else {
                ImGui::TextColored({0.2f, 1.0f, 0.2f, 1.0f}, "Saved & Checked");
            }

            ImGui::Separator();
            ImGui::Text("Status / Error Log:");
            ImGui::BeginChild("##SetupLog", ImVec2(0, kLogHeight), true);
            std::string displayLog = setupScript->compilerLog;

            bool ok = (displayLog == "Syntax OK!" || displayLog == "Run successful!");
            if(ok)
                ImGui::TextColored({0.2f,1,0.2f,1}, "%s", displayLog.c_str());
            else if(!displayLog.empty())
                ImGui::TextColored({1,0.3f,0.3f,1}, "%s", displayLog.c_str());
            else
                ImGui::TextDisabled("No log yet.");
            ImGui::EndChild();

            if (showSetupHelpWindow) {
                ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Fixture Setup Script Documentation", &showSetupHelpWindow)) {
                    ImGui::TextWrapped("This documentation reference guide covers the Lua API bindings available for programmatically configuring fixtures inside the current Patch.");
                    ImGui::Separator();

                    if (ImGui::CollapsingHeader("1. Patch Interface")) {
                        ImGui::Text("Exposed as global variable 'patch':");
                        ImGui::BulletText("patch:clear_fixtures() - Deletes all fixtures in the current patch.");
                        ImGui::BulletText("patch:create_line(name, startX, startY, startZ, endX, endY, endZ, numPixels, channels) - Creates a new Line Fixture and returns a Fixture object wrapper.");
                        ImGui::BulletText("patch:get_fixtures() - Returns a standard Lua array (table) of all Fixture objects currently in this patch.");
                    }

                    if (ImGui::CollapsingHeader("2. Fixture Interface")) {
                        ImGui::Text("Exposed on individual Fixture object wrappers:");
                        ImGui::BulletText("f:id() - Returns the unique numerical Flecs entity ID for the fixture.");
                        ImGui::BulletText("f:name() - Returns the fixture's name string.");
                        ImGui::BulletText("f:set_name(name) - Sets a new name string for the fixture.");
                        ImGui::BulletText("f:remove() - Deletes this fixture from the patch.");
                        ImGui::BulletText("f:get_shape_type() - Returns shape type string ('Line', 'Circle', or 'None').");
                        ImGui::BulletText("f:get_line_properties() - Returns 6 values: startX, startY, startZ, endX, endY, endZ (multiple returns).");
                        ImGui::BulletText("f:set_line_properties(startX, startY, startZ, endX, endY, endZ) - Configures the Line shape properties.");
                        ImGui::BulletText("f:get_layout() - Returns 2 values: pixelCount, channelsPerPixel.");
                        ImGui::BulletText("f:set_layout(pixelCount, channelsPerPixel) - Sets layout (e.g. RGB is 3 channels, RGBW is 4 channels).");
                        ImGui::BulletText("f:get_dmx() - Returns 2 values: universe, startAddress.");
                        ImGui::BulletText("f:set_dmx(universe, startAddress) - Sets the DMX patch address.");
                    }

                    if (ImGui::CollapsingHeader("3. Volumetric Setup Example")) {
                        ImGui::TextWrapped("The example script below clears the patch and lays out a grid of Line fixtures:");
                        ImGui::Separator();
                        ImGui::TextDisabled(
                            "-- Clear and create a 3x3 array of lines\n"
                            "patch:clear_fixtures()\n\n"
                            "local count = 1\n"
                            "for row = 0, 2 do\n"
                            "    for col = 0, 2 do\n"
                            "        local x = col * 40\n"
                            "        local y = row * 40\n"
                            "        local name = \"Grid_\" .. row .. \"_\" .. col\n"
                            "        local f = patch:create_line(name, x, y, 0, x + 30, y, 0, 8, 4)\n"
                            "        -- Sequentially patch DMX universes\n"
                            "        f:set_dmx(0, (count - 1) * 32)\n"
                            "        count = count + 1\n"
                            "    end\n"
                            "end"
                        );
                    }
                }
                    }
        }
        ImGui::End();
    });

    w.system<>("WindowPalettes").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app          = App::get(it.world());
        auto* ui          = &app.get_mut<App::UIConfig>();
        if (!ui->showPalettesWindow) return;
        auto selectedPatch = Patch::getSelected(app);

        if(ImGui::Begin("Palettes Editor", &ui->showPalettesWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            flecs::entity palFolder = selectedPatch.target<Generative::PaletteFolder>();
            if(!palFolder.is_valid()){ ImGui::TextDisabled("No palette folder."); ImGui::End(); return; }

            if(ImGui::BeginTable("PalTable", 2, ImGuiTableFlags_Resizable)){
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SeparatorText("Palettes");

                if (ImGui::Button("+ Palette")) {
                    std::vector<Generative::ColorStop> stops = {
                        { {1.0f, 0.0f, 0.0f, 1.0f}, 0.0f, 0.5f, {0.0f, 0.0f} },
                        { {0.0f, 0.0f, 1.0f, 1.0f}, 0.5f, 0.5f, {0.0f, 0.0f} },
                        { {1.0f, 0.0f, 0.0f, 1.0f}, 1.0f, 0.5f, {0.0f, 0.0f} }
                    };
                    static int customCount = 1;
                    std::string newName = "Palette " + std::to_string(customCount++);
                    auto p = it.world().entity().child_of(palFolder)
                        .add<Generative::Palette::Is>()
                        .set<Generative::Palette::Stops>({stops})
                        .set<Generative::Palette::IsModeB>({false});
                    p.set_name(newName.c_str());
                    
                    it.world().entity("PixelMapperApp").get_mut<App::UIConfig>().editingCueIndex = -3; // select flag
                    currentPatchId = p.id();
                    
                    auto prog = std::atomic_load(&App::currentPatchProgram);
                    if (prog) {
                        std::lock_guard<std::mutex> lock(prog->generativeMutex);
                        Generative::CompiledPalette cp;
                        cp.name = newName;
                        cp.stops = stops;
                        cp.isModeB = false;
                        prog->palettePool.push_back(cp);
                    }
                }
                
                ImGui::SameLine();
                
                static flecs::id_t selectedPalId = 0;
                
                bool hasSel = false;
                flecs::entity selectedPal = it.world().entity(selectedPalId);
                if (selectedPal.is_valid() && selectedPal.is_alive() && selectedPal.parent() == palFolder) {
                    hasSel = true;
                }
 
                if (!hasSel) ImGui::BeginDisabled();
                if (ImGui::Button("Remove")) {
                    std::string nameToRemove = selectedPal.name().c_str();
                    selectedPal.destruct();
                    selectedPalId = 0;
                    hasSel = false;
                    
                    auto prog = std::atomic_load(&App::currentPatchProgram);
                    if (prog) {
                        std::lock_guard<std::mutex> lock(prog->generativeMutex);
                        prog->palettePool.erase(
                            std::remove_if(prog->palettePool.begin(), prog->palettePool.end(),
                                [&](const Generative::CompiledPalette& cp) { return cp.name == nameToRemove; }),
                            prog->palettePool.end()
                        );
                    }
                }
                if (!hasSel) ImGui::EndDisabled();
 
                ImGui::Separator();
 
                if (ImGui::BeginListBox("##PalsList", ImGui::GetContentRegionAvail())) {
                    palFolder.children([&](flecs::entity child) {
                        if (child.has<Generative::Palette::Is>()) {
                            bool isSel = (child.id() == selectedPalId);
                            if (ImGui::Selectable(child.name().c_str(), isSel)) {
                                selectedPalId = child.id();
                            }
                        }
                    });
                    ImGui::EndListBox();
                }
 
                ImGui::TableSetColumnIndex(1);
                selectedPal = it.world().entity(selectedPalId);
                if (selectedPal.is_valid() && selectedPal.is_alive() && selectedPal.parent() == palFolder) {
                    ImGui::Text("Editing Palette: %s", selectedPal.name().c_str());
                    ImGui::Separator();
 
                    char nameBuf[128] = {};
                    std::strncpy(nameBuf, selectedPal.name().c_str(), sizeof(nameBuf) - 1);
                    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                        std::string oldName = selectedPal.name().c_str();
                        selectedPal.set_name(nameBuf);
                        auto prog = std::atomic_load(&App::currentPatchProgram);
                        if (prog) {
                            std::lock_guard<std::mutex> lock(prog->generativeMutex);
                            for (auto& cp : prog->palettePool) {
                                if (cp.name == oldName) {
                                    cp.name = nameBuf;
                                    break;
                                }
                            }
                        }
                    }
 
                    auto* modeBComp = selectedPal.try_get_mut<Generative::Palette::IsModeB>();
                    if (modeBComp) {
                        if (ImGui::Checkbox("Discrete Crossfade (Mode B Index-based)", &modeBComp->value)) {
                            auto prog = std::atomic_load(&App::currentPatchProgram);
                            if (prog) {
                                std::lock_guard<std::mutex> lock(prog->generativeMutex);
                                for (auto& cp : prog->palettePool) {
                                    if (cp.name == selectedPal.name().c_str()) {
                                        cp.isModeB = modeBComp->value;
                                        break;
                                    }
                                }
                            }
                        }
                    }
 
                    auto* stopsComp = selectedPal.try_get_mut<Generative::Palette::Stops>();
                    if (stopsComp) {
                        std::vector<Generative::ColorStop>& stops = stopsComp->value;
 
                        // ── Gradient visual bar rendering ──
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        ImVec2 barPos = ImGui::GetCursorScreenPos();
                        float barWidth = ImGui::GetContentRegionAvail().x - 10.0f;
                        float barHeight = 25.0f;
                        ImGui::InvisibleButton("##gradient_bar", ImVec2(barWidth, barHeight));
 
                        // Render gradient bar background
                        if (stops.size() >= 2) {
                            for (float x = 0; x < barWidth; x += 2.0f) {
                                float t = x / barWidth;
                                
                                // evaluate color at t
                                glm::vec4 col(0.0f);
                                for (size_t i = 0; i < stops.size() - 1; i++) {
                                    float p0 = stops[i].position;
                                    float p1 = stops[i+1].position;
                                    if (t >= p0 && t <= p1) {
                                        float blend = (t - p0) / std::max(0.0001f, p1 - p0);
                                        float smoothness = glm::mix(stops[i].smoothness, stops[i+1].smoothness, blend);
                                        float width = smoothness;
                                        float f;
                                        if (width > 0.001f) {
                                            float edge0 = 0.5f - width * 0.5f;
                                            float val = glm::clamp((blend - edge0) / width, 0.0f, 1.0f);
                                            f = val * val * (3.0f - 2.0f * val);
                                        } else {
                                            f = (blend < 0.5f) ? 0.0f : 1.0f;
                                        }
                                        col = glm::mix(stops[i].color, stops[i+1].color, f);
                                        break;
                                    }
                                }
                                ImU32 imCol = IM_COL32((int)(col.r * 255.0f), (int)(col.g * 255.0f), (int)(col.b * 255.0f), 255);
                                drawList->AddRectFilled(ImVec2(barPos.x + x, barPos.y), ImVec2(barPos.x + x + 2.0f, barPos.y + barHeight), imCol);
                            }
                        } else {
                            drawList->AddRectFilled(barPos, ImVec2(barPos.x + barWidth, barPos.y + barHeight), IM_COL32(100, 100, 100, 255));
                        }
                        
                        // draw black border
                        drawList->AddRect(barPos, ImVec2(barPos.x + barWidth, barPos.y + barHeight), IM_COL32(255, 255, 255, 100));
 
                        // Edit selected stop
                        static int selectedStopIdx = 0;
                        if (selectedStopIdx >= (int)stops.size()) selectedStopIdx = 0;
 
                        // ── Draggable Stop Markers (Single Unified Hitbox) ──
                        ImGui::Spacing();
                        ImVec2 markerAreaPos = ImGui::GetCursorScreenPos();
                        float markerAreaHeight = 16.0f;
                        
                        ImGui::InvisibleButton("##stops_marker_area_unified", ImVec2(barWidth, markerAreaHeight));
                        
                        bool stopsChanged = false;
                        bool areaActive = ImGui::IsItemActive();
                        bool areaHovered = ImGui::IsItemHovered();
                        
                        if (areaHovered && ImGui::IsMouseClicked(0)) {
                            float mouseX = ImGui::GetIO().MousePos.x;
                            float relativeX = (mouseX - barPos.x) / barWidth;
                            float minDist = 0.05f; // 5% tolerance threshold
                            int bestIdx = -1;
                            for (size_t i = 0; i < stops.size(); ++i) {
                                float dist = std::abs(stops[i].position - relativeX);
                                if (dist < minDist) {
                                    minDist = dist;
                                    bestIdx = (int)i;
                                }
                            }
                            if (bestIdx != -1) {
                                selectedStopIdx = bestIdx;
                            }
                        }
                        
                        if (areaActive && selectedStopIdx >= 0 && selectedStopIdx < (int)stops.size()) {
                            if (selectedStopIdx > 0 && selectedStopIdx < (int)stops.size() - 1) {
                                float mouseX = ImGui::GetIO().MousePos.x;
                                float relativeX = (mouseX - barPos.x) / barWidth;
                                float clampedX = glm::clamp(relativeX, 0.001f, 0.999f);
                                if (stops[selectedStopIdx].position != clampedX) {
                                    stops[selectedStopIdx].position = clampedX;
                                    stopsChanged = true;
                                }
                            }
                        }
                        
                        // Draw stops
                        for (size_t i = 0; i < stops.size(); ++i) {
                            float stopX = barPos.x + stops[i].position * barWidth;
                            float stopY = markerAreaPos.y + markerAreaHeight * 0.5f;
                            
                            float mouseX = ImGui::GetIO().MousePos.x;
                            float mouseY = ImGui::GetIO().MousePos.y;
                            bool isHovered = false;
                            if (areaHovered) {
                                float dist = std::abs((mouseX - barPos.x) / barWidth - stops[i].position);
                                if (dist < 0.025f && mouseY >= markerAreaPos.y && mouseY <= markerAreaPos.y + markerAreaHeight) {
                                    isHovered = true;
                                }
                            }
                            
                            ImU32 circleColor = IM_COL32(200, 200, 200, 255);
                            if ((int)i == selectedStopIdx) {
                                circleColor = IM_COL32(255, 100, 100, 255);
                            } else if (isHovered) {
                                circleColor = IM_COL32(255, 255, 255, 255);
                            }
                            
                            drawList->AddCircleFilled(ImVec2(stopX, stopY), 5.0f, IM_COL32(0, 0, 0, 255), 12);
                            drawList->AddCircleFilled(ImVec2(stopX, stopY), 4.0f, circleColor, 12);
                        }
 
                        if (stopsChanged) {
                            Generative::ColorStop selectedStop = stops[selectedStopIdx];
                            std::sort(stops.begin(), stops.end(), [](const Generative::ColorStop& a, const Generative::ColorStop& b) {
                                return a.position < b.position;
                            });
                            for (size_t i = 0; i < stops.size(); ++i) {
                                if (stops[i].position == selectedStop.position && stops[i].color == selectedStop.color && stops[i].smoothness == selectedStop.smoothness) {
                                    selectedStopIdx = (int)i;
                                    break;
                                }
                            }
                            // Enforce endpoints
                            if (stops.size() >= 2) {
                                stops[0].position = 0.0f;
                                stops.back().position = 1.0f;
                                stops.back().color = stops[0].color;
                            }
                        }
 
                        ImGui::Spacing();
                        ImGui::SeparatorText("Color Stops");
 
                        if (ImGui::Button("+ Stop") && stops.size() < 16) {
                            Generative::ColorStop newStop;
                            newStop.color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                            newStop.position = 0.5f;
                            newStop.smoothness = 0.5f;
                            stops.push_back(newStop);
                            
                            std::sort(stops.begin(), stops.end(), [](const Generative::ColorStop& a, const Generative::ColorStop& b) {
                                return a.position < b.position;
                            });
                            selectedStopIdx = (int)stops.size() / 2;
                            stopsChanged = true;
                        }
                        
                        ImGui::SameLine();
 
                        if (stops.size() > 2) {
                            if (ImGui::Button("- Stop")) {
                                stops.erase(stops.begin() + selectedStopIdx);
                                if (selectedStopIdx >= (int)stops.size()) selectedStopIdx = (int)stops.size() - 1;
                                stopsChanged = true;
                            }
                        }
 
                        // Display list of stops in columns/combo
                        std::vector<std::string> stopLabels;
                        for (size_t i = 0; i < stops.size(); ++i) {
                            stopLabels.push_back("Stop " + std::to_string(i) + " (" + std::to_string((int)(stops[i].position * 100.0f)) + "%)");
                        }
                        std::vector<const char*> stopLabelPtrs;
                        for (const auto& l : stopLabels) stopLabelPtrs.push_back(l.c_str());
 
                        ImGui::Combo("Select Node", &selectedStopIdx, stopLabelPtrs.data(), (int)stops.size());
 
                        ImGui::Spacing();
                        Generative::ColorStop& s = stops[selectedStopIdx];
 
                        // 2D Color Space Picker
                        glm::vec3 currentHSV = rgb2hsv(glm::vec3(s.color.r, s.color.g, s.color.b));
                        glm::vec3 currentHSL = rgb2hsl(glm::vec3(s.color.r, s.color.g, s.color.b));
                        static float hue = 0.0f;
                        static int lastStopIdx = -1;
                        static flecs::id_t lastPalId = 0;
                        if (selectedStopIdx != lastStopIdx || selectedPal.id() != lastPalId) {
                            hue = currentHSV.x;
                            lastStopIdx = selectedStopIdx;
                            lastPalId = selectedPal.id();
                        }

                        // Hue slider
                        if (ImGui::SliderFloat("Hue", &hue, 0.0f, 1.0f, "%.3f")) {
                            glm::vec3 rgb = hsv2rgb(hue, currentHSV.y, currentHSV.z);
                            s.color = glm::vec4(rgb.r, rgb.g, rgb.b, s.color.a);
                            stopsChanged = true;
                        }

                        // Alpha slider
                        float alpha = s.color.a;
                        if (ImGui::SliderFloat("Alpha (Opacity)", &alpha, 0.0f, 1.0f, "%.2f")) {
                            s.color.a = alpha;
                            stopsChanged = true;
                        }

                        ImGui::Spacing();

                        static GLuint hsvTexId = 0;
                        int texW = 64, texH = 64;

                        // Regenerate HSV texture
                        std::vector<uint8_t> texData(texW * texH * 4);
                        for (int y = 0; y < texH; ++y) {
                            for (int x = 0; x < texW; ++x) {
                                float sat = (float)x / (float)(texW - 1);
                                float val = 1.0f - (float)y / (float)(texH - 1);
                                glm::vec3 rgb = hsv2rgb(hue, sat, val);
                                int idx = (y * texW + x) * 4;
                                texData[idx + 0] = (uint8_t)(rgb.r * 255.f);
                                texData[idx + 1] = (uint8_t)(rgb.g * 255.f);
                                texData[idx + 2] = (uint8_t)(rgb.b * 255.f);
                                texData[idx + 3] = 255;
                            }
                        }
                        if (hsvTexId == 0) glGenTextures(1, &hsvTexId);
                        glBindTexture(GL_TEXTURE_2D, hsvTexId);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texW, texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData.data());
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glBindTexture(GL_TEXTURE_2D, 0);

                        ImVec2 pickerSize(150, 150);
                        ImVec2 screenPos = ImGui::GetCursorScreenPos();
                        ImGui::Image((ImTextureID)(intptr_t)hsvTexId, pickerSize);
                        
                        ImGui::SetCursorScreenPos(screenPos);
                        ImGui::InvisibleButton("##hsv_picker_btn", pickerSize);
                        if (ImGui::IsItemActive() || (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))) {
                            ImVec2 mousePos = ImGui::GetMousePos();
                            float s_val = (mousePos.x - screenPos.x) / pickerSize.x;
                            float v_val = 1.0f - (mousePos.y - screenPos.y) / pickerSize.y;
                            s_val = std::clamp(s_val, 0.0f, 1.0f);
                            v_val = std::clamp(v_val, 0.0f, 1.0f);
                            glm::vec3 newRgb = hsv2rgb(hue, s_val, v_val);
                            s.color = glm::vec4(newRgb.r, newRgb.g, newRgb.b, s.color.a);
                            stopsChanged = true;
                        }

                        float markerX = screenPos.x + currentHSV.y * pickerSize.x;
                        float markerY = screenPos.y + (1.0f - currentHSV.z) * pickerSize.y;
                        drawList->AddCircle(ImVec2(markerX, markerY), 6.0f, IM_COL32(0, 0, 0, 255), 12, 2.0f);
                        drawList->AddCircle(ImVec2(markerX, markerY), 5.0f, IM_COL32(255, 255, 255, 255), 12, 1.0f);
 
                        // Position (locked to 0.0 for first stop and 1.0 for last stop)
                        if (selectedStopIdx == 0) {
                            s.position = 0.0f;
                            ImGui::Text("Position: 0%% (Locked)");
                        } else if (selectedStopIdx == (int)stops.size() - 1) {
                            s.position = 1.0f;
                            ImGui::Text("Position: 100%% (Locked)");
                        } else {
                            float minPos = stops[selectedStopIdx - 1].position + 0.01f;
                            float maxPos = stops[selectedStopIdx + 1].position - 0.01f;
                            if (ImGui::SliderFloat("Position", &s.position, minPos, maxPos, "%.2f")) {
                                stopsChanged = true;
                            }
                        }
 
                        // Smoothness
                        if (ImGui::SliderFloat("Smoothness", &s.smoothness, 0.0f, 1.0f, "%.2f")) {
                            stopsChanged = true;
                        }
 
                        // Wrap-Anchor Rule: Copy color of stop 0 to final stop to guarantee seamless looping
                        if (stops.size() >= 2) {
                            stops[0].position = 0.0f;
                            stops.back().position = 1.0f;
                            if (stops.back().color != stops[0].color) {
                                stops.back().color = stops[0].color;
                                stopsChanged = true;
                            }
                        }

                        if (stopsChanged) {
                            auto prog = std::atomic_load(&App::currentPatchProgram);
                            if (prog) {
                                std::lock_guard<std::mutex> lock(prog->generativeMutex);
                                for (auto& cp : prog->palettePool) {
                                    if (cp.name == selectedPal.name().c_str()) {
                                        cp.stops = stops;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    ImGui::TextDisabled("Select a palette from the list.");
                }

                ImGui::EndTable();
            }
        }
        ImGui::End();
    });

    w.system<>("WindowGenerativeDashboard").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app          = App::get(it.world());
        auto* ui          = &app.get_mut<App::UIConfig>();

        // Sync Dashboard and Telemetry visibility
        static bool lastDashboardState = false;
        static bool lastTelemetryState = false;
        if (ui->showGenerativeDashboardWindow != lastDashboardState) {
            ui->showGenerativeTelemetryWindow = ui->showGenerativeDashboardWindow;
            lastDashboardState = ui->showGenerativeDashboardWindow;
            lastTelemetryState = ui->showGenerativeDashboardWindow;
        } else if (ui->showGenerativeTelemetryWindow != lastTelemetryState) {
            ui->showGenerativeDashboardWindow = ui->showGenerativeTelemetryWindow;
            lastDashboardState = ui->showGenerativeTelemetryWindow;
            lastTelemetryState = ui->showGenerativeTelemetryWindow;
        }

        if (!ui->showGenerativeDashboardWindow) return;
        auto selectedPatch = Patch::getSelected(app);

        if(ImGui::Begin("Generative Dashboard", &ui->showGenerativeDashboardWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            auto* settings = selectedPatch.try_get_mut<Generative::Settings>();
            if (!settings) { ImGui::TextDisabled("No generative settings component on patch."); ImGui::End(); return; }

            auto prog = std::atomic_load(&App::currentPatchProgram);
            bool isRunning = prog && prog->generativeSettings.masterEnabled && prog->generativeRuntime;

            // ── Master Controls ──
            bool settingsChanged = false;
            bool enabled = settings->masterEnabled;
            if (ImGui::Checkbox("Master Enable Generative Engine", &enabled)) {
                settings->masterEnabled = enabled;
                settingsChanged = true;
            }

            ImGui::SameLine(0, 30.0f);
            if (isRunning) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "[ACTIVE]");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "[PAUSED/DISABLED]");
            }

            ImGui::Separator();
            ImGui::Spacing();

            // Load preset/shader lists for indexing names and manual overrides
            flecs::entity palFolder = selectedPatch.target<Generative::PaletteFolder>();
            std::vector<std::string> paletteNames;
            if (palFolder.is_valid()) {
                palFolder.children([&](flecs::entity child) {
                    if (child.has<Generative::Palette::Is>()) {
                        paletteNames.push_back(child.name().c_str());
                    }
                });
            }

            flecs::entity motiveFolder = selectedPatch.target<Generative::MotiveFolder>();
            std::vector<std::string> motiveNames;
            if (motiveFolder.is_valid()) {
                motiveFolder.children([&](flecs::entity child) {
                    if (child.has<Generative::Motive::Is>()) {
                        motiveNames.push_back(child.name().c_str());
                    }
                });
            }

            flecs::entity cueFolder = selectedPatch.target<CueList::CueFolder>();
            std::vector<std::string> shaderNames;
            if (cueFolder.is_valid()) {
                struct CueEntry {
                    flecs::entity entity;
                    int order = 0;
                };
                std::vector<CueEntry> cues;
                cueFolder.children([&](flecs::entity child) {
                    if (child.has<CueList::Cue::Is>()) {
                        CueEntry entry;
                        entry.entity = child;
                        if (const auto* ord = child.try_get<CueList::Cue::IndexOrder>()) entry.order = ord->value;
                        cues.push_back(entry);
                    }
                });
                std::sort(cues.begin(), cues.end(), [](const CueEntry& a, const CueEntry& b) {
                    return a.order < b.order;
                });
                for (const auto& c : cues) {
                    shaderNames.push_back(c.entity.name().c_str());
                }
            }

            auto getPaletteName = [&](int idx) -> std::string {
                if (idx >= 0 && idx < (int)paletteNames.size()) return paletteNames[idx];
                return "None";
            };
            auto getMotiveName = [&](int idx) -> std::string {
                if (idx >= 0 && idx < (int)motiveNames.size()) return motiveNames[idx];
                return "None";
            };
            auto getShaderName = [&](int idx) -> std::string {
                if (idx >= 0 && idx < (int)shaderNames.size()) return shaderNames[idx];
                return "None";
            };

            // --- Timelines & Queues ---
            ImGui::SeparatorText("Timelines & Queues");

            if (prog && prog->generativeRuntime) {
                auto rt = prog->generativeRuntime;

                // Palette Queue
                {
                    ImGui::PushID("PaletteQueueControl");
                    bool pActive = settings->paletteEnabled;
                    if (ImGui::Checkbox("##playPalette", &pActive)) {
                        settings->paletteEnabled = pActive;
                        if (pActive) {
                            settings->manualPaletteOverride = false;
                        } else {
                            settings->manualPaletteOverride = true;
                            if (settings->manualPaletteIndex < 0) {
                                settings->manualPaletteIndex = rt->getActivePaletteIndex();
                            }
                        }
                        settingsChanged = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable Automatic Palette Timeline Playback");
                    
                    if (settings->paletteEnabled) {
                        ImGui::SameLine();
                        if (ImGui::Button("Next##Pal")) {
                            std::lock_guard<std::mutex> lock(prog->generativeMutex);
                            rt->triggerNextPalette(prog.get());
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Trigger Next Palette Crossfade");
                        ImGui::SameLine();
                        ImGui::Text("Palette: %s", getPaletteName(rt->getActivePaletteIndex()).c_str());
                        if (rt->getTargetPaletteIndex() >= 0) {
                            float fadeProg = rt->getPaletteFadeProgress();
                            float displayProg = (fadeProg >= 1.0f) ? 0.0f : fadeProg;
                            ImGui::SameLine();
                            ImGui::TextDisabled("-> %s (%.0f%%)", getPaletteName(rt->getTargetPaletteIndex()).c_str(), displayProg * 100.0f);
                        } else {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(Next in %.1fs)", std::max(0.0f, rt->getPaletteTimeLeft()));
                        }
                    } else {
                        ImGui::SameLine();
                        ImGui::TextUnformatted("Fixed Palette:");
                        ImGui::SameLine();
                        int palIdx = settings->manualPaletteIndex;
                        std::vector<const char*> palPtrs;
                        for (const auto& n : paletteNames) palPtrs.push_back(n.c_str());
                        ImGui::SetNextItemWidth(180.0f);
                        if (ImGui::Combo("##ManualPalette", &palIdx, palPtrs.data(), (int)palPtrs.size())) {
                            settings->manualPaletteOverride = true;
                            settings->manualPaletteIndex = palIdx;
                            settingsChanged = true;
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::Spacing();

                // Motive Queue
                {
                    ImGui::PushID("MotiveQueueControl");
                    bool mActive = settings->motiveEnabled;
                    if (ImGui::Checkbox("##playMotive", &mActive)) {
                        settings->motiveEnabled = mActive;
                        if (mActive) {
                            settings->manualMotiveOverride = false;
                        } else {
                            settings->manualMotiveOverride = true;
                            if (settings->manualMotiveIndex < 0) {
                                settings->manualMotiveIndex = rt->getActiveMotiveIndex();
                            }
                        }
                        settingsChanged = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable Automatic Motive Timeline Playback");
                    
                    if (settings->motiveEnabled) {
                        ImGui::SameLine();
                        if (ImGui::Button("Next##Mot")) {
                            std::lock_guard<std::mutex> lock(prog->generativeMutex);
                            rt->triggerNextMotive(prog.get());
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Trigger Next Motive Easing");
                        ImGui::SameLine();
                        ImGui::Text("Motive: %s", getMotiveName(rt->getActiveMotiveIndex()).c_str());
                        if (rt->getTargetMotiveIndex() >= 0) {
                            float fadeProg = rt->getMotiveFadeProgress();
                            float displayProg = (fadeProg >= 1.0f) ? 0.0f : fadeProg;
                            ImGui::SameLine();
                            ImGui::TextDisabled("-> %s (%.0f%%)", getMotiveName(rt->getTargetMotiveIndex()).c_str(), displayProg * 100.0f);
                        } else {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(Next in %.1fs)", std::max(0.0f, rt->getMotiveTimeLeft()));
                        }
                    } else {
                        ImGui::SameLine();
                        ImGui::TextUnformatted("Fixed Motive:");
                        ImGui::SameLine();
                        int motIdx = settings->manualMotiveIndex;
                        std::vector<const char*> motPtrs;
                        for (const auto& n : motiveNames) motPtrs.push_back(n.c_str());
                        ImGui::SetNextItemWidth(180.0f);
                        if (ImGui::Combo("##ManualMotive", &motIdx, motPtrs.data(), (int)motPtrs.size())) {
                            settings->manualMotiveOverride = true;
                            settings->manualMotiveIndex = motIdx;
                            settingsChanged = true;
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::Spacing();

                // Shader Queue
                {
                    ImGui::PushID("ShaderQueueControl");
                    bool sActive = settings->shaderEnabled;
                    if (ImGui::Checkbox("##playShader", &sActive)) {
                        settings->shaderEnabled = sActive;
                        if (sActive) {
                            settings->manualShaderOverride = false;
                        } else {
                            settings->manualShaderOverride = true;
                            if (settings->manualShaderIndex < 0) {
                                settings->manualShaderIndex = rt->getActiveShaderIndex();
                            }
                        }
                        settingsChanged = true;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enable Automatic Shader Timeline Playback");
                    
                    if (prog->activeCueIndex.load() >= 0) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Shader: Cue Active (Suspended)");
                    } else if (settings->shaderEnabled) {
                        ImGui::SameLine();
                        if (ImGui::Button("Next##Sh")) {
                            std::lock_guard<std::mutex> lock(prog->generativeMutex);
                            rt->triggerNextShader(prog.get());
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Trigger Next Shader Transition");
                        ImGui::SameLine();
                        ImGui::Text("Shader: %s", getShaderName(rt->getActiveShaderIndex()).c_str());
                        if (rt->getTargetShaderIndex() >= 0) {
                            float fadeProg = rt->getShaderFadeProgress();
                            float displayProg = (fadeProg >= 1.0f) ? 0.0f : fadeProg;
                            ImGui::SameLine();
                            ImGui::TextDisabled("-> %s (%.0f%%)", getShaderName(rt->getTargetShaderIndex()).c_str(), displayProg * 100.0f);
                        } else {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(Next in %.1fs)", std::max(0.0f, rt->getShaderTimeLeft()));
                        }
                    } else {
                        ImGui::SameLine();
                        ImGui::TextUnformatted("Fixed Shader:");
                        ImGui::SameLine();
                        int shIdx = settings->manualShaderIndex;
                        std::vector<const char*> shPtrs;
                        for (const auto& n : shaderNames) shPtrs.push_back(n.c_str());
                        ImGui::SetNextItemWidth(180.0f);
                        if (ImGui::Combo("##ManualShader", &shIdx, shPtrs.data(), (int)shPtrs.size())) {
                            settings->manualShaderOverride = true;
                            settings->manualShaderIndex = shIdx;
                            settingsChanged = true;
                        }
                    }
                    ImGui::PopID();
                }
            } else {
                ImGui::TextDisabled("Generative runtime not active. Check Master Enable.");
            }

            ImGui::Spacing();

            // Force manual transition type
            int manualTrans = settings->manualTransitionType;
            const char* transComboOpts[] = {
                "Random / Auto", 
                "Linear Dissolve", 
                "Luma Wipe", 
                "Sweep Wipe",
                "Circle/Sphere Wipe",
                "Luminosity Wipe"
            };
            int transSel = manualTrans + 1; // map -1..4 to 0..5
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo("Transition Type Override", &transSel, transComboOpts, 6)) {
                settings->manualTransitionType = transSel - 1; // map 0..5 to -1..4
                settingsChanged = true;
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::CollapsingHeader("Timeline & Intervals Config", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SeparatorText("Palettes Timeline");
                float pInt = settings->paletteInterval;
                if (ImGui::SliderFloat("Palette Interval (s)", &pInt, 5.0f, 300.0f, "%.0f s")) {
                    settings->paletteInterval = pInt;
                    settingsChanged = true;
                }
                float pJit = settings->paletteJitter;
                if (ImGui::SliderFloat("Palette Jitter (s)", &pJit, 0.0f, 60.0f, "%.0f s")) {
                    settings->paletteJitter = pJit;
                    settingsChanged = true;
                }
                float pFade = settings->paletteCrossfade;
                if (ImGui::SliderFloat("Palette Fade (s)", &pFade, 0.1f, 30.0f, "%.1f s")) {
                    settings->paletteCrossfade = pFade;
                    settingsChanged = true;
                }

                ImGui::SeparatorText("Motives Timeline");
                float mInt = settings->motiveInterval;
                if (ImGui::SliderFloat("Motive Interval (s)", &mInt, 5.0f, 300.0f, "%.0f s")) {
                    settings->motiveInterval = mInt;
                    settingsChanged = true;
                }
                float mJit = settings->motiveJitter;
                if (ImGui::SliderFloat("Motive Jitter (s)", &mJit, 0.0f, 60.0f, "%.0f s")) {
                    settings->motiveJitter = mJit;
                    settingsChanged = true;
                }
                float mFade = settings->motiveCrossfade;
                if (ImGui::SliderFloat("Motive Fade (s)", &mFade, 0.1f, 30.0f, "%.1f s")) {
                    settings->motiveCrossfade = mFade;
                    settingsChanged = true;
                }

                ImGui::SeparatorText("Shaders Timeline");
                float sInt = settings->shaderInterval;
                if (ImGui::SliderFloat("Shader Interval (s)", &sInt, 5.0f, 300.0f, "%.0f s")) {
                    settings->shaderInterval = sInt;
                    settingsChanged = true;
                }
                float sJit = settings->shaderJitter;
                if (ImGui::SliderFloat("Shader Jitter (s)", &sJit, 0.0f, 60.0f, "%.0f s")) {
                    settings->shaderJitter = sJit;
                    settingsChanged = true;
                }
                float sFade = settings->shaderCrossfade;
                if (ImGui::SliderFloat("Shader Fade (s)", &sFade, 0.1f, 30.0f, "%.1f s")) {
                    settings->shaderCrossfade = sFade;
                    settingsChanged = true;
                }

                ImGui::SeparatorText("Transition Blend Types");
                bool eLin = settings->enableLinearDissolve;
                if (ImGui::Checkbox("Linear Dissolve", &eLin)) {
                    settings->enableLinearDissolve = eLin;
                    settingsChanged = true;
                }
                bool eLuma = settings->enableLumaWipe;
                if (ImGui::Checkbox("Luma Wipe (Noise-based)", &eLuma)) {
                    settings->enableLumaWipe = eLuma;
                    settingsChanged = true;
                }
                bool eSweep = settings->enableSweep;
                if (ImGui::Checkbox("Sweep Wipe (Directional)", &eSweep)) {
                    settings->enableSweep = eSweep;
                    settingsChanged = true;
                }
                bool eCircle = settings->enableCircleWipe;
                if (ImGui::Checkbox("Circle/Sphere Wipe", &eCircle)) {
                    settings->enableCircleWipe = eCircle;
                    settingsChanged = true;
                }
                bool eLum = settings->enableLuminosityWipe;
                if (ImGui::Checkbox("Luminosity Wipe", &eLum)) {
                    settings->enableLuminosityWipe = eLum;
                    settingsChanged = true;
                }
                bool tVol = settings->transitionVolumetric;
                if (ImGui::Checkbox("Volumetric 3D Wipes", &tVol)) {
                    settings->transitionVolumetric = tVol;
                    settingsChanged = true;
                }
            }

            if (settingsChanged) {
                auto activeProg = std::atomic_load(&App::currentPatchProgram);
                if (activeProg) {
                    std::lock_guard<std::mutex> lock(activeProg->generativeMutex);
                    activeProg->generativeSettings = *settings;
                }
                selectedPatch.set<Generative::Settings>(*settings);
            }
        }
        ImGui::End();
    });

    w.system<>("WindowGenerativeTelemetry").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app          = App::get(it.world());
        auto* ui          = &app.get_mut<App::UIConfig>();

        if (!ui->showGenerativeTelemetryWindow) return;
        auto selectedPatch = Patch::getSelected(app);

        // Keep 2D preview active if telemetry is visible
        auto prog = std::atomic_load(&App::currentPatchProgram);
        if (prog) {
            prog->showPlaybackPreview.store(true);
        }

        if(ImGui::Begin("Generative Telemetry", &ui->showGenerativeTelemetryWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            bool isRunning = prog && prog->generativeSettings.masterEnabled && prog->generativeRuntime;

            // --- Live Shader Preview ---
            if (prog && prog->renderMode == Patch::RenderMode::GLSL) {
                GLuint texID = prog->glslCurrentPlaybackPreviewTexID.load();
                if (texID == 0) {
                    texID = prog->glslPlaybackPreviewFboTex;
                }

                if (texID != 0) {
                    float tw = (float)prog->previewWidth;
                    float th = (float)prog->previewHeight;
                    ImVec2 avail = ImGui::GetContentRegionAvail();
                    float targetH = 150.0f;
                    float scale = targetH / th;
                    ImVec2 imgSize(tw * scale, th * scale);
                    
                    ImGui::SeparatorText("Live Playback Shader Preview");
                    ImGui::SetCursorPosX((avail.x - imgSize.x) * 0.5f + ImGui::GetCursorPosX());
                    ImGui::Image((ImTextureID)(intptr_t)texID, imgSize, ImVec2(0,0), ImVec2(1,1));
                    ImGui::Spacing();
                }
            }

            // --- Live Palette Playback Preview ---
            if (prog && prog->generativeRuntime) {
                auto rt = prog->generativeRuntime;
                ImGui::SeparatorText("Live Palette Playback");
                auto ubo = rt->getUboState();
                if (ubo.activeStops >= 2) {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    ImVec2 barPos = ImGui::GetCursorScreenPos();
                    float barWidth = ImGui::GetContentRegionAvail().x - 10.0f;
                    float barHeight = 20.0f;
                    ImGui::InvisibleButton("##live_palette_bar", ImVec2(barWidth, barHeight));

                    for (float x = 0; x < barWidth; x += 2.0f) {
                        float t = x / barWidth;
                        
                        glm::vec4 col(0.0f);
                        for (int i = 0; i < ubo.activeStops - 1; i++) {
                            float p0 = ubo.palette[i].position;
                            float p1 = ubo.palette[i+1].position;
                            if (t >= p0 && t <= p1) {
                                float blend = (t - p0) / std::max(0.0001f, p1 - p0);
                                float smoothness = glm::mix(ubo.palette[i].smoothness, ubo.palette[i+1].smoothness, blend);
                                float width = smoothness;
                                float f;
                                if (width > 0.001f) {
                                    float edge0 = 0.5f - width * 0.5f;
                                    float val = glm::clamp((blend - edge0) / width, 0.0f, 1.0f);
                                    f = val * val * (3.0f - 2.0f * val);
                                } else {
                                    f = (blend < 0.5f) ? 0.0f : 1.0f;
                                }
                                col = glm::mix(ubo.palette[i].color, ubo.palette[i+1].color, f);
                                break;
                            }
                        }
                        ImU32 imCol = IM_COL32((int)(col.r * 255.0f), (int)(col.g * 255.0f), (int)(col.b * 255.0f), 255);
                        drawList->AddRectFilled(ImVec2(barPos.x + x, barPos.y), ImVec2(barPos.x + x + 2.0f, barPos.y + barHeight), imCol);
                    }
                    drawList->AddRect(barPos, ImVec2(barPos.x + barWidth, barPos.y + barHeight), IM_COL32(255, 255, 255, 100));

                    // Render stops as circular indicators below the bar
                    ImGui::Spacing();
                    ImVec2 markerAreaPos = ImGui::GetCursorScreenPos();
                    float markerAreaHeight = 16.0f;
                    ImGui::InvisibleButton("##live_stops_area", ImVec2(barWidth, markerAreaHeight));
                    for (int i = 0; i < ubo.activeStops; i++) {
                        float stopX = barPos.x + ubo.palette[i].position * barWidth;
                        float stopY = markerAreaPos.y + markerAreaHeight * 0.5f;
                        
                        // Alignment line pointing up to the gradient bar
                        drawList->AddLine(ImVec2(stopX, barPos.y + barHeight), ImVec2(stopX, stopY - 5.0f), IM_COL32(255, 255, 255, 120), 1.0f);

                        // Color-coded circle with outline
                        ImU32 stopCol = IM_COL32((int)(ubo.palette[i].color.r * 255.0f), (int)(ubo.palette[i].color.g * 255.0f), (int)(ubo.palette[i].color.b * 255.0f), 255);
                        drawList->AddCircleFilled(ImVec2(stopX, stopY), 5.0f, IM_COL32(0, 0, 0, 255), 12);
                        drawList->AddCircleFilled(ImVec2(stopX, stopY), 4.0f, stopCol, 12);
                        drawList->AddCircle(ImVec2(stopX, stopY), 4.0f, IM_COL32(255, 255, 255, 200), 12, 1.0f);
                    }
                } else {
                    ImGui::TextDisabled("No active palette to display.");
                }
            } else {
                ImGui::TextDisabled("Generative runtime not active.");
            }

            ImGui::Spacing();

            // --- Live Telemetry (Oscilloscope) ---
            ImGui::SeparatorText("Live Motive Telemetry");

            if (prog && prog->generativeRuntime) {
                auto rt = prog->generativeRuntime;

                static float historyVelocity[120] = {0.0f};
                static float historyComplexity[120] = {0.0f};
                static float historyScale[120] = {0.0f};
                static float historyDistortion[120] = {0.0f};
                static float historyAsymmetry[120] = {0.0f};
                static float historyIntensity[120] = {0.0f};
                static int historyOffset = 0;
                static PatchProgram* lastProg = nullptr;

                if (prog.get() != lastProg) {
                    std::memset(historyVelocity, 0, sizeof(historyVelocity));
                    std::memset(historyComplexity, 0, sizeof(historyComplexity));
                    std::memset(historyScale, 0, sizeof(historyScale));
                    std::memset(historyDistortion, 0, sizeof(historyDistortion));
                    std::memset(historyAsymmetry, 0, sizeof(historyAsymmetry));
                    std::memset(historyIntensity, 0, sizeof(historyIntensity));
                    historyOffset = 0;
                    lastProg = prog.get();
                }

                if (isRunning) {
                    historyVelocity[historyOffset] = rt->getLiveVelocity();
                    historyComplexity[historyOffset] = rt->getLiveComplexity();
                    historyScale[historyOffset] = rt->getLiveScale();
                    historyDistortion[historyOffset] = rt->getLiveDistortion();
                    historyAsymmetry[historyOffset] = rt->getLiveAsymmetry();
                    historyIntensity[historyOffset] = rt->getLiveIntensity();
                    historyOffset = (historyOffset + 1) % 120;
                }

                if (ImGui::BeginTable("TelemetryPlots", 2)) {
                    auto drawGraph = [&](const char* title, const float* data, ImVec4 color, float currentVal) {
                        ImGui::TableNextColumn();
                        char label[128];
                        snprintf(label, sizeof(label), "%s: %.2f", title, currentVal);
                        ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
                        ImGui::PlotLines("##Graph", data, 120, historyOffset, label, 0.0f, 1.0f, ImVec2(-1, 55.0f));
                        ImGui::PopStyleColor();
                    };

                    drawGraph("Velocity", historyVelocity, ImVec4(0.2f, 0.8f, 1.0f, 1.0f), rt->getLiveVelocity());
                    drawGraph("Complexity", historyComplexity, ImVec4(1.0f, 0.2f, 0.8f, 1.0f), rt->getLiveComplexity());
                    drawGraph("Scale", historyScale, ImVec4(1.0f, 0.7f, 0.2f, 1.0f), rt->getLiveScale());
                    drawGraph("Distortion", historyDistortion, ImVec4(1.0f, 0.3f, 0.3f, 1.0f), rt->getLiveDistortion());
                    drawGraph("Asymmetry", historyAsymmetry, ImVec4(0.7f, 0.4f, 1.0f, 1.0f), rt->getLiveAsymmetry());
                    drawGraph("Intensity", historyIntensity, ImVec4(0.3f, 0.9f, 0.3f, 1.0f), rt->getLiveIntensity());

                    ImGui::EndTable();
                }
            } else {
                ImGui::TextDisabled("Enable Generative Engine to see live waveforms.");
            }
        }
        ImGui::End();
    });

    w.system<>("WindowMotives").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app          = App::get(it.world());
        auto* ui          = &app.get_mut<App::UIConfig>();
        if (!ui->showMotivesWindow) return;
        auto selectedPatch = Patch::getSelected(app);

        if(ImGui::Begin("Motive Presets", &ui->showMotivesWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            flecs::entity motiveFolder = selectedPatch.target<Generative::MotiveFolder>();
            if(!motiveFolder.is_valid()){ ImGui::TextDisabled("No motive folder."); ImGui::End(); return; }

            if(ImGui::BeginTable("MotiveTable", 2, ImGuiTableFlags_Resizable)){
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::SeparatorText("Presets");

                if (ImGui::Button("+ Preset")) {
                    static int customMotiveCount = 1;
                    std::string newName = "Motive " + std::to_string(customMotiveCount++);
                    Generative::Motive::Params params;
                    auto p = it.world().entity().child_of(motiveFolder)
                        .add<Generative::Motive::Is>()
                        .set<Generative::Motive::Params>(params);
                    p.set_name(newName.c_str());
                    
                    auto prog = std::atomic_load(&App::currentPatchProgram);
                    if (prog) {
                        std::lock_guard<std::mutex> lock(prog->generativeMutex);
                        Generative::CompiledMotive cm;
                        cm.name = newName;
                        cm.velocity = params.velocity;
                        cm.complexity = params.complexity;
                        cm.scale = params.scale;
                        cm.distortion = params.distortion;
                        cm.asymmetry = params.asymmetry;
                        cm.intensity = params.intensity;
                        std::memcpy(cm.wanderAmp, params.wanderAmp, sizeof(cm.wanderAmp));
                        std::memcpy(cm.wanderFreq, params.wanderFreq, sizeof(cm.wanderFreq));
                        prog->motivePool.push_back(cm);
                    }
                }
                
                ImGui::SameLine();
                
                static flecs::id_t selectedMotiveId = 0;
                
                bool hasSel = false;
                flecs::entity selectedMotive = it.world().entity(selectedMotiveId);
                if (selectedMotive.is_valid() && selectedMotive.is_alive() && selectedMotive.parent() == motiveFolder) {
                    hasSel = true;
                }

                if (!hasSel) ImGui::BeginDisabled();
                if (ImGui::Button("Remove")) {
                    std::string nameToRemove = selectedMotive.name().c_str();
                    selectedMotive.destruct();
                    selectedMotiveId = 0;
                    hasSel = false;
                    
                    auto prog = std::atomic_load(&App::currentPatchProgram);
                    if (prog) {
                        std::lock_guard<std::mutex> lock(prog->generativeMutex);
                        prog->motivePool.erase(
                            std::remove_if(prog->motivePool.begin(), prog->motivePool.end(),
                                [&](const Generative::CompiledMotive& cm) { return cm.name == nameToRemove; }),
                            prog->motivePool.end()
                        );
                    }
                }
                if (!hasSel) ImGui::EndDisabled();

                ImGui::Separator();

                if (ImGui::BeginListBox("##MotiveList", ImGui::GetContentRegionAvail())) {
                    motiveFolder.children([&](flecs::entity child) {
                        if (child.has<Generative::Motive::Is>()) {
                            bool isSel = (child.id() == selectedMotiveId);
                            if (ImGui::Selectable(child.name().c_str(), isSel)) {
                                selectedMotiveId = child.id();
                            }
                        }
                    });
                    ImGui::EndListBox();
                }

                ImGui::TableSetColumnIndex(1);
                selectedMotive = it.world().entity(selectedMotiveId);
                if (selectedMotive.is_valid() && selectedMotive.is_alive() && selectedMotive.parent() == motiveFolder) {
                    ImGui::Text("Editing Preset: %s", selectedMotive.name().c_str());
                    ImGui::Separator();

                    char nameBuf[128] = {};
                    std::strncpy(nameBuf, selectedMotive.name().c_str(), sizeof(nameBuf) - 1);
                    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                        std::string oldName = selectedMotive.name().c_str();
                        selectedMotive.set_name(nameBuf);
                        auto prog = std::atomic_load(&App::currentPatchProgram);
                        if (prog) {
                            std::lock_guard<std::mutex> lock(prog->generativeMutex);
                            for (auto& cm : prog->motivePool) {
                                if (cm.name == oldName) {
                                    cm.name = nameBuf;
                                    break;
                                }
                            }
                        }
                    }

                    auto* params = selectedMotive.try_get_mut<Generative::Motive::Params>();
                    if (params) {
                        bool paramsChanged = false;

                        ImGui::Spacing();
                        ImGui::SeparatorText("Parameters & Wander Controls");

                        if (ImGui::BeginTable("MotiveParamsTable", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                            ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                            ImGui::TableSetupColumn("Base Value", ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableSetupColumn("Wander Amp", ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableSetupColumn("Wander Freq", ImGuiTableColumnFlags_WidthStretch);
                            ImGui::TableHeadersRow();

                            auto drawParamRow = [&](const char* label, float& base, float& amp, float& freq) {
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::TextUnformatted(label);

                                ImGui::TableSetColumnIndex(1);
                                ImGui::PushID(label);
                                ImGui::PushItemWidth(-1);
                                paramsChanged |= ImGui::SliderFloat("##Base", &base, 0.0f, 1.0f, "%.2f");
                                ImGui::PopItemWidth();

                                ImGui::TableSetColumnIndex(2);
                                ImGui::PushItemWidth(-1);
                                paramsChanged |= ImGui::SliderFloat("##Amp", &amp, 0.0f, 0.5f, "%.2f");
                                ImGui::PopItemWidth();

                                ImGui::TableSetColumnIndex(3);
                                ImGui::PushItemWidth(-1);
                                paramsChanged |= ImGui::SliderFloat("##Freq", &freq, 0.0f, 2.0f, "%.2f");
                                ImGui::PopItemWidth();
                                ImGui::PopID();
                            };

                            drawParamRow("Velocity", params->velocity, params->wanderAmp[0], params->wanderFreq[0]);
                            drawParamRow("Complexity", params->complexity, params->wanderAmp[1], params->wanderFreq[1]);
                            drawParamRow("Scale", params->scale, params->wanderAmp[2], params->wanderFreq[2]);
                            drawParamRow("Distortion", params->distortion, params->wanderAmp[3], params->wanderFreq[3]);
                            drawParamRow("Asymmetry", params->asymmetry, params->wanderAmp[4], params->wanderFreq[4]);
                            drawParamRow("Intensity", params->intensity, params->wanderAmp[5], params->wanderFreq[5]);

                            ImGui::EndTable();
                        }

                        if (paramsChanged) {
                            auto prog = std::atomic_load(&App::currentPatchProgram);
                            if (prog) {
                                std::lock_guard<std::mutex> lock(prog->generativeMutex);
                                for (auto& cm : prog->motivePool) {
                                    if (cm.name == selectedMotive.name().c_str()) {
                                        cm.velocity = params->velocity;
                                        cm.complexity = params->complexity;
                                        cm.scale = params->scale;
                                        cm.distortion = params->distortion;
                                        cm.asymmetry = params->asymmetry;
                                        cm.intensity = params->intensity;
                                        std::memcpy(cm.wanderAmp, params->wanderAmp, sizeof(cm.wanderAmp));
                                        std::memcpy(cm.wanderFreq, params->wanderFreq, sizeof(cm.wanderFreq));
                                        break;
                                    }
                                }
                            }
                        }
                    }
                } else {
                    ImGui::TextDisabled("Select a motive preset from the list.");
                }

                ImGui::EndTable();
            }
        }
        ImGui::End();
    });

    w.system<>("WindowOfflinePreview").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app          = App::get(it.world());
        auto* ui          = &app.get_mut<App::UIConfig>();
        if (!ui->showOfflinePreviewWindow) return;
        auto selectedPatch = Patch::getSelected(app);

        if(ImGui::Begin("Effect Preview", &ui->showOfflinePreviewWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            auto prog = std::atomic_load(&App::currentPatchProgram);
            if(prog && prog->glslEditorFboTex != 0){
                GLuint tex = prog->glslEditorFboTex;
                float tw = (float)prog->previewWidth;
                float th = (float)prog->previewHeight;

                ImVec2 avail = ImGui::GetContentRegionAvail();
                float scale = std::min(avail.x / tw, avail.y / th);
                if (scale < 0.1f) scale = 0.1f;
                ImVec2 imgSize(tw * scale, th * scale);

                ImGui::SetCursorPosX((avail.x - imgSize.x) * 0.5f + ImGui::GetCursorPosX());
                ImGui::SetCursorPosY((avail.y - imgSize.y) * 0.5f + ImGui::GetCursorPosY());

                ImGui::Image((ImTextureID)(intptr_t)tex, imgSize, ImVec2(0,0), ImVec2(1,1));
            } else {
                ImGui::TextDisabled("No offline shader preview available.");
            }
        }
        ImGui::End();
    });

} // import()

} // namespace PixelMapper::Gui





