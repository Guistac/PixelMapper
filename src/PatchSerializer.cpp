#include "PatchSerializer.h"
#include "App.h"
#include "Patch.h"
#include "Fixture.h"
#include "Artnet.h"
#include "Shape.h"

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
