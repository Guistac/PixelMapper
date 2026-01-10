#pragma once

#include <glad/glad.h>
#include <flecs.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct gfx {
    union Rgba {
        struct { uint8_t r, g, b, a; };
        uint32_t u;
        uint8_t v[4];

        Rgba();
        Rgba(uint8_t grey);
        Rgba(uint8_t r, uint8_t g, uint8_t b);
        Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    };

    enum UniformType {
        uint32,
        int32,
        f32,
        vec2,
        vec3,
        //vec4,
        color,
        //texture,
    };

    struct Uniform {
        UniformType type;
        int location = -1;
        std::string name = "";
        union {
            uint32_t valu32;
            int32_t vali32;
            float valf32;
            glm::vec2 valvec2;
            glm::vec3 valvec3;
            //glm::vec4 valvec4;
            Rgba valcol;
            // struct { uint32_t id, unit; } texture;
        };

        Uniform();
        Uniform(uint32_t val, std::string name);
        Uniform(int32_t val, std::string name);
        Uniform(float val, std::string name);
        Uniform(glm::vec2 val, std::string name);
        Uniform(glm::vec3 val, std::string name);
        Uniform(Rgba val, std::string name);

        void upload() const;
    };

    struct UniformList {
        std::vector<Uniform> list;

        void updateLocation(uint32_t prog);
        void upload() const;
    };
    
    // Invalid tag set by system that faild to transfrom an entity such as Texture builder or Framebuffer builder
    // TODO? should this contain a error meassage descibing why the system fail'd ?
    struct Invalid {};

    struct ComputeShaderSource {
        std::string str;
    };

    struct VertexShaderSource {
        std::string str;
    };

    struct FragmentShaderSource {
        std::string str;
    };

    struct TextureID { uint32_t id; };
    struct TextureSize { int width, height; };
    struct TextureFormat {
        // TODO: make these enum componant
        int pixel = GL_RGBA;
        int internal = GL_RGBA8;
    };
    struct TextureParameters {
        // TODO: make these enum componant
        int min_filter = GL_LINEAR;
        int mag_filter = GL_LINEAR;
        int wrap_s = GL_REPEAT;
        int wrap_t = GL_REPEAT;
    };
    // Tag that should be pair with:
    // - a filepath in wich case the file is loaded in memory and decoded if needed
    //   then uploaded to the texture storage (maybe watched in file syteme later for automatic update)
    // - a const uint8_t*, similar (vector) or any valable data array that matched a pixel format
    // FIXME(temp) set to uint8_t * and expect rgba
    struct TextureDataSource { size_t len; uint32_t *data; };

    // We Could have a sync tag which is match in compinaison with a valid Texture archetype
    // in a system that fetch the data from the gpu ?

    // Usable Framebuffer archetype: id, size, and at least one (attachement, output)
    struct FramebufferID { uint32_t id; };
    struct FramebufferSize { int width, height; };
    struct FramebufferColorAttachment0 {};

    struct ShaderID { uint32_t id; };

    gfx(flecs::world& w);

    //static flecs::entity createFramebuffer(flecs::world& w);
};
