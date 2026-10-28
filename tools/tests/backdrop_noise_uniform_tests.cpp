#include "Renderer/GLHelpers.h"
#include "Renderer/RawImageBackdrop.h"
#include "Renderer/RawImageBackdropShaders.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
decltype(glGetUniformLocation_) query = nullptr;
int queryCount = 0;
GLint APIENTRY CountQuery(GLuint program, const GLchar* name) {
    ++queryCount;
    return query(program, name);
}
struct QueryCounter {
    QueryCounter() { query = glGetUniformLocation_; glGetUniformLocation_ = CountQuery; }
    ~QueryCounter() { glGetUniformLocation_ = query; }
};
std::vector<float> ReadNoise(Stack::Renderer::RawImageBackdropNoise& noise, GLuint program) {
    glUseProgram(program);
    noise.Bind(program, 8, 13);
    std::vector<float> result;
    // Inspect every exposed profile/grain texture, including mip levels used
    // by the presentation shader, through the public binding contract.
    for (int unit : {8,9,10,13,14}) {
        glActiveTexture(GL_TEXTURE0 + unit);
        GLint levels=0;
        glGetTexParameteriv(GL_TEXTURE_2D_ARRAY,0x82DF /* GL_TEXTURE_IMMUTABLE_LEVELS */,&levels);
        Check(levels>0,"Missing noise texture storage");
        for(int level=0;level<levels;++level) {
            GLint width=0,height=0,depth=0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY,level,GL_TEXTURE_WIDTH,&width);
            glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY,level,GL_TEXTURE_HEIGHT,&height);
            glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY,level,0x8071 /* GL_TEXTURE_DEPTH */,&depth);
            Check(width>0 && height>0 && depth>0,"Missing noise texture");
            const auto offset=result.size(); result.resize(offset+width*height*depth*4);
            glGetTexImage(GL_TEXTURE_2D_ARRAY,level,GL_RGBA,GL_FLOAT,result.data()+offset);
        }
    }
    glActiveTexture(GL_TEXTURE0);
    for (float value : result) Check(std::isfinite(value),"Nonfinite noise output");
    Check(glGetError()==GL_NO_ERROR,"Noise bindings/readback generated GL error");
    return result;
}
}

// A loaded OpenGL 4.3 context is required. The cloud EGL driver can call the
// same test with STACK_BACKDROP_NOISE_TEST_NO_MAIN; normal CTest uses GLFW below.
void TestBackdropNoiseUniforms() {
    using namespace Stack::Renderer;
    const GLuint present=GLHelpers::CreateShaderProgram(
        RawImageBackdropShaders::Vertex,RawImageBackdropShaders::Present.c_str());
    Check(present!=0,"Presentation shader failed to link");
    std::vector<float> first(32*24*4),second(first.size());
    for (std::size_t i=0;i<first.size();++i) {
        first[i]=i%4==3 ? 1.f : float(i%31)/30.f;
        second[i]=i%4==3 ? 1.f : float(i%17)/16.f;
    }
    const GLuint a=GLHelpers::CreateTextureFromData(first.data(),32,24,GL_RGBA32F,GL_RGBA,GL_FLOAT,GL_LINEAR);
    const GLuint b=GLHelpers::CreateTextureFromData(second.data(),32,24,GL_RGBA32F,GL_RGBA,GL_FLOAT,GL_LINEAR);
    Check(a && b,"Source texture allocation failed");
    RawImageBackdropFrame frame;
    frame.nativeExtent={32,24};frame.completePhotoCoverage=true;frame.presentationFingerprint=1;
    frame.patches.push_back({});frame.patches[0].texture=a;frame.patches[0].previousTexture=b;
    RawImageBackdropNoise noise;
    QueryCounter counter;
    std::vector<std::vector<float>> expected;
    for (int lifetime=0;lifetime<2;++lifetime) {
        for (int step=0;step<4;++step) {
            frame.patches[0].fadeAmount=float(step)/3.f;
            queryCount=0;
            Check(noise.Prepare(frame),"Fade preparation failed");
            Check(queryCount==(step==0 ? 16 : 0),"Uniform lookup escaped program initialization");
            const auto pixels=ReadNoise(noise,present);
            if (lifetime==0) expected.push_back(pixels);
            else Check(pixels==expected[step],"Shutdown/reinitialization changed noise output");
            queryCount=0;
            Check(noise.Prepare(frame),"Unchanged frame preparation failed");
            Check(queryCount==0,"Unchanged frame repeated uniform lookup");
            Check(ReadNoise(noise,present)==pixels,"Unchanged frame changed noise output");
        }
        Check(expected.front()!=expected.back(),"Fade ignored the previous texture");
        // Atlas resizing retains programs and their locations, not stale pixels.
        frame.nativeExtent={48,24};queryCount=0;
        Check(noise.Prepare(frame),"Resized atlas preparation failed");
        Check(queryCount==0,"Atlas resizing re-queried linked program uniforms");
        ReadNoise(noise,present);
        frame.nativeExtent={32,24};
        noise.Shutdown();
    }
    glUseProgram(0);glDeleteProgram(present);glDeleteTextures(1,&a);glDeleteTextures(1,&b);
}

#ifndef STACK_BACKDROP_NOISE_TEST_NO_MAIN
int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window=glfwCreateWindow(32,32,"Backdrop noise uniform tests",nullptr,nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    int result=0;
    try {
        Check(LoadGLFunctions(),"OpenGL loader failed");
        TestBackdropNoiseUniforms();
        std::cout<<"Backdrop noise fade, cache, resize and reinitialization passed.\n";
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n'; result=1;
    }
    glfwDestroyWindow(window);glfwTerminate();return result;
}
#endif
