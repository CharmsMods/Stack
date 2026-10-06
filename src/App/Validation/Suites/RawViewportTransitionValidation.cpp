#include "App/Validation/Suites/EditorRenderWorkerPreviewValidation.h"
#include "Editor/Internal/RawWorkspace/RawViewportFadeRenderer.h"
#include "Renderer/FullscreenQuad.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/ScopedGLObjects.h"
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>

namespace Stack::Validation {
bool ValidateRawViewportTransitions() {
    Raw::ViewportFadeRenderer renderer;
    if (!renderer.Initialize()) return false;
    FullscreenQuad quad;
    quad.Initialize();
    const float identity[] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    const std::array<float,4> a {0.1f, 0.3f, 0.9f, 1}, b {0.8f, 0.6f, 0.05f, 1};
    GLuint textures[3] {};
    glGenTextures(3, textures);
    for (int i = 0; i < 3; ++i) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, i == 0 ? a.data() : b.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    GLint previousFbo = 0, viewport[4] {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    GLuint fbo = GLHelpers::CreateFBO(textures[2]);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0,0,1,1);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    bool valid = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    const auto decode = [](double x) { return x <= 0.04045 ? x/12.92 : std::pow((x+0.055)/1.055,2.4); };
    const auto encode = [](double x) { return x <= 0.0031308 ? x*12.92 : 1.055*std::pow(x,1/2.4)-0.055; };
    for (bool encoded : {false, true}) for (float amount : {0.0f,0.5f,1.0f}) {
        renderer.Bind(textures[0],amount,encoded,identity,false);
        glBindTexture(GL_TEXTURE_2D,textures[1]);
        quad.Draw();
        std::array<float,4> actual {};
        glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,actual.data());
        for (int c = 0; c < 3; ++c) {
            const double expected = encoded ? encode((1-amount)*decode(a[c])+amount*decode(b[c])) : (1-amount)*a[c]+amount*b[c];
            valid = valid && std::isfinite(actual[c]) && std::abs(actual[c]-expected) < 0.0001;
        }
        valid = valid && std::abs(actual[3]-1) < 0.0001;
    }
    // Graph frames can contain transparency. Mix premultiplied colors so
    // invisible RGB does not contaminate the transition.
    auto translucentA = a, translucentB = b;
    translucentA[3] = 0.25f; translucentB[3] = 0.75f;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_FLOAT,translucentA.data());
    glBindTexture(GL_TEXTURE_2D, textures[1]);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_FLOAT,translucentB.data());
    renderer.Bind(textures[0],0.5f,false,identity,false);
    quad.Draw();
    std::array<float,4> translucent {};
    glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,translucent.data());
    for (int c = 0; c < 3; ++c)
        valid = valid && std::abs(translucent[c] - (0.25f*a[c] + 0.75f*b[c])) < 0.0001f;
    valid = valid && std::abs(translucent[3]-0.5f) < 0.0001f;
    // Proxy and native textures must stay aligned without allocating an
    // intermediate image just to make their dimensions agree.
    std::array<float,16> proxy {};
    std::array<float,64> native {}, refined {};
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        const int p = (y*2+x)*4;
        proxy[p] = float(x); proxy[p+1] = float(y); proxy[p+2] = 0.25f; proxy[p+3] = 1;
    }
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        const int p = (y*4+x)*4;
        native[p] = x/3.0f; native[p+1] = y/3.0f; native[p+2] = 0.75f; native[p+3] = 1;
    }
    glBindTexture(GL_TEXTURE_2D,textures[0]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,2,2,0,GL_RGBA,GL_FLOAT,proxy.data());
    glBindTexture(GL_TEXTURE_2D,textures[1]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,4,4,0,GL_RGBA,GL_FLOAT,native.data());
    glBindTexture(GL_TEXTURE_2D,textures[2]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,4,4,0,GL_RGBA,GL_FLOAT,nullptr);
    glViewport(0,0,4,4);
    for (float amount : {0.0f,0.5f,1.0f}) {
        renderer.Bind(textures[0],amount,false,identity,false);
        glBindTexture(GL_TEXTURE_2D,textures[1]);
        quad.Draw();
        glReadPixels(0,0,4,4,GL_RGBA,GL_FLOAT,refined.data());
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) for (int c = 0; c < 4; ++c) {
            const int p = (y*4+x)*4+c;
            const float expected = (1-amount)*proxy[((y/2)*2+x/2)*4+c]+amount*native[p];
            valid = valid && std::abs(refined[p]-expected) < 0.0001f;
        }
    }
    renderer.Bind(textures[0],0,false,identity,false,ImVec2(0.5f,0),ImVec2(0.5f,1));
    glBindTexture(GL_TEXTURE_2D,textures[1]);
    quad.Draw();
    glReadPixels(0,0,4,4,GL_RGBA,GL_FLOAT,refined.data());
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        const int p = (y*4+x)*4;
        valid = valid && std::abs(refined[p]-1.0f) < 0.0001f &&
            std::abs(refined[p+1]-float(y/2)) < 0.0001f;
    }
    // Measure animation cost separately from RAW processing and readbacks.
    // Solid textures still exercise two full raster samples and display transfer.
    constexpr int width = 1280, height = 720, frames = 32;
    glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < 3; ++i) {
        glBindTexture(GL_TEXTURE_2D,textures[i]);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,width,height,0,GL_RGBA,GL_FLOAT,nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[i],0);
        glClearColor(0.2f+0.1f*i,0.3f,0.5f,1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[2],0);
    glViewport(0,0,width,height);
    const GLuint copy = GLHelpers::CreateShaderProgram(R"(
#version 330 core
layout(location=0) in vec2 Position;
layout(location=1) in vec2 UV;
out vec2 uv;
void main() { uv=UV; gl_Position=vec4(Position,0,1); }
)", R"(
#version 330 core
in vec2 uv;
uniform sampler2D Texture;
out vec4 color;
void main() { color=texture(Texture,uv); }
)");
    double timings[2] {};
    for (int mode = 0; mode < 2; ++mode) {
        if (mode == 1) renderer.Bind(textures[0],0.5f,true,identity,false);
        else { glUseProgram(copy); glUniform1i(glGetUniformLocation(copy,"Texture"),0); }
        glBindTexture(GL_TEXTURE_2D,textures[1]);
        quad.Draw();
        GLuint queries[2] {};
        glGenQueries(2,queries);
        glQueryCounter(queries[0],GL_TIMESTAMP);
        for (int frame = 0; frame < frames; ++frame) quad.Draw();
        glQueryCounter(queries[1],GL_TIMESTAMP);
        GLuint64 begin = 0, end = 0;
        glGetQueryObjectui64v(queries[0],GL_QUERY_RESULT,&begin);
        glGetQueryObjectui64v(queries[1],GL_QUERY_RESULT,&end);
        timings[mode] = double(end-begin)/1000000.0/frames;
        glDeleteQueries(2,queries);
    }
    glDeleteProgram(copy);
    std::cout << "RAW fade GPU timing at 1280x720: copy " << timings[0] << " ms, fade " << timings[1]
        << " ms, added cost " << std::max(0.0,timings[1]-timings[0]) << " ms/frame.\n";
    glBindFramebuffer(GL_FRAMEBUFFER,previousFbo);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glDeleteFramebuffers(1,&fbo);
    glDeleteTextures(3,textures);
    std::cout << "RAW viewport fade GPU color validation " << (valid ? "passed" : "FAILED") << ".\n";
    return valid;
}
}
