#include "gfx.hpp"
#include <GLFW/glfw3.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstdio>

static uint32_t buildShaderProgram(const char *vs_source, const char *fs_source) {
    char log[1024];
    int32_t success = 0;
    uint32_t prog = glCreateProgram(), vs = 0, fs = 0;

    glDisable(GL_DEBUG_OUTPUT);
    vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vs_source, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vs, 1024, NULL, log);
        fprintf(stderr, "Fail'd to compile vertex shader:\n%s\n", log);
        goto err;
    }
    glAttachShader(prog, vs);

    fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fs_source, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fs, 1024, NULL, log);
        fprintf(stderr, "Fail'd to compile fragment shader:\n%s\n", log);
        goto err;
    }
    glAttachShader(prog, fs);

    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(prog, 1024, NULL, log);
        fprintf(stderr, "Fail'd to link program:\n%s\n", log);
        goto err;
    }

    glDeleteShader(vs); glDeleteShader(fs);
    glEnable(GL_DEBUG_OUTPUT);
    return prog;

err:
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    if (prog) glDeleteProgram(prog);
    glEnable(GL_DEBUG_OUTPUT);
    return 0;
}

static bool hasFramebufferArchetype(flecs::entity e) {
    return !e.has<gfx::Invalid>() && e.has<gfx::FramebufferID>() && e.has<gfx::FramebufferColorAttachment0>();
}
static bool hasShaderArchetype(flecs::entity e) {
    return !e.has<gfx::Invalid>() && e.has<gfx::ShaderID>() && e.has<gfx::UniformList>();
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
    glTexImage2D(GL_TEXTURE_2D, 0, format.internal, width, height, 0, format.pixel, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, params ? params->min_filter : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, params ? params->mag_filter : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, params ? params->wrap_s : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, params ? params->wrap_t : GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
    return id;
}

// From flecs/examples/cpp/reflection/ser_std_vector/src/main.cpp
// TODO this should be move to flecs utils or something
template <typename Elem, typename Vector = std::vector<Elem>>
flecs::opaque<Vector, Elem> stdVectorSupport(flecs::world& w) {
    return flecs::opaque<Vector, Elem>()
        .as_type(w.vector<Elem>())
        .serialize([](const flecs::serializer *s, const Vector *data) {
            for (const auto& el : *data) s->value(el);
            return 0;
        })
        .count([](const Vector *data) { return data->size(); })
        .resize([](Vector *data, size_t size) { data->resize(size); })
        .ensure_element([](Vector *data, size_t elem) {
            if (data->size() <= elem) data->resize(elem + 1);
            return &data->data()[elem];
        })
    ;
}


gfx::gfx(flecs::world& w) {
    // We assume the openGL (or other gpu) context is loaded and available for now

    w.component<Invalid>();

    // TODO: this should be moved to flecs extra utils or something
    w.component<std::string>()
        .opaque(flecs::String) // Opaque type that maps to string
        .serialize([](const flecs::serializer *s, const std::string *data) {
            const char *str = data->c_str();
            return s->value(flecs::String, &str); // Forward to serializer
        })
        .assign_string([](std::string* data, const char *value) {
            *data = value; // Assign new value to std::string
        })
    ;

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
    w.component<FramebufferColorAttachment0>()
        .member<flecs::entity>("e")
    ;

    w.component<ShaderID>();
    w.component<VertexShaderSource>()
        .member<std::string>("str")
    ;
    w.component<FragmentShaderSource>()
        .member<std::string>("str")
    ;
    w.component<ComputeShaderSource>()
        .member<std::string>("str")
    ;
    w.component<Uniform>()
        .member<UniformType>("type")
        .member<int>("location")
        .member<std::string>("name")
    ;
    w.component<std::vector<Uniform>>()
        .opaque(stdVectorSupport<Uniform>)
    ;
    w.component<UniformList>()
        .member<std::vector<Uniform>>("list")
    ;

    w.component<RenderCommand>()
        .member<flecs::entity>("fb")
        .member<flecs::entity>("shader")
    ;

    w.component<Rgba>()
        .member<uint8_t>("red")
        .member<uint8_t>("green")
        .member<uint8_t>("blue")
        .member<uint8_t>("alpha")
    ;
    w.component<std::vector<Rgba>>()
        .opaque(stdVectorSupport<Rgba>)
    ;
    w.component<FramebufferDataRequest>()
        .member<int>("width")
        .member<int>("height")
        .member<std::vector<Rgba>>("data")
    ;

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

    // Texture Observers
    w.observer<const TextureID, const TextureSize, const TextureFormat, const TextureDataSource*>("Texture size updater")
        .event(flecs::OnSet)
        .without<Invalid>()
        .each([](const TextureID& texture, const TextureSize& size, const TextureFormat& format, const TextureDataSource* dataSource) {
            glBindTexture(GL_TEXTURE_2D, texture.id);
            uint8_t *data = nullptr;
            if (dataSource && dataSource->len >= (size_t)size.width * (size_t)size.height)
                data = (uint8_t*)dataSource->data;
            glTexImage2D(GL_TEXTURE_2D, 0, format.internal, size.width, size.height, 0, format.pixel, GL_UNSIGNED_BYTE, data);
            glBindTexture(GL_TEXTURE_2D, 0);
        })
    ;

    w.observer<const TextureID, const TextureParameters>("Texture parameters updater")
        .event(flecs::OnSet)
        .without<Invalid>()
        .each([](const TextureID& texture, const TextureParameters& params) {
            glBindTexture(GL_TEXTURE_2D, texture.id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, params.min_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, params.mag_filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, params.wrap_s);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, params.wrap_t);
            glBindTexture(GL_TEXTURE_2D, 0);
        })
    ;

    // We wat system that catch entity with
    // FramebufferSize And at least an {Attachment, TextureId} pair but Not a FramebufferID
    // that crate the FramebufferID componant for that entity
    w.system<const FramebufferColorAttachment0>("Framebuffer builder")
        .kind(flecs::PreUpdate)
        .without<Invalid>()
        .without<FramebufferID>()
        .each([](flecs::entity e, const FramebufferColorAttachment0& attachment0) {
            std::cout << "[gfx] Framebuffer builder: " << e.name() << " | " << attachment0.e.name() << '\n';
            if (!(attachment0.e.has<TextureID>() && attachment0.e.has<TextureSize>())) {
                //std::cerr << "[gfx]:Framebuffer builder: attachment0(" << attachment0.e.name() << ") is not a texture\n";
                return;
            }
            const TextureID texture = attachment0.e.get<TextureID>();
            //const TextureSize size = attachment0.e.get<TextureID>();
            uint32_t id = 0;
            glGenFramebuffers(1, &id);
            glBindFramebuffer(GL_FRAMEBUFFER, id);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.id, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "[gfx] error: framebuffer incomplete framebuffer\n";
                e.add<Invalid>();
                return;
            }
            std::cout << "[gfx] create framebuffer ID for (" << e.name() << ")\n";
            e.set<FramebufferID>({ id });
        })
    ;

    // We want system that catch entity with
    // VertexShaderSource AND FragmentShaderSource but Not a ShaderID
    // that create the ShaderID component for that entity
    w.system<const VertexShaderSource, const FragmentShaderSource>("Shader builder")
        .kind(flecs::PreUpdate)
        .without<Invalid>()
        .without<ShaderID>()
        .each([](flecs::entity e, const VertexShaderSource& vsSource, const FragmentShaderSource& fsSource) {
            uint32_t prog = buildShaderProgram(vsSource.str.c_str(), fsSource.str.c_str());
            if (!prog) { e.add<Invalid>(); return; }
            e.set<ShaderID>({ prog });
        })
    ;
    // We want system that catch entity with
    // ComputeShaderSource but Not a ShaderID
    // that create the ShaderID component for that entity
    // TODO!

    //w.observer<ShaderID&>("Shader updater")
    //    .event(flecs::OnRemove)
    //    .with<const VertexShaderSource>()
    //    .with<const FragmentShaderSource>()
    //    .without<Invalid>()
    //    .each([](flecs::entity e, ShaderID& shader) {
    //        glDeleteProgram(shader.id);
    //        e.remove<ShaderID>();
    //    })
    //;

    // Observer that react to UniformList and update there location
    w.observer<const ShaderID, UniformList>("Uniform location updater")
        .event(flecs::OnSet)
        .without<Invalid>()
        .each([](const ShaderID& shader, UniformList& uniforms) {
            uniforms.updateLocation(shader.id);
        })
    ;

    // System that do the rendering
    w.system<const RenderCommand>("Render effect system")
        .kind(flecs::PreStore)
        .each([](const RenderCommand& cmd) {
            if (!hasFramebufferArchetype(cmd.fb) || !hasShaderArchetype(cmd.shader))
                return;
            glBindFramebuffer(GL_FRAMEBUFFER, cmd.fb.get<FramebufferID>().id);
            const TextureSize& size = cmd.fb.get<FramebufferColorAttachment0>().e.get<TextureSize>();
            glViewport(0,0, size.width, size.height);
            glUseProgram(cmd.shader.get<ShaderID>().id);
            // default uniform hack
            glUniform1f(1, (float)glfwGetTime());
            cmd.shader.get<UniformList>().upload();
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        })
    ;

    // FIXME: format! for now this assume rgba unsigned_byte format (well openGL do handle conversion)
    w.system<const FramebufferID, const FramebufferColorAttachment0, FramebufferDataRequest>("Framebuffer request")
        .kind(flecs::OnStore)
        .without<Invalid>()
        .each([](const FramebufferID& framebuffer, const FramebufferColorAttachment0& attachment0, FramebufferDataRequest& request) {
            // FIXME: check attachment validity
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer.id);
            const TextureSize& size = attachment0.e.get<TextureSize>();
            size_t len = (size_t)size.width * (size_t)size.height;
            request.width = size.width; request.height = size.height;
            request.data.resize(len);
            glReadPixels(0, 0, size.width, size.height, GL_RGBA, GL_UNSIGNED_BYTE, request.data.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        })
    ;
}

gfx::Rgba gfx::FramebufferDataRequest::at(int x, int y) const {
    x = std::clamp(x, 0, this->width-1);
    y = std::clamp(y, 0, this->height-1);
    return this->data[x + y * this->width];
}

gfx::Rgba gfx::FramebufferDataRequest::sample(glm::vec2 pos) const {
    const float x1 = pos.x * this->width - 0.5;
    const float x2 = x1 + 1.0;
    const float y1 = pos.y * this->height - 0.5;
    const float y2 = y1 + 1.0;

    const auto v1 = gfx::Rgba::lerp(
        at(std::floor(x1), std::floor(y1)),
        at(std::floor(x2), std::floor(y1)),
        1.0 - x1 - std::floor(x1)
    );
    const auto v2 = gfx::Rgba::lerp(
        at(std::floor(x1), std::floor(y2)),
        at(std::floor(x2), std::floor(y2)),
        1.0 - x1 - std::floor(x1)
    );
    return Rgba::lerp(v1, v2, 1.0 - y1 - std::floor(y1));
}
        
gfx::Rgba gfx::Rgba::lerp(const gfx::Rgba& a, const gfx::Rgba& b, float t) {
    return gfx::Rgba(
        std::lerp((float)a.r, (float)b.r, t),
        std::lerp((float)a.g, (float)b.g, t),
        std::lerp((float)a.b, (float)b.b, t)
    );
}

gfx::Rgba::Rgba()                                           : r(255),  g(255),  b(255),  a(255) {}
gfx::Rgba::Rgba(uint8_t grey)                               : r(grey), g(grey), b(grey), a(255) {}
gfx::Rgba::Rgba(uint8_t r, uint8_t g, uint8_t b)            : r(r),    g(g),    b(b),    a(255) {}
gfx::Rgba::Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) : r(r),    g(g),    b(b),    a(a)   {}

gfx::Uniform::Uniform()                                : type(uint32), valu32(0) {}
gfx::Uniform::Uniform(uint32_t  val, std::string name) : type(uint32), valu32(val) , name(name) {}
gfx::Uniform::Uniform(int32_t   val, std::string name) : type(int32) , vali32(val) , name(name) {}
gfx::Uniform::Uniform(float     val, std::string name) : type(f32)   , valf32(val) , name(name) {}
gfx::Uniform::Uniform(glm::vec2 val, std::string name) : type(vec2)  , valvec2(val), name(name) {}
gfx::Uniform::Uniform(glm::vec3 val, std::string name) : type(vec3)  , valvec3(val), name(name) {}
gfx::Uniform::Uniform(Rgba      val, std::string name) : type(color) , valcol(val) , name(name) {}

void gfx::Uniform::upload() const {
    switch (this->type) {
        case uint32: glUniform1ui(this->location, this->valu32); break;
        case int32 : glUniform1i(this->location, this->vali32); break;
        case f32   : glUniform1f(this->location, this->valf32); break;
        case vec2  : glUniform2f(this->location, this->valvec2.x, this->valvec2.y); break;
        case vec3  : glUniform3f(this->location, this->valvec3.x, this->valvec3.y, this->valvec3.z); break;
        case color : glUniform4f(this->location, this->valcol.r/255.0, this->valcol.g/255.0, this->valcol.b/255.0, this->valcol.a/255.0); break;
    }
}

void gfx::UniformList::updateLocation(uint32_t prog) {
    for (Uniform& uni: this->list)
        uni.location = glGetUniformLocation(prog, uni.name.c_str());
}

void gfx::UniformList::upload() const {
    for (const Uniform& uni: this->list) {
        if (uni.location == -1) continue;
        uni.upload();
    }
}

