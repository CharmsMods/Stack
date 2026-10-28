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
std::vector<float> ReadBlur(Stack::Renderer::RawImageBackdropBlur& blur, GLuint program) {
    glUseProgram(program);
    blur.Bind(program, 1, {32,24});
    std::vector<float> result;
    // Inspect every retained Gaussian level and the broad color field through
    // the public binding contract, not private resource handles.
    for (int unit : {1,2,3,4,5,6,7,12}) {
        glActiveTexture(GL_TEXTURE0 + unit);
        GLint width=0, height=0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
        glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
        Check(width>0 && height>0,"Missing blur texture");
        const auto offset=result.size(); result.resize(offset+width*height*4);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,result.data()+offset);
    }
    glActiveTexture(GL_TEXTURE0);
    for (float value : result) Check(std::isfinite(value),"Nonfinite blur output");
    Check(glGetError()==GL_NO_ERROR,"Blur bindings/readback generated GL error");
    return result;
}
}

// A loaded OpenGL 4.3 context is required. The cloud EGL driver can call the
// same test with STACK_BACKDROP_BLUR_TEST_NO_MAIN; normal CTest uses GLFW below.
void TestBackdropBlurUniforms() {
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
    RawImageBackdropBlur blur;
    QueryCounter counter;
    std::vector<std::vector<float>> expected;
    for (int lifetime=0;lifetime<2;++lifetime) {
        for (int step=0;step<4;++step) {
            frame.patches[0].fadeAmount=float(step)/3.f;
            queryCount=0;
            Check(blur.Prepare(frame),"Fade preparation failed");
            Check(queryCount==(step==0 ? 8 : 0),"Uniform lookup escaped program initialization");
            const auto pixels=ReadBlur(blur,present);
            if (lifetime==0) expected.push_back(pixels);
            else Check(pixels==expected[step],"Shutdown/reinitialization changed blur output");
            queryCount=0;
            Check(blur.Prepare(frame),"Unchanged frame preparation failed");
            Check(queryCount==0,"Unchanged frame repeated uniform lookup");
            Check(ReadBlur(blur,present)==pixels,"Unchanged frame changed blur output");
        }
        Check(expected.front()!=expected.back(),"Fade ignored the previous texture");
        // Atlas resizing retains programs and their locations, not stale pixels.
        frame.nativeExtent={48,24};queryCount=0;
        Check(blur.Prepare(frame),"Resized atlas preparation failed");
        Check(queryCount==0,"Atlas resizing re-queried linked program uniforms");
        ReadBlur(blur,present);
        frame.nativeExtent={32,24};
        blur.Shutdown();
    }
    glUseProgram(0);glDeleteProgram(present);glDeleteTextures(1,&a);glDeleteTextures(1,&b);
}

#ifndef STACK_BACKDROP_BLUR_TEST_NO_MAIN
int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window=glfwCreateWindow(32,32,"Backdrop blur uniform tests",nullptr,nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    int result=0;
    try {
        Check(LoadGLFunctions(),"OpenGL loader failed");
        TestBackdropBlurUniforms();
        std::cout<<"Backdrop blur fade, cache, resize and reinitialization passed.\n";
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n'; result=1;
    }
    glfwDestroyWindow(window);glfwTerminate();return result;
}
#endif
