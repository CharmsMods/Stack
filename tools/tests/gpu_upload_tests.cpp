#include "Raw/RawGpuPipeline.h"
#include "Raw/Demosaic/GpuDemosaic.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void CheckState(GLenum name, GLint expected, const char* message) {
    GLint actual = 0;
    glGetIntegerv(name, &actual);
    Check(actual == expected, message);
}

void TestTypedUploads() {
    const std::array<std::uint16_t, 6> samples { 1, 257, 4095, 83, 1024, 65535 };
    GLuint sentinel = GLHelpers::CreateEmptyTexture(1, 1);
    Check(sentinel != 0, "Could not create the binding sentinel.");
    glBindTexture(GL_TEXTURE_2D, sentinel);
    GLuint unpackBuffer = 0;
    glGenBuffers(1, &unpackBuffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, 32, nullptr, GL_STATIC_DRAW);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 17);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 3);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);

    auto checkRestored = [&]() {
        CheckState(GL_TEXTURE_BINDING_2D, sentinel, "Texture binding was not restored.");
        CheckState(GL_PIXEL_UNPACK_BUFFER_BINDING, unpackBuffer, "Upload PBO was not restored.");
        CheckState(GL_UNPACK_ALIGNMENT, 8, "Upload alignment was not restored.");
        CheckState(GL_UNPACK_ROW_LENGTH, 17, "Upload row length was not restored.");
        CheckState(GL_UNPACK_SKIP_ROWS, 2, "Upload row offset was not restored.");
        CheckState(GL_UNPACK_SKIP_PIXELS, 3, "Upload pixel offset was not restored.");
        CheckState(GL_UNPACK_SWAP_BYTES, GL_TRUE, "Upload byte order was not restored.");
    };

    const GLuint texture = GLHelpers::CreateTextureFromData(
        samples.data(), 3, 2, GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT);
    Check(texture != 0, "Odd-width UInt16 upload failed with an unrelated PBO bound.");
    checkRestored();

    std::array<std::uint16_t, 6> actual {};
    glPixelStorei(GL_PACK_SWAP_BYTES, GL_TRUE);
    const Stack::Renderer::GLState::PixelPackState pack;
    pack.ConfigureTightCpuReadback();
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, actual.data());
    pack.Restore();
    Check(actual == samples, "Odd-width upload changed sample values or row boundaries.");
    CheckState(GL_PACK_SWAP_BYTES, GL_TRUE, "Readback byte order was not restored.");
    glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
    glBindTexture(GL_TEXTURE_2D, sentinel);

    Check(GLHelpers::CreateTextureFromData(
        samples.data(), 3, 2, 0xffffffffu, GL_RED_INTEGER, GL_UNSIGNED_SHORT) == 0,
        "A rejected allocation was published as a valid texture.");
    checkRestored();
    GLint maximumSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumSize);
    Check(GLHelpers::CreateTextureFromData(
        nullptr, maximumSize + 1, 1, GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT) == 0,
        "An oversized texture was accepted.");
    checkRestored();
    GLuint empty = GLHelpers::CreateEmptyTexture(3, 2);
    Check(empty != 0, "Empty texture allocation treated a bound PBO as pixel data.");
    checkRestored();

    glDeleteTextures(1, &empty);
    glDeleteTextures(1, &texture);
    glDeleteTextures(1, &sentinel);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glDeleteBuffers(1, &unpackBuffer);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
    Check(glGetError() == GL_NO_ERROR, "Texture upload left an OpenGL error.");
}

void TestIncompleteRawInputs() {
    Raw::RawGpuPipeline pipeline;
    Raw::RawImageData raw;
    raw.metadata.rawWidth = raw.metadata.visibleWidth = 3;
    raw.metadata.rawHeight = raw.metadata.visibleHeight = 2;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw.metadata.cfaPattern = Raw::CfaPattern::RGGB;
    raw.rawBuffer.assign(1, 1024);
    Check(pipeline.Render(raw, {}) == 0, "A truncated RAW sensor buffer was uploaded.");
    Check(pipeline.GetLastError().find("incomplete sensor") != std::string::npos,
        "A truncated RAW buffer did not report its size mismatch.");

    raw.metadata.pixelLayout = Raw::RawPixelLayout::LinearRgb;
    raw.metadata.linearChannels = 3;
    raw.linearFloatBuffer.assign(1, 0.5f);
    Check(pipeline.Render(raw, {}) == 0, "A truncated linear RGB buffer was uploaded.");
    Check(pipeline.GetLastError().find("incomplete RGB") != std::string::npos,
        "A truncated linear RGB buffer did not report its size mismatch.");
    Check(glGetError() == GL_NO_ERROR, "Invalid CPU input reached an OpenGL upload.");
}

void TestDemosaicState() {
    // Reuse the odd-sized upload fixture at the new compute boundary.
    const std::array<float,6> samples{1,257,4095,83,1024,65535};
    GLuint source=GLHelpers::CreateTextureFromData(samples.data(),3,2,GL_R32F,GL_RED,GL_FLOAT);
    GLuint sentinel=GLHelpers::CreateTextureFromData(nullptr,1,1,GL_R32F,GL_RED,GL_FLOAT);
    Check(source && sentinel,"Could not allocate demosaicing state fixture.");
    GLuint storage=0;glGenBuffers(1,&storage);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,storage);
    const GLuint value=123;glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof(value),&value,GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,storage);
    glBindImageTexture(0,sentinel,0,GL_FALSE,0,GL_READ_ONLY,GL_R32F);
    for(int unit=0;unit<4;++unit) {glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,sentinel);}
    glActiveTexture(GL_TEXTURE2);
    Raw::Demosaic::GpuDemosaic engine;
    std::string error;
    const GLuint result=engine.Reconstruct(source,3,2,Raw::CfaPattern::RGGB,Raw::DemosaicMethod::Lmmse,{},error);
    Check(result!=0,error.c_str());
    CheckState(GL_ACTIVE_TEXTURE,GL_TEXTURE2,"Demosaicing changed the active texture unit.");
    for(int unit=0;unit<4;++unit) {
        glActiveTexture(GL_TEXTURE0+unit);
        CheckState(GL_TEXTURE_BINDING_2D,sentinel,"Demosaicing changed a caller's texture binding.");
    }
    GLint binding=0;glGetIntegeri_v(GL_IMAGE_BINDING_NAME,0,&binding);
    Check(binding==static_cast<GLint>(sentinel),"Demosaicing changed the caller's image binding.");
    constexpr GLenum storageBinding=0x90D3;
    glGetIntegeri_v(storageBinding,0,&binding);
    Check(binding==static_cast<GLint>(storage),"Demosaicing changed the indexed storage buffer.");
    CheckState(storageBinding,storage,"Demosaicing changed the generic storage buffer.");
    GLuint retained=0;glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof(retained),&retained);
    Check(retained==value,"Demosaicing wrote to the caller's storage buffer.");
    glBindImageTexture(0,0,0,GL_FALSE,0,GL_READ_ONLY,GL_R32F);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,0);glBindBuffer(GL_SHADER_STORAGE_BUFFER,0);
    glDeleteBuffers(1,&storage);glDeleteTextures(1,&result);glDeleteTextures(1,&source);glDeleteTextures(1,&sentinel);
    glActiveTexture(GL_TEXTURE0);
    Check(glGetError()==GL_NO_ERROR,"GPU demosaicing left an OpenGL error.");
}

} // namespace

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(32, 32, "Texture upload tests", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    int result = 0;
    try {
        Check(LoadGLFunctions(), "Could not load OpenGL functions.");
        TestTypedUploads();
        TestIncompleteRawInputs();
        TestDemosaicState();
        std::cout << "Typed texture uploads, GL state, allocation rejection, RAW input bounds and GPU demosaicing state passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
