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
 
static int editingCueIndex = -1;
static int editingBankIndex = -1;
static bool showOfflinePreviewWindow = false;
static bool showEffectBankWindow = false;
static bool autoCompile = true;
static bool hasUncompiledChanges = false;
 
static bool patchLocked = false;
static float previewOpacity = 1.0f;
static bool b_showGrid = true;
 
static void setEditingCueIndex(int idx) {
    editingCueIndex = idx;
    App::patchProgramLock.lock();
    if (App::currentPatchProgram) {
        App::currentPatchProgram->editingCueIndex = idx;
        if (idx >= -1) {
            App::currentPatchProgram->editingBankIndex = -1;
        }
    }
    App::patchProgramLock.unlock();
    if (idx >= -1) {
        editingBankIndex = -1;
    }
}
 
static void setEditingBankIndex(int idx) {
    editingBankIndex = idx;
    App::patchProgramLock.lock();
    if (App::currentPatchProgram) {
        App::currentPatchProgram->editingBankIndex = idx;
        if (idx >= 0) {
            App::currentPatchProgram->editingCueIndex = -2;
        }
    }
    App::patchProgramLock.unlock();
    if (idx >= 0) {
        editingCueIndex = -2;
    }
}

static GLuint vfbPreviewTexId = 0;
static int currentVfbWidth = 0;
static int currentVfbHeight = 0;

enum class GuiLayout {
    PatchEditing,
    EffectsControl
};

static GuiLayout currentLayout = GuiLayout::PatchEditing;

static bool showFixturesWindow    = true;
static bool showPatchEditor       = true;
static bool showArtnetData        = true;
static bool showNetworkSettings   = true;
static bool showArtnetDevices     = true;
static bool showScriptEditor      = false; // Default to false for PatchEditing layout
static bool showCuesWindow        = false;

static void applyLayout(GuiLayout layout) {
    currentLayout = layout;
    if (layout == GuiLayout::PatchEditing) {
        showPatchEditor       = true;
        showFixturesWindow    = true;
        showArtnetDevices     = true;
        showNetworkSettings   = true;
        showArtnetData        = true;
        showScriptEditor      = false;
        showCuesWindow        = false;
        showOfflinePreviewWindow = false;
        showEffectBankWindow  = false;
    } else if (layout == GuiLayout::EffectsControl) {
        showScriptEditor      = true;
        showCuesWindow        = true;
        showPatchEditor       = true;
        showFixturesWindow    = false;
        showArtnetDevices     = false;
        showNetworkSettings   = false;
        showArtnetData        = false;
        showOfflinePreviewWindow = true;
        showEffectBankWindow  = true;
    }
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
                if(ImGui::MenuItem("Patch Editing", nullptr, currentLayout == GuiLayout::PatchEditing)) {
                    applyLayout(GuiLayout::PatchEditing);
                }
                if(ImGui::MenuItem("Effects & Control", nullptr, currentLayout == GuiLayout::EffectsControl)) {
                    applyLayout(GuiLayout::EffectsControl);
                }
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("View")){
                ImGui::MenuItem("Fixtures",                nullptr, &showFixturesWindow);
                ImGui::MenuItem("Patch Editor",            nullptr, &showPatchEditor);
                ImGui::MenuItem("Artnet Data",             nullptr, &showArtnetData);
                ImGui::MenuItem("Patch & Network Settings",nullptr, &showNetworkSettings);
                ImGui::MenuItem("Artnet Devices",          nullptr, &showArtnetDevices);
                ImGui::MenuItem("Script & Shader Editor",  nullptr, &showScriptEditor);
                ImGui::MenuItem("Cue List",                nullptr, &showCuesWindow);
                ImGui::MenuItem("Offline Preview",         nullptr, &showOfflinePreviewWindow);
                ImGui::MenuItem("Effect Bank",             nullptr, &showEffectBankWindow);
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

    // ─────────────── Cmd+S shortcut ───────────────────────────
    w.system<>("KeyboardShortcuts").kind(flecs::PreStore)
    .run([&](flecs::iter& it){
        if (ImGui::GetIO().KeySuper && ImGui::IsKeyPressed(ImGuiKey_S, false))
            PatchSerializer::save(App::get(it.world()), "patches/default.xml");
    });

    // ─────────────── Fixtures Window ─────────────────────────────
    w.system<>("WindowFixtures").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        if (!showFixturesWindow) return;
        auto app              = App::get(it.world());
        auto selectedPatch    = Patch::getSelected(app);
        auto selectedFixture  = Fixture::getSelected(selectedPatch);

        if(ImGui::Begin("Fixtures", &showFixturesWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if(ImGui::Button("Add Line")){
                Fixture::createLine(selectedPatch, {0,0,0}, {100,100,0});
                selectedPatch.add<Patch::DmxMapDirty>();
                selectedPatch.add<Patch::RenderAreaDirty>();
            }
            ImGui::SameLine();
            if(ImGui::Button("Add Circle")){
                Fixture::createCircle(selectedPatch, {0,0,0}, 50.f);
                selectedPatch.add<Patch::DmxMapDirty>();
                selectedPatch.add<Patch::RenderAreaDirty>();
            }
            bool hasSel = selectedFixture.is_valid() && selectedFixture.is_alive();
            ImGui::SameLine();
            if(!hasSel) ImGui::BeginDisabled();
            if(ImGui::Button("Duplicate")){
                auto dup = Fixture::duplicate(selectedPatch, selectedFixture);
                if(dup.is_valid()){ Fixture::select(selectedPatch, dup); }
                selectedPatch.add<Patch::DmxMapDirty>(); selectedPatch.add<Patch::RenderAreaDirty>();
            }
            if(!hasSel) ImGui::EndDisabled();
            ImGui::SameLine();
            if(ImGui::Button("Auto-Pack DMX")) Fixture::autoPackDmx(selectedPatch);
            ImGui::SameLine();
            if(!hasSel) ImGui::BeginDisabled();
            ImGui::PushStyleColor(ImGuiCol_Button,        {0.5f,0.1f,0.1f,1});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.7f,0.15f,0.15f,1});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.4f,0.05f,0.05f,1});
            if(ImGui::Button("Remove")){
                auto p = Fixture::getPatch(selectedFixture);
                selectedFixture.destruct();
                if(p.is_valid()){ Fixture::clearSelection(p); msClear(p); p.add<Patch::DmxMapDirty>(); p.add<Patch::RenderAreaDirty>(); }
            }
            ImGui::PopStyleColor(3);
            if(!hasSel) ImGui::EndDisabled();
            ImGui::PopStyleVar();
            ImGui::Separator();

            if(ImGui::BeginTable("FixturesTable", 2, ImGuiTableFlags_Resizable)){
                ImGui::TableNextRow();
                // Fixture list
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("Fixture List");
                if(ImGui::BeginListBox("##Fixtures", ImGui::GetContentRegionAvail())){
                    static flecs::id_t renamingId = 0;
                    static char renameBuf[64] = {};
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
        if (!showPatchEditor) return;
        auto app             = App::get(it.world());
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

        if(ImGui::Begin("Patch Editor", &showPatchEditor,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::Checkbox("Lock", &patchLocked); ImGui::SameLine();
            ImGui::Checkbox("Grid", &b_showGrid); ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::SliderFloat("Opacity", &previewOpacity, 0.0f, 1.0f, "%.2f"); ImGui::SameLine();
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

                if (b_showGrid) {
                    canvas.drawGrid(100.0, 0xFF333333, 0xFF000000);
                } else {
                    drawing->AddRectFilled(canvas.frameMin, canvas.frameMax, 0xFF000000);
                }

                if(selectedPatch.is_valid()){

                    // ── VFB preview texture ──
                    if(b_showFrame){
                        App::patchProgramLock.lock();
                        if(App::currentPatchProgram){
                            int tw = App::currentPatchProgram->vfbWidth;
                            int th = App::currentPatchProgram->vfbHeight;
                            GLuint texID = 0;
                            if(App::currentPatchProgram->renderMode == Patch::RenderMode::GLSL){
                                if (App::currentPatchProgram->crossfadeProgress < 1.0f && App::currentPatchProgram->glslFboTexBlend != 0) {
                                    texID = App::currentPatchProgram->glslFboTexBlend;
                                } else {
                                    texID = App::currentPatchProgram->glslFboTex;
                                }
                            } else if(App::currentPatchProgram->vfbPixels){
                                if(vfbPreviewTexId == 0) glGenTextures(1, &vfbPreviewTexId);
                                glBindTexture(GL_TEXTURE_2D, vfbPreviewTexId);
                                if(currentVfbWidth != tw || currentVfbHeight != th){
                                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th, 0,
                                                 GL_RGBA, GL_UNSIGNED_BYTE, App::currentPatchProgram->vfbPixels);
                                    currentVfbWidth = tw; currentVfbHeight = th;
                                } else {
                                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tw, th,
                                                    GL_RGBA, GL_UNSIGNED_BYTE, App::currentPatchProgram->vfbPixels);
                                }
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                                glBindTexture(GL_TEXTURE_2D, 0);
                                texID = vfbPreviewTexId;
                            }
                            if(const auto* ra = selectedPatch.try_get<Patch::RenderArea>()){
                                glm::vec2 pMin = canvas.canvasToScreen(glm::vec2(ra->min));
                                glm::vec2 pMax = canvas.canvasToScreen(glm::vec2(ra->max));
                                ImU32 tintCol = IM_COL32(255, 255, 255, (int)(previewOpacity * 255));
                                if(texID) drawing->AddImage((ImTextureID)(intptr_t)texID, pMin, pMax, ImVec2(0,0), ImVec2(1,1), tintCol);
                                else      drawing->AddRectFilled(pMin, pMax, IM_COL32(0, 0, 0, (int)(previewOpacity * 0.4f * 255)));
                            }
                        }
                        App::patchProgramLock.unlock();
                    } else if(const auto* ra = selectedPatch.try_get<Patch::RenderArea>()){
                        drawing->AddRectFilled(canvas.canvasToScreen(glm::vec2(ra->min)),
                                               canvas.canvasToScreen(glm::vec2(ra->max)), IM_COL32(0, 0, 0, (int)(previewOpacity * 0.27f * 255)));
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
                        App::patchProgramLock.lock();
                        if (App::currentPatchProgram && App::currentPatchProgram->pixelColors) {
                            tempColors.assign(App::currentPatchProgram->pixelColors,
                                              App::currentPatchProgram->pixelColors + App::currentPatchProgram->pixelCount);
                            hasColors = true;
                        }
                        App::patchProgramLock.unlock();

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
                    if (!patchLocked) {
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

                    if(!patchLocked && selectedFixture.is_valid() && b_showFixtures && !ImGui::IsKeyDown(ImGuiKey_Space) && !fixtureDragging){
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
                    // Only act when the canvas background was clicked (canvasClicked),
                    // not when a drag handle consumed the click.
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
                            if (!patchLocked) {
                                fixtureDragging = true;
                                fixtureHasDragged = false;
                                lastDragMouseCanvas = mCanvas;
                            }

                            // If it's not selected yet, select it immediately on click down so dragging works immediately
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
                            // Clicked empty space — start marquee (canvasActive will track drag)
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

                                // Lambda to move a fixture
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

                                // Move all selected fixtures
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
                            // Mouse released
                            if(!fixtureHasDragged && clickedFixture.is_valid()){
                                // Toggle selection or update selection since it was a click, not a drag
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
                    // canvasActive is true only while the user is dragging the BACKGROUND
                    // (the InvisibleButton). It is false when dragging a child handle button.
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
                            // Mouse released — commit selection
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

                    // Cancel marquee if mouse was released outside
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
        if (!showArtnetData) return;
        auto app             = App::get(it.world());
        auto selectedPatch   = Patch::getSelected(app);
        auto selectedFixture = Fixture::getSelected(selectedPatch);
        auto selectedUniverse = Artnet::Universe::getSelected(selectedPatch);

        if(ImGui::Begin("Artnet Data", &showArtnetData)){
            if(selectedPatch.is_valid()){
                // Get all universes in sorted order
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
                        // MappedField: {Name, Offset, Count, Color, b_selected}
                        fields.push_back({fixture.name().c_str(), offset, count, 0, (fixture == selectedFixture)});
                });
                std::sort(fields.begin(), fields.end(), [](const MappedField& a, const MappedField& b){ return a.Offset < b.Offset; });
                for(int i = 0; i < (int)fields.size(); i++)
                    fields[i].Color = fields[i].b_selected ? IM_COL32(127,127,0,255) : colors[i%2];
                uint8_t localChannels[512] = {0};
                bool channelsCopied = false;
                if (univProps) {
                    App::patchProgramLock.lock();
                    if (App::currentPatchProgram) {
                        for (int ui = 0; ui < App::currentPatchProgram->universeCount; ++ui) {
                            if (App::currentPatchProgram->universes[ui].id == univProps->universeId) {
                                std::memcpy(localChannels, App::currentPatchProgram->universes[ui].buffer, 512);
                                channelsCopied = true;
                                break;
                            }
                        }
                    }
                    App::patchProgramLock.unlock();
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
        if (!showNetworkSettings) return;
        auto app          = App::get(it.world());
        auto selectedPatch = Patch::getSelected(app);
        if(ImGui::Begin("Patch & Network Settings", &showNetworkSettings)){
            if(selectedPatch.is_valid()){
                if(auto* s = selectedPatch.try_get_mut<Patch::Settings>()){
                    bool e = false;
                    ImGui::SeparatorText("Network");
                    e |= ImGui::Checkbox("Enable ArtNet Sending", &s->networkEnabled);
                    int sp = s->sourcePort;
                    if(ImGui::InputInt("Source Port", &sp)){ s->sourcePort = std::clamp(sp,1,65535); e = true; }
                    ImGui::SeparatorText("Timing");
                    e |= ImGui::SliderFloat("Refresh Rate (Hz)", &s->refreshRate, 1.f, 120.f, "%.1f Hz");
                    if(e) selectedPatch.add<Patch::ProgramDirty>();
                }
            } else ImGui::TextDisabled("No patch selected.");
        }
        ImGui::End();
    });

    // ─────────────── Artnet Devices Window ───────────────────────
    w.system<>("WindowArtnetDevices").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        if (!showArtnetDevices) return;
        auto app               = App::get(it.world());
        auto selectedPatch     = Patch::getSelected(app);
        auto selectedDev       = Artnet::Device::getSelected(selectedPatch);

        if(ImGui::Begin("Artnet Devices", &showArtnetDevices)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            if(ImGui::Button("Add Device")){
                auto dev = Artnet::Device::create(selectedPatch);
                static int cnt = 1;
                dev.set_name(("Device " + std::to_string(cnt++)).c_str());
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
        if (!showScriptEditor) return;
        auto app          = App::get(it.world());
        auto selectedPatch = Patch::getSelected(app);

        if(!editorsInitialized){
            luaEditor  = std::make_unique<TextEditor>();
            glslEditor = std::make_unique<TextEditor>();
            luaEditor->SetLanguage(TextEditor::Language::Lua());
            glslEditor->SetLanguage(TextEditor::Language::Glsl());
            editorsInitialized = true;
        }

        if(ImGui::Begin("Script & Shader Editor", &showScriptEditor)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            auto* settings   = selectedPatch.try_get_mut<Patch::Settings>();
            auto* scriptData = selectedPatch.try_get_mut<Patch::ScriptData>();
            auto* cueList    = selectedPatch.try_get_mut<CueList::List>();
            auto* bank       = selectedPatch.try_get_mut<EffectBank::Bank>();
            if(!settings || !scriptData){ ImGui::End(); return; }

            // ── Target Syncing and Change Detection ──
            static flecs::id_t lastPatchId = 0;
            static int lastEditingCueIndex = -99;
            static int lastEditingBankIndex = -99;
            static std::string lastEditorText = "";

            bool targetChanged = (selectedPatch.id() != lastPatchId) || 
                                 (editingCueIndex != lastEditingCueIndex) || 
                                 (editingBankIndex != lastEditingBankIndex);

            if (targetChanged) {
                lastPatchId = selectedPatch.id();
                lastEditingCueIndex = editingCueIndex;
                lastEditingBankIndex = editingBankIndex;
                
                std::string targetText = "";
                if (editingCueIndex == -1) {
                    targetText = scriptData->glslSource;
                } else if (editingCueIndex >= 0 && cueList && editingCueIndex < (int)cueList->cues.size()) {
                    targetText = cueList->cues[editingCueIndex].glslSource;
                } else if (editingCueIndex == -2 && bank && editingBankIndex >= 0 && editingBankIndex < (int)bank->effects.size()) {
                    targetText = bank->effects[editingBankIndex].glslSource;
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
                if (editingCueIndex == -1) {
                    scriptData->glslSource = newSource;
                    if (settings) {
                        try {
                            std::ofstream f(settings->shaderPath);
                            if (f) f << newSource;
                        } catch (...) {}
                    }
                } else if (editingCueIndex >= 0 && cueList && editingCueIndex < (int)cueList->cues.size()) {
                    cueList->cues[editingCueIndex].glslSource = newSource;
                } else if (editingCueIndex == -2 && bank && editingBankIndex >= 0 && editingBankIndex < (int)bank->effects.size()) {
                    bank->effects[editingBankIndex].glslSource = newSource;
                }
                selectedPatch.add<Patch::ProgramDirty>();
                hasUncompiledChanges = false;
            }

            // ── Editor Header Row ──
            if (editingCueIndex == -1) {
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
            } else if (editingCueIndex >= 0 && cueList && editingCueIndex < (int)cueList->cues.size()) {
                CueList::Cue& cue = cueList->cues[editingCueIndex];
                ImGui::Text("Editing Cue %d:", editingCueIndex + 1); ImGui::SameLine();
                char cueName[64];
                std::strncpy(cueName, cue.name.c_str(), sizeof(cueName)-1);
                ImGui::SetNextItemWidth(120);
                if (ImGui::InputText("Name", cueName, sizeof(cueName))) {
                    cue.name = cueName;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                ImGui::InputFloat("Hold", &cue.holdSeconds, 0.0f, 0.0f, "%.1fs");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                ImGui::InputFloat("Fade", &cue.fadeSeconds, 0.0f, 0.0f, "%.1fs");
                ImGui::SameLine();
                if (ImGui::Button("Release")) {
                    setEditingCueIndex(-1);
                }
            } else if (editingCueIndex == -2 && bank && editingBankIndex >= 0 && editingBankIndex < (int)bank->effects.size()) {
                EffectBank::Effect& fx = bank->effects[editingBankIndex];
                ImGui::Text("Editing Bank Effect %d:", editingBankIndex + 1); ImGui::SameLine();
                char fxName[64];
                std::strncpy(fxName, fx.name.c_str(), sizeof(fxName)-1);
                ImGui::SetNextItemWidth(150);
                if (ImGui::InputText("Name", fxName, sizeof(fxName))) {
                    fx.name = fxName;
                }
                ImGui::SameLine();
                if (ImGui::Button("Add to Cue List")) {
                    CueList::Cue cue;
                    cue.name = fx.name;
                    cue.glslSource = fx.glslSource;
                    cue.holdSeconds = 5.0f;
                    cue.fadeSeconds = 2.0f;
                    cueList->cues.push_back(cue);
                    selectedPatch.add<Patch::ProgramDirty>();
                }
                ImGui::SameLine();
                if (ImGui::Button("Release")) {
                    setEditingCueIndex(-1);
                }
            }

            ImGui::SameLine();
            static int presetIdx = -1;
            auto presetGetter = [](void*, int i, const char** out) -> bool { *out = kPresets[i].name; return true; };
            ImGui::SetNextItemWidth(130);
            if(ImGui::Combo("Preset", &presetIdx, presetGetter, nullptr, kPresetCount) && presetIdx >= 0){
                std::string presetGlsl = kPresets[presetIdx].glsl;
                glslEditor->SetText(presetGlsl);
                if (editingCueIndex == -1) {
                    scriptData->glslSource = presetGlsl;
                    if (settings) {
                        try {
                            std::ofstream f(settings->shaderPath);
                            if (f) f << presetGlsl;
                        } catch (...) {}
                    }
                } else if (editingCueIndex >= 0 && cueList && editingCueIndex < (int)cueList->cues.size()) {
                    cueList->cues[editingCueIndex].glslSource = presetGlsl;
                } else if (editingCueIndex == -2 && bank && editingBankIndex >= 0 && editingBankIndex < (int)bank->effects.size()) {
                    bank->effects[editingBankIndex].glslSource = presetGlsl;
                }
                selectedPatch.add<Patch::ProgramDirty>();
                hasUncompiledChanges = false;
            }

            ImGui::Separator();

            // ── Editor fills remaining space minus log area ──
            const float kLogHeight = 100.f;
            const float kSepHeight = ImGui::GetStyle().ItemSpacing.y + 1.f;
            float editorHeight = ImGui::GetContentRegionAvail().y - kLogHeight - kSepHeight * 3.f - ImGui::GetTextLineHeightWithSpacing();
            if(editorHeight < 80.f) editorHeight = 80.f;

            glslEditor->Render("GlslEd", ImVec2(0, editorHeight));

            ImGui::Separator();
            // Status bar
            if (hasUncompiledChanges) {
                ImGui::TextColored({1.0f, 0.5f, 0.0f, 1.0f}, "● Unsaved Changes (compiling...)");
            } else {
                ImGui::TextColored({0.2f, 1.0f, 0.2f, 1.0f}, "● Saved & Compiled");
            }

            ImGui::Separator();
            ImGui::Text("Compilation Log:");
            ImGui::BeginChild("##Log", ImVec2(0, kLogHeight), true);
            std::string displayLog = "";
            App::patchProgramLock.lock();
            if (App::currentPatchProgram) {
                if (editingCueIndex == -1) {
                    displayLog = App::currentPatchProgram->defaultCompilerLog;
                } else if (editingCueIndex >= 0 && editingCueIndex < (int)App::currentPatchProgram->compiledCues.size()) {
                    displayLog = App::currentPatchProgram->compiledCues[editingCueIndex].compilerLog;
                } else if (editingCueIndex == -2 && editingBankIndex >= 0 && editingBankIndex < (int)App::currentPatchProgram->compiledBankEffects.size()) {
                    displayLog = App::currentPatchProgram->compiledBankEffects[editingBankIndex].compilerLog;
                }
            }
            App::patchProgramLock.unlock();

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

    // ─────────────── Cue List Window ─────────────────────────────
    w.system<>("WindowCues").kind(flecs::OnStore)
    .run([&](flecs::iter& it){
        if (!showCuesWindow) return;
        auto app          = App::get(it.world());
        auto selectedPatch = Patch::getSelected(app);

        if(ImGui::Begin("Cue List", &showCuesWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            if(!selectedPatch.has<CueList::List>()) selectedPatch.set<CueList::List>({});
            auto* cueList    = selectedPatch.try_get_mut<CueList::List>();
            if(!cueList){ ImGui::End(); return; }

            // ── Transport ──
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.f);
            ImGui::Checkbox("▶ Auto-advance", &cueList->autoAdvance); ImGui::SameLine();
            ImGui::Checkbox("↺ Loop",         &cueList->loop);

            auto triggerCue = [&](int idx){
                if(idx < 0 || idx >= (int)cueList->cues.size()) return;
                const CueList::Cue& cue = cueList->cues[idx];
                cueList->activeIndex = idx;
                cueList->holdTimer   = 0.f;
                App::patchProgramLock.lock();
                if (App::currentPatchProgram) {
                    App::currentPatchProgram->activeCueIndex = idx;
                    App::currentPatchProgram->pendingCrossfadeDuration = cue.fadeSeconds;
                }
                App::patchProgramLock.unlock();
            };

            ImGui::SameLine();
            if(ImGui::Button("⏮ Prev")){
                if(!cueList->cues.empty()){
                    int prev = cueList->activeIndex - 1;
                    if(prev < 0) prev = cueList->loop ? (int)cueList->cues.size()-1 : 0;
                    triggerCue(prev);
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("⏭ Next")){
                if(!cueList->cues.empty()){
                    int next = cueList->activeIndex + 1;
                    if(next >= (int)cueList->cues.size()) next = cueList->loop ? 0 : (int)cueList->cues.size()-1;
                    triggerCue(next);
                }
            }

            // Crossfade in progress indicator
            if(App::patchProgramLock.try_lock()){
                if(App::currentPatchProgram && App::currentPatchProgram->crossfadeProgress < 1.f){
                    float p = App::currentPatchProgram->crossfadeProgress;
                    App::patchProgramLock.unlock();
                    ImGui::SameLine();
                    ImGui::TextColored({0.4f,0.8f,1.f,1.f}, "Crossfade: %.0f%%", p * 100.f);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(120.f);
                    ImGui::ProgressBar(p, ImVec2(120.f, 0));
                } else {
                    App::patchProgramLock.unlock();
                }
            }

            ImGui::Separator();

            if(ImGui::Button("+ Add Cue")){
                CueList::Cue cue;
                cue.name       = "Cue " + std::to_string(cueList->cues.size() + 1);
                auto* scriptData = selectedPatch.try_get<Patch::ScriptData>();
                cue.glslSource = scriptData ? scriptData->glslSource : "";
                cue.holdSeconds = 5.f;
                cue.fadeSeconds = 2.f;
                cueList->cues.push_back(cue);
                if(cueList->activeIndex < 0) cueList->activeIndex = 0;
                selectedPatch.add<Patch::ProgramDirty>();
            }
            ImGui::PopStyleVar();
            ImGui::Separator();

            // ── Cue list ──
            if (ImGui::BeginChild("##CueItems", ImVec2(0, 0), true)) {
                if (ImGui::BeginTable("CueTable", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Status/Name", ImGuiTableColumnFlags_WidthStretch, 2.0f);
                    ImGui::TableSetupColumn("Hold", ImGuiTableColumnFlags_WidthFixed, 80.f);
                    ImGui::TableSetupColumn("Fade", ImGuiTableColumnFlags_WidthFixed, 80.f);
                    ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableSetupColumn("Remove", ImGuiTableColumnFlags_WidthFixed, 60.f);
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < (int)cueList->cues.size(); i++) {
                        CueList::Cue& cue = cueList->cues[i];
                        bool isActive = (i == cueList->activeIndex);
                        bool isEditing = (editingCueIndex == i);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();

                        ImGui::PushID(i);

                        // Bullet or Play indicator
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

                        // Selectable name triggers cue execution (start cue)
                        char label[128];
                        std::snprintf(label, sizeof(label), "%s##select", cue.name.c_str());
                        if (ImGui::Selectable(label, isActive, ImGuiSelectableFlags_SpanAllColumns)) {
                            triggerCue(i);
                        }

                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(70);
                        ImGui::InputFloat("##h", &cue.holdSeconds, 0.0f, 0.0f, "%.1fs");

                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(70);
                        ImGui::InputFloat("##f", &cue.fadeSeconds, 0.0f, 0.0f, "%.1fs");

                        ImGui::TableNextColumn();
                        if (isEditing) {
                            ImGui::PushStyleColor(ImGuiCol_Button, {0.8f, 0.5f, 0.1f, 1.0f});
                            if (ImGui::Button("Edit")) {
                                setEditingCueIndex(-1); // Toggle edit off
                            }
                            ImGui::PopStyleColor();
                        } else {
                            if (ImGui::Button("Edit")) {
                                setEditingCueIndex(i);
                            }
                        }

                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Button, {0.6f, 0.1f, 0.1f, 1.0f});
                        if (ImGui::Button("Remove")) {
                            cueList->cues.erase(cueList->cues.begin() + i);
                            if (cueList->activeIndex == i) {
                                cueList->activeIndex = std::clamp(cueList->activeIndex, -1, (int)cueList->cues.size() - 1);
                            } else if (cueList->activeIndex > i) {
                                cueList->activeIndex--;
                            }
                            if (editingCueIndex == i) {
                                setEditingCueIndex(-1);
                            } else if (editingCueIndex > i) {
                                setEditingCueIndex(editingCueIndex - 1);
                            }
                            i--; // Adjust loop counter since we removed an item
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
        if (!showEffectBankWindow) return;
        auto app          = App::get(it.world());
        auto selectedPatch = Patch::getSelected(app);

        if(ImGui::Begin("Effect Bank", &showEffectBankWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            if(!selectedPatch.has<EffectBank::Bank>()) selectedPatch.set<EffectBank::Bank>({});
            auto* bank    = selectedPatch.try_get_mut<EffectBank::Bank>();
            auto* cueList = selectedPatch.try_get_mut<CueList::List>();
            if(!bank || !cueList){ ImGui::End(); return; }

            if(ImGui::Button("+ Add Experimental Effect")){
                EffectBank::Effect fx;
                fx.name = "Effect " + std::to_string(bank->effects.size() + 1);
                fx.glslSource = 
                    "#version 150\n"
                    "in vec2 uv;\n"
                    "out vec4 fragColor;\n"
                    "uniform float time;\n"
                    "uniform vec2 resolution;\n"
                    "void main() {\n"
                    "    fragColor = vec4(uv.x, uv.y, sin(time)*0.5+0.5, 1.0);\n"
                    "}\n";
                bank->effects.push_back(fx);
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

                    for (int i = 0; i < (int)bank->effects.size(); i++) {
                        EffectBank::Effect& fx = bank->effects[i];
                        bool isEditing = (editingCueIndex == -2 && editingBankIndex == i);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();

                        ImGui::PushID(i);

                        if (isEditing) {
                            ImGui::TextColored({1.0f, 0.7f, 0.2f, 1.0f}, "✎ %s", fx.name.c_str());
                        } else {
                            ImGui::Text("%s", fx.name.c_str());
                        }

                        ImGui::TableNextColumn();
                        if (ImGui::Button("Add to Cue List")) {
                            CueList::Cue cue;
                            cue.name = fx.name;
                            cue.glslSource = fx.glslSource;
                            cue.holdSeconds = 5.0f;
                            cue.fadeSeconds = 2.0f;
                            cueList->cues.push_back(cue);
                            selectedPatch.add<Patch::ProgramDirty>();
                        }

                        ImGui::TableNextColumn();
                        if (isEditing) {
                            ImGui::PushStyleColor(ImGuiCol_Button, {0.8f, 0.5f, 0.1f, 1.0f});
                            if (ImGui::Button("Edit")) {
                                setEditingCueIndex(-1); // Toggle edit off
                            }
                            ImGui::PopStyleColor();
                        } else {
                            if (ImGui::Button("Edit")) {
                                setEditingBankIndex(i);
                            }
                        }

                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Button, {0.6f, 0.1f, 0.1f, 1.0f});
                        if (ImGui::Button("Remove")) {
                            bank->effects.erase(bank->effects.begin() + i);
                            if (editingCueIndex == -2 && editingBankIndex == i) {
                                setEditingCueIndex(-1);
                            } else if (editingCueIndex == -2 && editingBankIndex > i) {
                                setEditingBankIndex(editingBankIndex - 1);
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
        if (!showOfflinePreviewWindow) return;
        auto app          = App::get(it.world());
        auto selectedPatch = Patch::getSelected(app);

        if(ImGui::Begin("Offline Shader Preview", &showOfflinePreviewWindow)){
            if(!selectedPatch.is_valid()){ ImGui::TextDisabled("No patch."); ImGui::End(); return; }

            App::patchProgramLock.lock();
            if(App::currentPatchProgram && App::currentPatchProgram->glslEditorFboTex != 0){
                GLuint tex = App::currentPatchProgram->glslEditorFboTex;
                float tw = App::currentPatchProgram->vfbWidth;
                float th = App::currentPatchProgram->vfbHeight;
                App::patchProgramLock.unlock();

                ImVec2 avail = ImGui::GetContentRegionAvail();
                float scale = std::min(avail.x / tw, avail.y / th);
                if (scale < 0.1f) scale = 0.1f;
                ImVec2 imgSize(tw * scale, th * scale);

                ImGui::SetCursorPosX((avail.x - imgSize.x) * 0.5f + ImGui::GetCursorPosX());
                ImGui::SetCursorPosY((avail.y - imgSize.y) * 0.5f + ImGui::GetCursorPosY());

                // OpenGL textures are y-up, so flip UV coordinates to show upright in ImGui
                ImGui::Image((ImTextureID)(intptr_t)tex, imgSize, ImVec2(0,1), ImVec2(1,0));
            } else {
                App::patchProgramLock.unlock();
                ImGui::TextDisabled("No offline shader preview available.");
            }
        }
        ImGui::End();
    });

} // import()

} // namespace PixelMapper::Gui