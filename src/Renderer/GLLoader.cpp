#include "GLLoader.h"

#include <iostream>

GLuint(APIENTRY* glCreateShader_)(GLenum) = nullptr;
void(APIENTRY* glDeleteShader_)(GLuint) = nullptr;
void(APIENTRY* glShaderSource_)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
void(APIENTRY* glCompileShader_)(GLuint) = nullptr;
void(APIENTRY* glGetShaderiv_)(GLuint, GLenum, GLint*) = nullptr;
void(APIENTRY* glGetShaderInfoLog_)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;

GLuint(APIENTRY* glCreateProgram_)(void) = nullptr;
void(APIENTRY* glDeleteProgram_)(GLuint) = nullptr;
void(APIENTRY* glAttachShader_)(GLuint, GLuint) = nullptr;
void(APIENTRY* glLinkProgram_)(GLuint) = nullptr;
void(APIENTRY* glUseProgram_)(GLuint) = nullptr;
void(APIENTRY* glGetProgramiv_)(GLuint, GLenum, GLint*) = nullptr;
void(APIENTRY* glGetProgramInfoLog_)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;

GLint(APIENTRY* glGetUniformLocation_)(GLuint, const GLchar*) = nullptr;
void(APIENTRY* glUniform1i_)(GLint, GLint) = nullptr;
void(APIENTRY* glUniform1f_)(GLint, GLfloat) = nullptr;
void(APIENTRY* glUniform2f_)(GLint, GLfloat, GLfloat) = nullptr;
void(APIENTRY* glUniform2i_)(GLint, GLint, GLint) = nullptr;
void(APIENTRY* glUniform3f_)(GLint, GLfloat, GLfloat, GLfloat) = nullptr;
void(APIENTRY* glUniform4f_)(GLint, GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
void(APIENTRY* glUniform3fv_)(GLint, GLsizei, const GLfloat*) = nullptr;
void(APIENTRY* glUniformMatrix4fv_)(GLint, GLsizei, GLboolean, const GLfloat*) = nullptr;

void(APIENTRY* glGenVertexArrays_)(GLsizei, GLuint*) = nullptr;
void(APIENTRY* glDeleteVertexArrays_)(GLsizei, const GLuint*) = nullptr;
void(APIENTRY* glBindVertexArray_)(GLuint) = nullptr;

void(APIENTRY* glGenBuffers_)(GLsizei, GLuint*) = nullptr;
void(APIENTRY* glDeleteBuffers_)(GLsizei, const GLuint*) = nullptr;
void(APIENTRY* glBindBuffer_)(GLenum, GLuint) = nullptr;
void(APIENTRY* glBufferData_)(GLenum, ptrdiff_t, const void*, GLenum) = nullptr;
void(APIENTRY* glBufferSubData_)(GLenum, GLintptr, GLsizeiptr, const void*) = nullptr;
void(APIENTRY* glGetBufferSubData_)(GLenum, GLintptr, GLsizeiptr, void*) = nullptr;
void(APIENTRY* glBindBufferBase_)(GLenum, GLuint, GLuint) = nullptr;

void(APIENTRY* glEnableVertexAttribArray_)(GLuint) = nullptr;
void(APIENTRY* glVertexAttribPointer_)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;

void(APIENTRY* glGenFramebuffers_)(GLsizei, GLuint*) = nullptr;
void(APIENTRY* glDeleteFramebuffers_)(GLsizei, const GLuint*) = nullptr;
void(APIENTRY* glBindFramebuffer_)(GLenum, GLuint) = nullptr;
void(APIENTRY* glFramebufferTexture2D_)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
void(APIENTRY* glBlitFramebuffer_)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) = nullptr;
GLenum(APIENTRY* glCheckFramebufferStatus_)(GLenum) = nullptr;

void(APIENTRY* glActiveTexture_)(GLenum) = nullptr;
void(APIENTRY* glTexStorage2D_)(GLenum, GLsizei, GLenum, GLsizei, GLsizei) = nullptr;
void(APIENTRY* glTexStorage3D_)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei) = nullptr;
void(APIENTRY* glTexSubImage3D_)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*) = nullptr;
void(APIENTRY* glBindImageTexture_)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum) = nullptr;
void(APIENTRY* glDrawBuffers_)(GLsizei, const GLenum*) = nullptr;

void(APIENTRY* glDispatchCompute_)(GLuint, GLuint, GLuint) = nullptr;
void(APIENTRY* glMemoryBarrier_)(GLbitfield) = nullptr;
GLsync(APIENTRY* glFenceSync_)(GLenum, GLbitfield) = nullptr;
GLenum(APIENTRY* glClientWaitSync_)(GLsync, GLbitfield, GLuint64) = nullptr;
void(APIENTRY* glDeleteSync_)(GLsync) = nullptr;

bool LoadGLFunctions() {
    bool ok = true;

    #define LOAD(var, name)                                                         \
        var = reinterpret_cast<decltype(var)>(glfwGetProcAddress(name));            \
        if (!(var)) {                                                               \
            std::cerr << "[GLLoader] Failed to load required symbol: " << name      \
                      << "\n";                                                      \
            ok = false;                                                             \
        }

    LOAD(glCreateShader_, "glCreateShader");
    LOAD(glDeleteShader_, "glDeleteShader");
    LOAD(glShaderSource_, "glShaderSource");
    LOAD(glCompileShader_, "glCompileShader");
    LOAD(glGetShaderiv_, "glGetShaderiv");
    LOAD(glGetShaderInfoLog_, "glGetShaderInfoLog");

    LOAD(glCreateProgram_, "glCreateProgram");
    LOAD(glDeleteProgram_, "glDeleteProgram");
    LOAD(glAttachShader_, "glAttachShader");
    LOAD(glLinkProgram_, "glLinkProgram");
    LOAD(glUseProgram_, "glUseProgram");
    LOAD(glGetProgramiv_, "glGetProgramiv");
    LOAD(glGetProgramInfoLog_, "glGetProgramInfoLog");

    LOAD(glGetUniformLocation_, "glGetUniformLocation");
    LOAD(glUniform1i_, "glUniform1i");
    LOAD(glUniform1f_, "glUniform1f");
    LOAD(glUniform2f_, "glUniform2f");
    LOAD(glUniform2i_, "glUniform2i");
    LOAD(glUniform3f_, "glUniform3f");
    LOAD(glUniform4f_, "glUniform4f");
    LOAD(glUniform3fv_, "glUniform3fv");
    LOAD(glUniformMatrix4fv_, "glUniformMatrix4fv");

    LOAD(glGenVertexArrays_, "glGenVertexArrays");
    LOAD(glDeleteVertexArrays_, "glDeleteVertexArrays");
    LOAD(glBindVertexArray_, "glBindVertexArray");

    LOAD(glGenBuffers_, "glGenBuffers");
    LOAD(glDeleteBuffers_, "glDeleteBuffers");
    LOAD(glBindBuffer_, "glBindBuffer");
    LOAD(glBufferData_, "glBufferData");
    LOAD(glBufferSubData_, "glBufferSubData");
    LOAD(glGetBufferSubData_, "glGetBufferSubData");
    LOAD(glBindBufferBase_, "glBindBufferBase");

    LOAD(glEnableVertexAttribArray_, "glEnableVertexAttribArray");
    LOAD(glVertexAttribPointer_, "glVertexAttribPointer");

    LOAD(glGenFramebuffers_, "glGenFramebuffers");
    LOAD(glDeleteFramebuffers_, "glDeleteFramebuffers");
    LOAD(glBindFramebuffer_, "glBindFramebuffer");
    LOAD(glFramebufferTexture2D_, "glFramebufferTexture2D");
    LOAD(glBlitFramebuffer_, "glBlitFramebuffer");
    LOAD(glCheckFramebufferStatus_, "glCheckFramebufferStatus");

    LOAD(glActiveTexture_, "glActiveTexture");
    LOAD(glTexStorage2D_, "glTexStorage2D");
    LOAD(glTexStorage3D_, "glTexStorage3D");
    LOAD(glTexSubImage3D_, "glTexSubImage3D");
    LOAD(glBindImageTexture_, "glBindImageTexture");
    LOAD(glDrawBuffers_, "glDrawBuffers");

    LOAD(glDispatchCompute_, "glDispatchCompute");
    LOAD(glMemoryBarrier_, "glMemoryBarrier");
    LOAD(glFenceSync_, "glFenceSync");
    LOAD(glClientWaitSync_, "glClientWaitSync");
    LOAD(glDeleteSync_, "glDeleteSync");

    #undef LOAD

    if (ok) {
        std::cout << "[GLLoader] All required OpenGL 4.3 symbols loaded successfully.\n";
    }

    return ok;
}
