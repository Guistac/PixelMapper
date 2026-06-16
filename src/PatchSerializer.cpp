#include "PatchSerializer.h"
#include "App.h"
#include "Patch.h"
#include "Fixture.h"
#include "Artnet.h"
#include "Shape.h"
#include "CueList.h"
#include "EffectBank.h"


#include <tinyxml2.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>

namespace PixelMapper {
namespace PatchSerializer {

// ─────────────────────────── HELPERS ────────────────────────────

static std::string ipToString(uint32_t ip) {
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&ip);
    return std::to_string(b[0]) + "." + std::to_string(b[1]) + "."
         + std::to_string(b[2]) + "." + std::to_string(b[3]);
}

static uint32_t ipFromString(const char* s) {
    uint32_t ip = 0;
    uint8_t* b  = reinterpret_cast<uint8_t*>(&ip);
    if (s) {
        std::sscanf(s, "%hhu.%hhu.%hhu.%hhu", &b[0], &b[1], &b[2], &b[3]);
    }
    return ip;
}

static std::string readFileToString(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void writeStringToFile(const std::string& path, const std::string& content) {
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    std::ofstream f(path);
    if (f.is_open()) f << content;
}

// Encode/decode multiline script source as XML CDATA text
static void setElementText(tinyxml2::XMLDocument& doc, tinyxml2::XMLElement* el,
                            const std::string& text) {
    auto* cdata = doc.NewText(text.c_str());
    cdata->SetCData(true);
    el->InsertEndChild(cdata);
}

static std::string getElementText(tinyxml2::XMLElement* el) {
    if (!el) return "";
    const char* t = el->GetText();
    return t ? t : "";
}

// ─────────────────────────── SAVE ────────────────────────────────

bool save(flecs::entity pixelMapper, const std::string& path) {
    using namespace tinyxml2;

    XMLDocument doc;
    XMLDeclaration* decl = doc.NewDeclaration();
    doc.InsertFirstChild(decl);

    XMLElement* root = doc.NewElement("PixelMapper");
    doc.InsertEndChild(root);

    // ── Save UIConfig ──
    if (const auto* config = pixelMapper.try_get<App::UIConfig>()) {
        XMLElement* uiEl = doc.NewElement("UIConfig");
        uiEl->SetAttribute("currentLayout", config->currentLayout);
        uiEl->SetAttribute("showFixturesWindow", config->showFixturesWindow ? 1 : 0);
        uiEl->SetAttribute("showPatchEditor", config->showPatchEditor ? 1 : 0);
        uiEl->SetAttribute("showArtnetData", config->showArtnetData ? 1 : 0);
        uiEl->SetAttribute("showNetworkSettings", config->showNetworkSettings ? 1 : 0);
        uiEl->SetAttribute("showArtnetDevices", config->showArtnetDevices ? 1 : 0);
        uiEl->SetAttribute("showScriptEditor", config->showScriptEditor ? 1 : 0);
        uiEl->SetAttribute("showCuesWindow", config->showCuesWindow ? 1 : 0);
        uiEl->SetAttribute("showOfflinePreviewWindow", config->showOfflinePreviewWindow ? 1 : 0);
        uiEl->SetAttribute("showEffectBankWindow", config->showEffectBankWindow ? 1 : 0);
        uiEl->SetAttribute("patchLocked", config->patchLocked ? 1 : 0);
        uiEl->SetAttribute("previewOpacity", config->previewOpacity);
        uiEl->SetAttribute("showGrid", config->showGrid ? 1 : 0);
        uiEl->SetAttribute("showFixtures", config->showFixtures ? 1 : 0);
        uiEl->SetAttribute("showPixels", config->showPixels ? 1 : 0);
        uiEl->SetAttribute("showFrame", config->showFrame ? 1 : 0);
        uiEl->SetAttribute("autoZoom", config->autoZoom ? 1 : 0);
        uiEl->SetAttribute("editingCueIndex", config->editingCueIndex);
        uiEl->SetAttribute("editingBankIndex", config->editingBankIndex);
        root->InsertEndChild(uiEl);
    }

    Patch::iterate(pixelMapper, [&](flecs::entity patch) {
        XMLElement* patchEl = doc.NewElement("Patch");
        patchEl->SetAttribute("name", patch.name().c_str());
        root->InsertEndChild(patchEl);

        // ── Settings ──
        if (const auto* settings = patch.try_get<Patch::Settings>()) {
            XMLElement* sEl = doc.NewElement("Settings");
            sEl->SetAttribute("refreshRate",    settings->refreshRate);
            sEl->SetAttribute("networkEnabled", settings->networkEnabled ? 1 : 0);
            sEl->SetAttribute("sourcePort",     (int)settings->sourcePort);
            sEl->SetAttribute("renderMode",     (int)settings->renderMode);
            sEl->SetAttribute("vfbResolution",  settings->vfbResolution);
            sEl->SetAttribute("luaScriptPath",  settings->luaScriptPath);
            sEl->SetAttribute("shaderPath",     settings->shaderPath);
            patchEl->InsertEndChild(sEl);
        }

        // ── Script sources ──
        if (const auto* sd = patch.try_get<Patch::ScriptData>()) {
            XMLElement* luaEl = doc.NewElement("LuaSource");
            setElementText(doc, luaEl, sd->luaSource);
            patchEl->InsertEndChild(luaEl);

            XMLElement* glslEl = doc.NewElement("GlslSource");
            setElementText(doc, glslEl, sd->glslSource);
            patchEl->InsertEndChild(glslEl);
        }

        // ── Fixtures ──
        XMLElement* fixturesEl = doc.NewElement("Fixtures");
        patchEl->InsertEndChild(fixturesEl);

        Fixture::iterateWithDmx(patch,
            [&](flecs::entity fixture, const Fixture::Layout& layout,
                const Fixture::DmxAddress& dmx)
        {
            XMLElement* fEl = doc.NewElement("Fixture");
            fEl->SetAttribute("name",             fixture.name().c_str());
            fEl->SetAttribute("pixelCount",        layout.pixelCount);
            fEl->SetAttribute("channelsPerPixel",  layout.channelsPerPixel);
            fEl->SetAttribute("dmxUniverse",       (int)dmx.universe);
            fEl->SetAttribute("dmxAddress",        (int)dmx.address);

            flecs::entity shapeType = fixture.target<Fixture::WithShape>();
            if (shapeType == fixture.world().id<Shape::Line>()) {
                const Shape::Line& l = fixture.get<Fixture::WithShape, Shape::Line>();
                fEl->SetAttribute("shape",    "Line");
                fEl->SetAttribute("startX",   l.start.x);
                fEl->SetAttribute("startY",   l.start.y);
                fEl->SetAttribute("startZ",   l.start.z);
                fEl->SetAttribute("endX",     l.end.x);
                fEl->SetAttribute("endY",     l.end.y);
                fEl->SetAttribute("endZ",     l.end.z);
            } else if (shapeType == fixture.world().id<Shape::Circle>()) {
                const Shape::Circle& c = fixture.get<Fixture::WithShape, Shape::Circle>();
                fEl->SetAttribute("shape",    "Circle");
                fEl->SetAttribute("centerX",  c.center.x);
                fEl->SetAttribute("centerY",  c.center.y);
                fEl->SetAttribute("centerZ",  c.center.z);
                fEl->SetAttribute("radius",   c.radius);
            }

            fixturesEl->InsertEndChild(fEl);
        });

        // ── ArtNet Devices ──
        XMLElement* devicesEl = doc.NewElement("ArtNetDevices");
        patchEl->InsertEndChild(devicesEl);

        Artnet::Device::iterateInPatch(patch,
            [&](flecs::entity device, const Artnet::Device::Settings& ds)
        {
            XMLElement* dEl = doc.NewElement("Device");
            dEl->SetAttribute("name",           device.name().c_str());
            dEl->SetAttribute("ip",             ipToString(ds.ipAddress).c_str());
            dEl->SetAttribute("startUniverse",  (int)ds.startUniverse);
            dEl->SetAttribute("universeCount",  (int)ds.universeCount);
            devicesEl->InsertEndChild(dEl);
        });

        // ── Map effects to unique IDs ──
        std::unordered_map<flecs::id_t, int> effectToId;
        std::vector<flecs::entity> effects;
        flecs::entity bankFolder = patch.target<EffectBank::EffectFolder>();
        if (bankFolder.is_valid()) {
            bankFolder.children([&](flecs::entity child) {
                if (child.has<EffectBank::Effect::Is>()) {
                    effectToId[child.id()] = (int)effects.size();
                    effects.push_back(child);
                }
            });
        }

        // ── Cue List ──
        flecs::entity cueFolder = patch.target<CueList::CueFolder>();
        if (cueFolder.is_valid()) {
            if (const auto* session = cueFolder.try_get<CueList::SessionState>()) {
                XMLElement* clEl = doc.NewElement("CueList");
                clEl->SetAttribute("autoAdvance", session->autoAdvance ? 1 : 0);
                clEl->SetAttribute("loop",        session->loop ? 1 : 0);
                clEl->SetAttribute("activeIndex", session->activeIndex);
                patchEl->InsertEndChild(clEl);

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

                for (const auto& entry : cues) {
                    XMLElement* cueEl = doc.NewElement("Cue");
                    cueEl->SetAttribute("name", entry.entity.name().c_str());
                    float hold = 5.0f;
                    if (const auto* h = entry.entity.try_get<CueList::Cue::HoldDuration>()) hold = h->value;
                    float fade = 0.0f;
                    if (const auto* f = entry.entity.try_get<CueList::Cue::FadeDuration>()) fade = f->value;
                    cueEl->SetAttribute("holdSeconds", hold);
                    cueEl->SetAttribute("fadeSeconds", fade);

                    flecs::entity targetFx = entry.entity.target<CueList::Cue::TargetEffect>();
                    if (targetFx.is_valid() && effectToId.count(targetFx.id())) {
                        cueEl->SetAttribute("targetEffectId", effectToId[targetFx.id()]);
                    }
                    
                    clEl->InsertEndChild(cueEl);
                }
            }
        }

        // ── Effect Bank ──
        if (bankFolder.is_valid()) {
            XMLElement* ebEl = doc.NewElement("EffectBank");
            if (const auto* session = bankFolder.try_get<EffectBank::SessionState>()) {
                ebEl->SetAttribute("activeIndex", session->activeIndex);
            }
            patchEl->InsertEndChild(ebEl);

            for (size_t i = 0; i < effects.size(); ++i) {
                flecs::entity fx = effects[i];
                XMLElement* fxEl = doc.NewElement("Effect");
                fxEl->SetAttribute("name", fx.name().c_str());
                fxEl->SetAttribute("id", (int)i);

                std::string glsl = "";
                if (const auto* g = fx.try_get<EffectBank::Effect::GlslSource>()) glsl = g->value;
                setElementText(doc, fxEl, glsl);
                ebEl->InsertEndChild(fxEl);
            }
        }
    });

    // Ensure parent directory exists
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());

    XMLError err = doc.SaveFile(path.c_str());
    if (err != XML_SUCCESS) {
        std::cerr << "[PatchSerializer] Save failed: " << doc.ErrorStr() << "\n";
        return false;
    }
    std::cout << "[PatchSerializer] Saved to " << path << "\n";
    return true;
}

// ─────────────────────────── LOAD ────────────────────────────────

bool load(flecs::entity pixelMapper, const std::string& path) {
    using namespace tinyxml2;

    XMLDocument doc;
    XMLError err = doc.LoadFile(path.c_str());
    if (err != XML_SUCCESS) {
        std::cerr << "[PatchSerializer] Load failed (" << path << "): "
                  << doc.ErrorStr() << "\n";
        return false;
    }

    XMLElement* root = doc.FirstChildElement("PixelMapper");
    if (!root) {
        std::cerr << "[PatchSerializer] No <PixelMapper> root element.\n";
        return false;
    }

    // ── Load UIConfig ──
    if (XMLElement* uiEl = root->FirstChildElement("UIConfig")) {
        auto* config = &pixelMapper.get_mut<App::UIConfig>();
        if (config) {
            uiEl->QueryIntAttribute("currentLayout", &config->currentLayout);
            int sf=1, sp=1, sd=1, sn=1, sdev=1, sse=0, sc=0, sop=0, seb=0, pl=0, sg=1;
            int sfix=1, spix=1, sfrm=1, sazm=0;
            uiEl->QueryIntAttribute("showFixturesWindow", &sf);
            uiEl->QueryIntAttribute("showPatchEditor", &sp);
            uiEl->QueryIntAttribute("showArtnetData", &sd);
            uiEl->QueryIntAttribute("showNetworkSettings", &sn);
            uiEl->QueryIntAttribute("showArtnetDevices", &sdev);
            uiEl->QueryIntAttribute("showScriptEditor", &sse);
            uiEl->QueryIntAttribute("showCuesWindow", &sc);
            uiEl->QueryIntAttribute("showOfflinePreviewWindow", &sop);
            uiEl->QueryIntAttribute("showEffectBankWindow", &seb);
            uiEl->QueryIntAttribute("patchLocked", &pl);
            uiEl->QueryIntAttribute("showGrid", &sg);
            uiEl->QueryIntAttribute("showFixtures", &sfix);
            uiEl->QueryIntAttribute("showPixels", &spix);
            uiEl->QueryIntAttribute("showFrame", &sfrm);
            uiEl->QueryIntAttribute("autoZoom", &sazm);
            uiEl->QueryFloatAttribute("previewOpacity", &config->previewOpacity);
            uiEl->QueryIntAttribute("editingCueIndex", &config->editingCueIndex);
            uiEl->QueryIntAttribute("editingBankIndex", &config->editingBankIndex);
            config->showFixturesWindow = (sf != 0);
            config->showPatchEditor = (sp != 0);
            config->showArtnetData = (sd != 0);
            config->showNetworkSettings = (sn != 0);
            config->showArtnetDevices = (sdev != 0);
            config->showScriptEditor = (sse != 0);
            config->showCuesWindow = (sc != 0);
            config->showOfflinePreviewWindow = (sop != 0);
            config->showEffectBankWindow = (seb != 0);
            config->patchLocked = (pl != 0);
            config->showGrid = (sg != 0);
            config->showFixtures = (sfix != 0);
            config->showPixels = (spix != 0);
            config->showFrame = (sfrm != 0);
            config->autoZoom = (sazm != 0);
        }
    }

    // Destroy existing patches first
    Patch::iterate(pixelMapper, [](flecs::entity patch) {
        patch.destruct();
    });

    for (XMLElement* patchEl = root->FirstChildElement("Patch");
         patchEl; patchEl = patchEl->NextSiblingElement("Patch"))
    {
        const char* patchName = patchEl->Attribute("name");

        flecs::entity patch = Patch::create(pixelMapper);
        if (patchName) patch.set_name(patchName);

        // ── Settings ──
        if (XMLElement* sEl = patchEl->FirstChildElement("Settings")) {
            auto* settings = patch.try_get_mut<Patch::Settings>();
            if (settings) {
                sEl->QueryFloatAttribute("refreshRate",   &settings->refreshRate);
                int netEn = 0, rm = 0, vfbRes = 256;
                sEl->QueryIntAttribute("networkEnabled",  &netEn);
                sEl->QueryIntAttribute("renderMode",      &rm);
                sEl->QueryIntAttribute("vfbResolution",   &vfbRes);
                settings->networkEnabled = (netEn != 0);
                settings->renderMode     = (Patch::RenderMode)rm;
                settings->vfbResolution  = vfbRes;

                int srcPort = 6454;
                sEl->QueryIntAttribute("sourcePort", &srcPort);
                settings->sourcePort = (uint16_t)srcPort;

                const char* luaPath  = sEl->Attribute("luaScriptPath");
                const char* glslPath = sEl->Attribute("shaderPath");
                if (luaPath)  std::strncpy(settings->luaScriptPath, luaPath,  255);
                if (glslPath) std::strncpy(settings->shaderPath,    glslPath, 255);
            }
        }

        // ── Script sources ──
        {
            auto* sd = patch.try_get_mut<Patch::ScriptData>();
            if (sd) {
                sd->luaSource  = getElementText(patchEl->FirstChildElement("LuaSource"));
                sd->glslSource = getElementText(patchEl->FirstChildElement("GlslSource"));
            }
        }

        // ── Fixtures ──
        XMLElement* fixturesEl = patchEl->FirstChildElement("Fixtures");
        if (fixturesEl) {
            int orderIdx = 0;
            for (XMLElement* fEl = fixturesEl->FirstChildElement("Fixture");
                 fEl; fEl = fEl->NextSiblingElement("Fixture"))
            {
                const char* name   = fEl->Attribute("name");
                const char* shape  = fEl->Attribute("shape");
                int pixelCount = 16, channelsPerPixel = 4;
                int dmxUniverse = 0, dmxAddress = 0;
                fEl->QueryIntAttribute("pixelCount",       &pixelCount);
                fEl->QueryIntAttribute("channelsPerPixel", &channelsPerPixel);
                fEl->QueryIntAttribute("dmxUniverse",      &dmxUniverse);
                fEl->QueryIntAttribute("dmxAddress",       &dmxAddress);

                flecs::entity fixture;

                if (shape && std::string(shape) == "Line") {
                    float sx=0,sy=0,sz=0,ex=100,ey=100,ez=0;
                    fEl->QueryFloatAttribute("startX", &sx);
                    fEl->QueryFloatAttribute("startY", &sy);
                    fEl->QueryFloatAttribute("startZ", &sz);
                    fEl->QueryFloatAttribute("endX",   &ex);
                    fEl->QueryFloatAttribute("endY",   &ey);
                    fEl->QueryFloatAttribute("endZ",   &ez);
                    fixture = Fixture::createLine(patch,
                        glm::vec3(sx,sy,sz), glm::vec3(ex,ey,ez),
                        pixelCount, channelsPerPixel);
                } else if (shape && std::string(shape) == "Circle") {
                    float cx=0,cy=0,cz=0,r=50;
                    fEl->QueryFloatAttribute("centerX", &cx);
                    fEl->QueryFloatAttribute("centerY", &cy);
                    fEl->QueryFloatAttribute("centerZ", &cz);
                    fEl->QueryFloatAttribute("radius",  &r);
                    fixture = Fixture::createCircle(patch,
                        glm::vec3(cx,cy,cz), r, pixelCount, channelsPerPixel);
                }

                if (fixture.is_valid()) {
                    if (name) fixture.set_name(name);
                    fixture.set<Fixture::Order>({orderIdx++});
                    Fixture::setDmxProperties(fixture,
                        (uint16_t)dmxUniverse, (uint16_t)dmxAddress);
                }
            }
        }

        // ── ArtNet Devices ──
        XMLElement* devicesEl = patchEl->FirstChildElement("ArtNetDevices");
        if (devicesEl) {
            int devCounter = 1;
            for (XMLElement* dEl = devicesEl->FirstChildElement("Device");
                 dEl; dEl = dEl->NextSiblingElement("Device"))
            {
                const char* name = dEl->Attribute("name");
                const char* ip   = dEl->Attribute("ip");
                int startU = 0, uCount = 1;
                dEl->QueryIntAttribute("startUniverse",  &startU);
                dEl->QueryIntAttribute("universeCount",  &uCount);

                flecs::entity dev = Artnet::Device::create(patch);
                if (name) dev.set_name(name);
                else      dev.set_name(("Device " + std::to_string(devCounter++)).c_str());

                if (auto* s = dev.try_get_mut<Artnet::Device::Settings>()) {
                    s->ipAddress    = ipFromString(ip);
                    s->startUniverse = (uint16_t)startU;
                    s->universeCount = (uint16_t)uCount;
                }
            }
        }

        // ── Effect Bank ──
        std::unordered_map<int, flecs::entity> idToEffect;
        flecs::entity bankFolder = patch.target<EffectBank::EffectFolder>();
        if (XMLElement* ebEl = patchEl->FirstChildElement("EffectBank")) {
            if (bankFolder.is_valid()) {
                auto* session = &bankFolder.get_mut<EffectBank::SessionState>();
                int actIdx = -1;
                ebEl->QueryIntAttribute("activeIndex", &actIdx);
                session->activeIndex = actIdx;

                for (XMLElement* fxEl = ebEl->FirstChildElement("Effect");
                     fxEl; fxEl = fxEl->NextSiblingElement("Effect"))
                {
                    const char* fxName = fxEl->Attribute("name");
                    int fxId = -1;
                    fxEl->QueryIntAttribute("id", &fxId);

                    auto newEffect = patch.world().entity()
                        .child_of(bankFolder)
                        .add<EffectBank::Effect::Is>()
                        .set<EffectBank::Effect::GlslSource>({getElementText(fxEl)})
                        .set<Patch::GPUProgram>({});
                    if (fxName) newEffect.set_name(fxName);

                    if (fxId != -1) {
                        idToEffect[fxId] = newEffect;
                    }
                }
            }
        }

        // ── Cue List ──
        flecs::entity cueFolder = patch.target<CueList::CueFolder>();
        if (XMLElement* clEl = patchEl->FirstChildElement("CueList")) {
            if (cueFolder.is_valid()) {
                auto* session = &cueFolder.get_mut<CueList::SessionState>();
                int autoAdv = 0, loop = 1, actIdx = -1;
                clEl->QueryIntAttribute("autoAdvance", &autoAdv);
                clEl->QueryIntAttribute("loop",        &loop);
                clEl->QueryIntAttribute("activeIndex", &actIdx);
                session->autoAdvance = (autoAdv != 0);
                session->loop        = (loop != 0);
                session->activeIndex = actIdx;

                int cueOrderIdx = 0;
                for (XMLElement* cueEl = clEl->FirstChildElement("Cue");
                     cueEl; cueEl = cueEl->NextSiblingElement("Cue"))
                {
                    const char* cueName = cueEl->Attribute("name");
                    float hold = 5.0f, fade = 0.0f;
                    cueEl->QueryFloatAttribute("holdSeconds", &hold);
                    cueEl->QueryFloatAttribute("fadeSeconds", &fade);
                    int targetFxId = -1;
                    cueEl->QueryIntAttribute("targetEffectId", &targetFxId);

                    auto newCue = patch.world().entity()
                        .child_of(cueFolder)
                        .add<CueList::Cue::Is>()
                        .set<CueList::Cue::HoldDuration>({hold})
                        .set<CueList::Cue::FadeDuration>({fade})
                        .set<CueList::Cue::IndexOrder>({cueOrderIdx++});
                    if (cueName) newCue.set_name(cueName);

                    if (targetFxId != -1 && idToEffect.count(targetFxId)) {
                        newCue.add<CueList::Cue::TargetEffect>(idToEffect[targetFxId]);
                    } else {
                        // Fallback/Backward compatibility: create an effect in EffectBank from inline source
                        std::string glsl = getElementText(cueEl);
                        auto fallbackEffect = patch.world().entity()
                            .child_of(bankFolder)
                            .add<EffectBank::Effect::Is>()
                            .set<EffectBank::Effect::GlslSource>({glsl})
                            .set<Patch::GPUProgram>({});
                        std::string fxName = (cueName ? std::string(cueName) : "Cue Effect");
                        fallbackEffect.set_name(fxName.c_str());
                        newCue.add<CueList::Cue::TargetEffect>(fallbackEffect);
                    }
                }
            }
        }

        // Trigger full recompile
        patch.add<Patch::DmxMapDirty>();
        patch.add<Patch::RenderAreaDirty>();
        patch.add<Patch::ProgramDirty>();
    }

    // Select the first patch
    Patch::iterate(pixelMapper, [&](flecs::entity patch) {
        Patch::select(pixelMapper, patch);
        return; // select first only
    });

    Fixture::updateFixtureNameCounter(pixelMapper.world());

    std::cout << "[PatchSerializer] Loaded from " << path << "\n";
    return true;
}
} // namespace PatchSerializer
} // namespace PixelMapper
