#include "PixelMapper.h"

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

static bool autoCompile = true;
static bool hasUncompiledChanges = false;
 
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
        if(ImGui::BeginMainMenuBar()){

            if(ImGui::BeginMenu("File")){
                if(ImGui::MenuItem("Save Patch", "Cmd+S"))
                    PatchSerializer::save(app, "patches/default.xml");
                if(ImGui::MenuItem("Load Patch"))
                    PatchSerializer::load(app, "patches/default.xml");
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
                ImGui::MenuItem("Script & Shader Editor",  nullptr, &ui->showScriptEditor);
                ImGui::MenuItem("Cue List",                nullptr, &ui->showCuesWindow);
                ImGui::MenuItem("Offline Preview",         nullptr, &ui->showOfflinePreviewWindow);
                ImGui::MenuItem("Effect Bank",             nullptr, &ui->showEffectBankWindow);
                ImGui::EndMenu();
            }

            // ── RT stats (right-aligned) ──
            float rtFps    = App::rtFps.load();
            float rtMbps   = App::rtBitrateMbps.load();
            char statusBuf[96];
            snprintf(statusBuf, sizeof(statusBuf),
                     "RT: %.0f fps   |   ArtNet: %.2f Mbit/s", rtFps, rtMbps);
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
            if(ImGui::IsKeyPressed(ImGuiKey_S, false)) PatchSerializer::save(app, "patches/default.xml");
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
                        bool e = false;
                        ImGui::SeparatorText("Dmx Address");
                        uint16_t s1 = 1, s10 = 10;
                        e |= ImGui::InputScalar("Universe", ImGuiDataType_U16, &dmx.universe, &s1, &s10);
                        e |= ImGui::InputScalar("Address",  ImGuiDataType_U16, &dmx.address,  &s1, &s10);
                        if(e) selectedFixture.set<Fixture::DmxAddress>(dmx);
                    }
                    auto shapeType = selectedFixture.target<Fixture::WithShape>();
                    if(shapeType == selectedFixture.world().id<Shape::Line>()){
                        Shape::Line l = selectedFixture.get<Fixture::WithShape, Shape::Line>();
                        bool e = false;
                        ImGui::SeparatorText("Line Segment");
                        e |= ImGui::InputFloat2("Start", &l.start.x, "%.1fmm");
                        e |= ImGui::InputFloat2("End",   &l.end.x,   "%.1fmm");
                        if(e){ selectedFixture.get_mut<Fixture::WithShape, Shape::Line>() = l; selectedFixture.add<Fixture::PixelPositionsDirty>(); }
                    } else if(shapeType == selectedFixture.world().id<Shape::Circle>()){
                        Shape::Circle c = selectedFixture.get<Fixture::WithShape, Shape::Circle>();
                        bool e = false;
                        ImGui::SeparatorText("Circle");
                        e |= ImGui::InputFloat2("Center", &c.center.x, "%.1fmm");
                        e |= ImGui::InputFloat("Radius",  &c.radius,   0, 0, "%.1fmm");
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

        static bool b_showFixtures = true;
        static bool b_showPixels   = true;
        static bool b_showFrame    = true;

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
            ImGui::Checkbox("Fixtures", &b_showFixtures); ImGui::SameLine();
            ImGui::Checkbox("Pixels",   &b_showPixels);   ImGui::SameLine();
            ImGui::Checkbox("Rendered", &b_showFrame);    ImGui::SameLine();
            if(ImGui::Button("Zoom to Fit") && selectedPatch.is_valid()){
                if(const auto* ra = selectedPatch.try_get<Patch::RenderArea>())
                    canvas.zoomToFit(glm::vec2(ra->min), glm::vec2(ra->max), {30,30});
            }

            if(ImDrawList* drawing = canvas.begin("Canvas", ImGui::GetContentRegionAvail())){
                // ── Capture canvas interaction state BEFORE any child widgets ──
                bool canvasHovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(canvas.frameMin, canvas.frameMax) && !ImGui::IsAnyItemHovered();
                bool canvasClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && canvasHovered;
                bool canvasActive  = ImGui::IsMouseDown(ImGuiMouseButton_Left) && canvasHovered;
                glm::vec2 mCanvas  = canvas.getMouseCanvasPos();
                bool shiftHeld     = ImGui::GetIO().KeyShift;

                if (ui->showGrid) {
                    canvas.drawGrid(100.0, 0xFF333333, 0xFF000000);
                } else {
                    drawing->AddRectFilled(canvas.frameMin, canvas.frameMax, 0xFF000000);
                }

                if(selectedPatch.is_valid()){

                    // ── VFB preview texture ──
                    if(b_showFrame){
                        auto prog = std::atomic_load(&App::currentPatchProgram);
                        if(prog){
                            int tw = prog->vfbWidth;
                            int th = prog->vfbHeight;
                            GLuint texID = 0;
                            if(prog->renderMode == Patch::RenderMode::GLSL){
                                if (prog->crossfadeProgress < 1.0f && prog->glslFboTexBlend != 0) {
                                    texID = prog->glslFboTexBlend;
                                } else {
                                    texID = prog->glslFboTex;
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
                        drawing->AddRectFilled(canvas.canvasToScreen(glm::vec2(ra->min)),
                                               canvas.canvasToScreen(glm::vec2(ra->max)), IM_COL32(0, 0, 0, (int)(ui->previewOpacity * 0.27f * 255)));
                    }

                    // ── Draw fixtures ──
                    if(b_showFixtures){
                        Fixture::iterateWithDmx(selectedPatch,
                            [&](flecs::entity f, const Fixture::Layout& layout, const Fixture::DmxAddress&)
                        {
                            bool isSel   = (f == selectedFixture);
                            bool isMulti = msContains(selectedPatch, f);
                            uint32_t col = isSel ? 0xFF00FFFF : (isMulti ? 0xFF80FF80 : 0xFF0000FF);
                            auto st = f.target<Fixture::WithShape>();
                            if(st == f.world().id<Shape::Line>()){
                                const Shape::Line& l = f.get<Fixture::WithShape, Shape::Line>();
                                drawing->AddLine(canvas.canvasToScreen(l.start), canvas.canvasToScreen(l.end), col, 5.f);
                            } else if(st == f.world().id<Shape::Circle>()){
                                const Shape::Circle& c = f.get<Fixture::WithShape, Shape::Circle>();
                                drawing->AddCircle(canvas.canvasToScreen(c.center),
                                                   canvas.canvasSizeToScreenSize(c.radius), col, layout.pixelCount, 5.f);
                            }
                        });
                    }

                    if(b_showPixels){
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
                                    glm::vec2 sz(2);
                                    for(int i = 0; i < (int)pd->positions.size(); i++){
                                        auto p = canvas.canvasToScreen(pd->positions[i]);
                                        ColorRGBW c{0, 0, 0, 255};
                                        if (hasColors && (pixelIndex + i < (int)tempColors.size())) {
                                            c = tempColors[pixelIndex + i];
                                        } else if (i < (int)pd->colors.size()) {
                                            c = pd->colors[i];
                                        }
                                        drawing->AddRectFilled(p-sz, p+sz, IM_COL32(c.r, c.g, c.b, 255));
                                    }
                                }
                                pixelIndex += layout.pixelCount;
                        });
                    }

                    // ── Double-click to add fixture ──
                    if (!ui->patchLocked) {
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

                    if(!ui->patchLocked && selectedFixture.is_valid() && b_showFixtures && !ImGui::IsKeyDown(ImGuiKey_Space) && !fixtureDragging){
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
                    if(canvasClicked && !handleDragged && !handleActive && b_showFixtures && !ImGui::IsKeyDown(ImGuiKey_Space)){
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
                    if(fixtureDragging){
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
                    if(marqueeActive){
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

                if (ImGui::Button("◀ Prev") && !univs.empty()) {
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

                if (ImGui::Button("Next ▶") && !univs.empty()) {
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
                    ImGui::SeparatorText("Timing");
                    e |= ImGui::SliderFloat("Refresh Rate (Hz)", &s->refreshRate, 1.f, 120.f, "%.1f Hz");
                    
                    ImGui::SeparatorText("Virtual Framebuffer");
                    int vfbRes = s->vfbResolution;
                    if (ImGui::SliderInt("Resolution", &vfbRes, 16, 2048)) {
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

    // ─────────────── Script & Shader Editor ──────────────────────
    w.system<>("WindowScriptEditor").kind(flecs::OnStore)
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

        if(ImGui::Begin("Script & Shader Editor", &ui->showScriptEditor)){
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
                } else if (ui->editingCueIndex >= 0) {
                    flecs::entity cueEnt = getCueEntityByIndex(cueFolder, ui->editingCueIndex);
                    if (cueEnt.is_valid()) {
                        flecs::entity targetEffect = cueEnt.target<CueList::Cue::TargetEffect>();
                        if (targetEffect.is_valid()) {
                            if (const auto* glsl = targetEffect.try_get<EffectBank::Effect::GlslSource>()) {
                                targetText = glsl->value;
                            }
                        }
                    }
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
                } else if (ui->editingCueIndex >= 0) {
                    flecs::entity cueEnt = getCueEntityByIndex(cueFolder, ui->editingCueIndex);
                    if (cueEnt.is_valid()) {
                        flecs::entity targetEffect = cueEnt.target<CueList::Cue::TargetEffect>();
                        if (targetEffect.is_valid()) {
                            targetEffect.set<EffectBank::Effect::GlslSource>({newSource});
                        }
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
                ImGui::SetNextItemWidth(75);
                if(ImGui::InputInt("Res", &settings->vfbResolution)) {
                    settings->vfbResolution = std::clamp(settings->vfbResolution, 16, 2048);
                    selectedPatch.add<Patch::ProgramDirty>();
                }
            } else if (ui->editingCueIndex >= 0) {
                flecs::entity cueEnt = getCueEntityByIndex(cueFolder, ui->editingCueIndex);
                if (cueEnt.is_valid()) {
                    ImGui::Text("Editing Cue %d:", ui->editingCueIndex + 1); ImGui::SameLine();
                    char cueName[64];
                    std::strncpy(cueName, cueEnt.name().c_str(), sizeof(cueName)-1);
                    ImGui::SetNextItemWidth(120);
                    if (ImGui::InputText("Name", cueName, sizeof(cueName))) {
                        cueEnt.set_name(cueName);
                    }
                    ImGui::SameLine();
                    
                    float hold = 5.0f;
                    if (const auto* h = cueEnt.try_get<CueList::Cue::HoldDuration>()) hold = h->value;
                    ImGui::SetNextItemWidth(70);
                    if (ImGui::InputFloat("Hold", &hold, 0.1f, 1.0f, "%.1fs")) {
                        cueEnt.set<CueList::Cue::HoldDuration>({std::max(0.0f, hold)});
                    }
                    ImGui::SameLine();
                    
                    float fade = 0.0f;
                    if (const auto* f = cueEnt.try_get<CueList::Cue::FadeDuration>()) fade = f->value;
                    ImGui::SetNextItemWidth(70);
                    if (ImGui::InputFloat("Fade", &fade, 0.1f, 1.0f, "%.1fs")) {
                        cueEnt.set<CueList::Cue::FadeDuration>({std::max(0.0f, fade)});
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Release")) {
                        setEditingCueIndex(app, -1);
                    }
                }
            } else if (ui->editingCueIndex == -2 && ui->editingBankIndex >= 0) {
                flecs::entity fxEnt = getEffectEntityByIndex(bankFolder, ui->editingBankIndex);
                if (fxEnt.is_valid()) {
                    ImGui::Text("Editing Bank Effect %d:", ui->editingBankIndex + 1); ImGui::SameLine();
                    char fxName[64];
                    std::strncpy(fxName, fxEnt.name().c_str(), sizeof(fxName)-1);
                    ImGui::SetNextItemWidth(150);
                    if (ImGui::InputText("Name", fxName, sizeof(fxName))) {
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
                } else if (ui->editingCueIndex >= 0) {
                    flecs::entity cueEnt = getCueEntityByIndex(cueFolder, ui->editingCueIndex);
                    if (cueEnt.is_valid()) {
                        flecs::entity targetEffect = cueEnt.target<CueList::Cue::TargetEffect>();
                        if (targetEffect.is_valid()) {
                            targetEffect.set<EffectBank::Effect::GlslSource>({presetGlsl});
                        }
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

            ImGui::Separator();

            const float kLogHeight = 100.f;
            const float kSepHeight = ImGui::GetStyle().ItemSpacing.y + 1.f;
            float editorHeight = ImGui::GetContentRegionAvail().y - kLogHeight - kSepHeight * 3.f - ImGui::GetTextLineHeightWithSpacing();
            if(editorHeight < 80.f) editorHeight = 80.f;

            glslEditor->Render("GlslEd", ImVec2(0, editorHeight));

            ImGui::Separator();
            if (hasUncompiledChanges) {
                ImGui::TextColored({1.0f, 0.5f, 0.0f, 1.0f}, "● Unsaved Changes (compiling...)");
            } else {
                ImGui::TextColored({0.2f, 1.0f, 0.2f, 1.0f}, "● Saved & Compiled");
            }

            ImGui::Separator();
            ImGui::Text("Compilation Log:");
            ImGui::BeginChild("##Log", ImVec2(0, kLogHeight), true);
            std::string displayLog = "";
            auto prog = std::atomic_load(&App::currentPatchProgram);
            if (prog) {
                if (ui->editingCueIndex == -1) {
                    displayLog = prog->defaultCompilerLog;
                } else if (ui->editingCueIndex >= 0 && ui->editingCueIndex < (int)prog->compiledCues.size()) {
                    displayLog = prog->compiledCues[ui->editingCueIndex].compilerLog;
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
            if (!cueFolder.is_valid()) { ImGui::TextDisabled("No cue folder."); ImGui::End(); return; }
            auto* session = cueFolder.try_get_mut<CueList::SessionState>();
            if(!session){ ImGui::End(); return; }

            // ── Transport ──
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            ImGui::Checkbox("▶ Auto-advance", &session->autoAdvance); ImGui::SameLine();
            ImGui::Checkbox("↺ Loop",         &session->loop);

            struct CueEntry {
                flecs::entity entity;
                int order = 0;
                float hold = 5.0f;
                float fade = 2.0f;
                std::string name;
            };
            std::vector<CueEntry> cues;

            cueFolder.children([&](flecs::entity child) {
                if (child.has<CueList::Cue::Is>()) {
                    CueEntry entry;
                    entry.entity = child;
                    if (const auto* ord = child.try_get<CueList::Cue::IndexOrder>()) entry.order = ord->value;
                    if (const auto* h = child.try_get<CueList::Cue::HoldDuration>()) entry.hold = h->value;
                    if (const auto* f = child.try_get<CueList::Cue::FadeDuration>()) entry.fade = f->value;
                    entry.name = child.name();
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
                    program->activeCueIndex.store(idx);
                    program->pendingCrossfadeDuration.store(cueEntry.fade);
                }
            };

            ImGui::SameLine();
            if(ImGui::Button("⏮ Prev")){
                if(!cues.empty()){
                    int prev = session->activeIndex - 1;
                    if(prev < 0) prev = session->loop ? (int)cues.size()-1 : 0;
                    triggerCue(prev);
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("⏭ Next")){
                if(!cues.empty()){
                    int next = session->activeIndex + 1;
                    if(next >= (int)cues.size()) next = session->loop ? 0 : (int)cues.size()-1;
                    triggerCue(next);
                }
            }

            // Crossfade in progress indicator
            auto activeProg = std::atomic_load(&App::currentPatchProgram);
            if(activeProg && activeProg->crossfadeProgress < 1.f){
                float p = activeProg->crossfadeProgress;
                ImGui::SameLine();
                ImGui::TextColored({0.4f,0.8f,1.f,1.f}, "Crossfade: %.0f%%", p * 100.f);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.f);
                ImGui::ProgressBar(p, ImVec2(120.f, 0));
            }

            ImGui::Separator();

            if(ImGui::Button("+ Add Cue")){
                int nextOrder = 0;
                for (const auto& cue : cues) {
                    if (cue.order >= nextOrder) {
                        nextOrder = cue.order + 1;
                    }
                }
                std::string cueName = "Cue " + std::to_string(cues.size() + 1);
                
                auto* scriptData = selectedPatch.try_get<Patch::ScriptData>();
                std::string glsl = scriptData ? scriptData->glslSource : "";

                flecs::entity bankFolder = selectedPatch.target<EffectBank::EffectFolder>();
                auto fallbackEffect = it.world().entity()
                    .child_of(bankFolder)
                    .add<EffectBank::Effect::Is>()
                    .set<EffectBank::Effect::GlslSource>({glsl})
                    .set<Patch::GPUProgram>({});
                fallbackEffect.set_name(cueName.c_str());

                auto newCue = it.world().entity()
                    .child_of(cueFolder)
                    .add<CueList::Cue::Is>()
                    .set<CueList::Cue::HoldDuration>({5.f})
                    .set<CueList::Cue::FadeDuration>({2.f})
                    .set<CueList::Cue::IndexOrder>({nextOrder})
                    .add<CueList::Cue::TargetEffect>(fallbackEffect);
                newCue.set_name(cueName.c_str());

                if (session->activeIndex < 0) session->activeIndex = 0;
                selectedPatch.add<Patch::ProgramDirty>();
            }
            ImGui::PopStyleVar();
            ImGui::Separator();

            if (ImGui::BeginChild("##CueItems", ImVec2(0, 0), true)) {
                if (ImGui::BeginTable("CueTable", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Status/Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                    ImGui::TableSetupColumn("Hold", ImGuiTableColumnFlags_WidthFixed, 80.f);
                    ImGui::TableSetupColumn("Fade", ImGuiTableColumnFlags_WidthFixed, 80.f);
                    ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < (int)cues.size(); i++) {
                        auto& cueEntry = cues[i];
                        bool isActive = (i == session->activeIndex);
                        bool isEditing = (ui->editingCueIndex == i);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();

                        ImGui::PushID(cueEntry.entity.id());

                        if (isActive) {
                            ImGui::TextColored({0.2f, 1.0f, 0.2f, 1.0f}, "▶ ");
                            ImGui::SameLine();
                        } else if (isEditing) {
                            ImGui::TextColored({1.0f, 0.7f, 0.2f, 1.0f}, "✎ ");
                            ImGui::SameLine();
                        } else {
                            ImGui::Text("  ");
                            ImGui::SameLine();
                        }

                        char label[128];
                        std::snprintf(label, sizeof(label), "%s##select", cueEntry.name.c_str());
                        if (ImGui::Selectable(label, isActive, ImGuiSelectableFlags_SpanAllColumns)) {
                            triggerCue(i);
                        }

                        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                            uint64_t dragCid = cueEntry.entity.id();
                            ImGui::SetDragDropPayload("DND_CUE_ORDER", &dragCid, sizeof(dragCid));
                            ImGui::Text("Move %s", cueEntry.name.c_str());
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
                        float hold = cueEntry.hold;
                        ImGui::SetNextItemWidth(70);
                        if (ImGui::InputFloat("##h", &hold, 0.1f, 1.0f, "%.1fs")) {
                            cueEntry.entity.set<CueList::Cue::HoldDuration>({std::max(0.0f, hold)});
                        }

                        ImGui::TableNextColumn();
                        float fade = cueEntry.fade;
                        ImGui::SetNextItemWidth(70);
                        if (ImGui::InputFloat("##f", &fade, 0.1f, 1.0f, "%.1fs")) {
                            cueEntry.entity.set<CueList::Cue::FadeDuration>({std::max(0.0f, fade)});
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
                                setEditingCueIndex(app, i);
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
                            if (ui->editingCueIndex == i) {
                                setEditingCueIndex(app, -1);
                            } else if (ui->editingCueIndex > i) {
                                setEditingCueIndex(app, ui->editingCueIndex - 1);
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

            if(ImGui::Button("+ Add Experimental Effect")){
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
                selectedPatch.add<Patch::ProgramDirty>();
            }

            ImGui::Separator();

            if (ImGui::BeginChild("##BankItems", ImVec2(0, 0), true)) {
                if (ImGui::BeginTable("BankTable", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 110.f);
                    ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableHeadersRow();

                    std::vector<flecs::entity> fxList;
                    bankFolder.children([&](flecs::entity child) {
                        if (child.has<EffectBank::Effect::Is>()) {
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
                            ImGui::TextColored({1.0f, 0.7f, 0.2f, 1.0f}, "✎ %s", fx.name().c_str());
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
                            
                            std::string glsl = "";
                            if (const auto* src = fx.try_get<EffectBank::Effect::GlslSource>()) {
                                glsl = src->value;
                            }
                            
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
    w.system<>("WindowOfflinePreview").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        auto app          = App::get(it.world());
        auto* ui          = &app.get_mut<App::UIConfig>();
        if (!ui->showOfflinePreviewWindow) return;
        auto selectedPatch = Patch::getSelected(app);

        if(ImGui::Begin("Offline Shader Preview", &ui->showOfflinePreviewWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            auto prog = std::atomic_load(&App::currentPatchProgram);
            if(prog && prog->glslEditorFboTex != 0){
                GLuint tex = prog->glslEditorFboTex;
                float tw = prog->vfbWidth;
                float th = prog->vfbHeight;

                ImVec2 avail = ImGui::GetContentRegionAvail();
                float scale = std::min(avail.x / tw, avail.y / th);
                if (scale < 0.1f) scale = 0.1f;
                ImVec2 imgSize(tw * scale, th * scale);

                ImGui::SetCursorPosX((avail.x - imgSize.x) * 0.5f + ImGui::GetCursorPosX());
                ImGui::SetCursorPosY((avail.y - imgSize.y) * 0.5f + ImGui::GetCursorPosY());

                ImGui::Image((ImTextureID)(intptr_t)tex, imgSize, ImVec2(0,1), ImVec2(1,0));
            } else {
                ImGui::TextDisabled("No offline shader preview available.");
            }
        }
        ImGui::End();
    });

} // import()

} // namespace PixelMapper::Gui