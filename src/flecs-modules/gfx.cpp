#include "gfx.hpp"
#include <iostream>

//flecs::entity gfx::createFramebuffer(flecs::world& w, int width, int height) {
//    uint32_t id = 0;
//    glGenFramebuffer(1, &id);
//    glBindFramebuffer(GL_FRAMEBUFFER, id);
//
//    uint32_t texture_id = 0;
//    glGenTextures(1, &texture_id);
//    glBindTexture(GL_TEXTURE_2D);
//    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
//    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHEMENT0, GL_TEXTURE_2D, texture_id, 0);
//
//    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
//        std::cerr << "error::gfx::framebuffer incomplete framebuffer\n";
//        return w.entity(0);
//    }
//
//    return w.entity()
//        .set<gfx::FramebufferId>({ id })
//        .set<gfx::FramebufferSize>({ width, height })
//        .set<gfx::FramebufferColorAttachement0, gfx::FrameBufferOutput>({ texture_id })
//        //.set<FramebufferDepthStencilAttachment, gfx::FrameBufferOutput>({ texture_id })
//    ;
//}

static void getDataFromSource() {
}

static uint32_t createTexture(
    int width, int height,
    const gfx::TextureFormat& format,
    const gfx::TextureParameters* params,
    const uint8_t* data)
{
    uint32_t id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    // TODO: format !
    glTexImage2D(GL_TEXTURE_2D, 0, format.internal, width, height, 0, format.pixel, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, params ? params->min_filter : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, params ? params->mag_filter : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, params ? params->wrap_s : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, params ? params->wrap_t : GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

gfx::gfx(flecs::world& w) {
    // We assume the openGL (or other gpu) context is loaded and available for now

    //w.component<GfxContext>()
    //    .add(flecs::Singleton)
    //;

    w.component<TextureID>();
    w.component<TextureSize>()
        .member<int>("width")
        .member<int>("height")
    ;
    w.component<TextureParameters>()
        .member<int>("min_filter")
        .member<int>("mag_filter")
        .member<int>("warp_s")
        .member<int>("warp_t")
    ;
    w.component<TextureFormat>()
        .member<int>("pixel")
        .member<int>("internal")
    ;

    w.component<TextureDataSource>();
    
    w.component<FramebufferID>();
    w.component<FramebufferSize>()
        .member<int>("width")
        .member<int>("height")
    ;

    w.component<VertexShaderSource>()
        .member<std::string>("source")
    ;

    w.component<FragmentShaderSource>()
        .member<std::string>("source")
    ;

    w.component<ComputeShaderSource>()
        .member<std::string>("source")
    ;

    w.component<Invalid>();

    // We want system that catch entity with
    // VertexShaderSource AND FragmentShaderSource but Not a ShaderID
    // that create the ShaderID component for that entity

    // We want system that catch entity with
    // ComputeShaderSource but Not a ShaderID
    // that create the ShaderID component for that entity
    // TODO!

    // We want system that catch entity with
    // TextureSize And|Or TextureDataSource [And TextureParameter] but Not a TextureID
    // that create the TextureID component for that entity
    w.system<const TextureSize, const TextureFormat, const TextureParameters*, const TextureDataSource*>("Texture builder")
        .kind(flecs::PreUpdate)
        .without<Invalid>()
        .without<TextureID>()
        //.expr("!Invalid, !TextureID, [in] TextureSize, [in] ?TextureParameters")
        .each([](flecs::entity e, const TextureSize& size, const TextureFormat& format, const TextureParameters* params, const TextureDataSource* dataSource) {
            std::cout << "[gfx] create texture id for : " << e.name() << '\n';
            //const uint8_t *data = dataSource ? getDataFromSource(dataSource) : nullptr;
            uint8_t *data = nullptr;
            if (dataSource && dataSource->len >= (size_t)size.width * (size_t)size.height)
                data = (uint8_t*)dataSource->data;
            uint32_t id = createTexture(size.width, size.height, format, params, data);
            if (id) {
                e.set<TextureID>({ id });
            } else {
                std::cerr << "fail'd to create texture\n";
                e.add<Invalid>();
            }
        })
    ;

    // We wat system that catch entity with
    // FramebufferSize And at least an {Attachment, TextureId} pair but Not a FramebufferID
    // that crate the FramebufferID componant for that entity
}


// FIXME ???? when exactly these thing are call by flecs ???
//gfx::TextureID::TextureID() {}
//gfx::TextureID::TextureID(uint32_t id) : id(id) {}
//gfx::TextureID::~TextureID() {
//    //flecs::log::trace("delete texture");
//    std::cerr << "delete texture(" << id << ")\n";
//    if (!this->id) return;
//    glDeleteTextures(1, &this->id);
//    this->id = 0;
//    std::cerr << "deleted texture(" << id << ")\n";
//}
        
gfx::Rgba::Rgba()                                           : r(255),  g(255),  b(255),  a(255) {}
gfx::Rgba::Rgba(uint8_t grey)                               : r(grey), g(grey), b(grey), a(255) {}
gfx::Rgba::Rgba(uint8_t r, uint8_t g, uint8_t b)            : r(r),    g(g),    b(b),    a(255) {}
gfx::Rgba::Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : r(r),    g(g),    b(b),    a(a)   {}

gfx::Uniform::Uniform()              : type(uint32), valu32(0) {}
gfx::Uniform::Uniform(uint32_t val)  : type(uint32), valu32(val) {}
gfx::Uniform::Uniform(int32_t val)   : type(int32) , vali32(val) {}
gfx::Uniform::Uniform(float val)     : type(f32)   , valf32(val) {}
gfx::Uniform::Uniform(glm::vec2 val) : type(vec2)  , valvec2(val) {}
gfx::Uniform::Uniform(glm::vec3 val) : type(vec3)  , valvec3(val) {}
gfx::Uniform::Uniform(Rgba val)      : type(color) , valcol(val) {}

void gfx::Uniform::upload() const {
    switch (this->type) {
        case uint32: glUniform1ui(this->location, this->valu32); break;
        case int32: glUniform1i(this->location, this->vali32); break;
        case f32: glUniform1f(this->location, this->valf32); break;
        case vec2: glUniform2f(this->location, this->valvec2.x, this->valvec2.y); break;
        case vec3: glUniform3f(this->location, this->valvec3.x, this->valvec3.y, this->valvec3.z); break;
        case color: glUniform4f(this->location, this->valcol.r/255.0, this->valcol.g/255.0, this->valcol.b/255.0, this->valcol.a/255.0); break;
    }
}

void gfx::UniformList::updateLocation(uint32_t prog) {
    for (Uniform& uni: this->uniforms)
        uni.location = glGetUniformLocation(prog, uni.name);
}

void gfx::UniformList::upload() const {
    for (const Uniform& uni: this->uniforms) {
        if (uni.location == -1) continue;
        uni.upload();
    }
}

