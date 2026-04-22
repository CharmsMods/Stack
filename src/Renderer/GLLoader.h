#pragma once

// Lightweight OpenGL 4.3 Core function loader.
// Uses glfwGetProcAddress to load extension functions beyond the 1.1 export table.

#include <GLFW/glfw3.h>
#include <stddef.h>

#ifndef APIENTRY
  #ifdef _WIN32
    #define APIENTRY __stdcall
  #else
    #define APIENTRY
  #endif
#endif

typedef char GLchar;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;
typedef unsigned long long GLuint64;
#ifndef GLsync
typedef struct __GLsync* GLsync;
#endif

#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER 0x8B30
#endif
#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#endif
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif
#ifndef GL_DYNAMIC_DRAW
#define GL_DYNAMIC_DRAW 0x88E8
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_COLOR_ATTACHMENT1
#define GL_COLOR_ATTACHMENT1 0x8CE1
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_FRAMEBUFFER_BINDING
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8919
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TEXTURE1
#define GL_TEXTURE1 0x84C1
#endif
#ifndef GL_TEXTURE2
#define GL_TEXTURE2 0x84C2
#endif
#ifndef GL_TEXTURE3
#define GL_TEXTURE3 0x84C3
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RGB
#define GL_RGB 0x1907
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_TEXTURE_2D_ARRAY
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif
#ifndef GL_DEPTH_COMPONENT
#define GL_DEPTH_COMPONENT 0x1902
#endif
#ifndef GL_UNSIGNED_INT
#define GL_UNSIGNED_INT 0x1405
#endif
#ifndef GL_WRITE_ONLY
#define GL_WRITE_ONLY 0x88B9
#endif
#ifndef GL_READ_ONLY
#define GL_READ_ONLY 0x88B8
#endif
#ifndef GL_READ_WRITE
#define GL_READ_WRITE 0x88BA
#endif
#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif
#ifndef GL_TEXTURE_FETCH_BARRIER_BIT
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif
#ifndef GL_MAJOR_VERSION
#define GL_MAJOR_VERSION 0x821B
#endif
#ifndef GL_MINOR_VERSION
#define GL_MINOR_VERSION 0x821C
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED 0x911A
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911B
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED 0x911C
#endif
#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED 0x911D
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif

// Shaders
extern GLuint(APIENTRY* glCreateShader_)(GLenum type);
extern void(APIENTRY* glDeleteShader_)(GLuint shader);
extern void(APIENTRY* glShaderSource_)(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length);
extern void(APIENTRY* glCompileShader_)(GLuint shader);
extern void(APIENTRY* glGetShaderiv_)(GLuint shader, GLenum pname, GLint* params);
extern void(APIENTRY* glGetShaderInfoLog_)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);

// Programs
extern GLuint(APIENTRY* glCreateProgram_)(void);
extern void(APIENTRY* glDeleteProgram_)(GLuint program);
extern void(APIENTRY* glAttachShader_)(GLuint program, GLuint shader);
extern void(APIENTRY* glLinkProgram_)(GLuint program);
extern void(APIENTRY* glUseProgram_)(GLuint program);
extern void(APIENTRY* glGetProgramiv_)(GLuint program, GLenum pname, GLint* params);
extern void(APIENTRY* glGetProgramInfoLog_)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);

// Uniforms
extern GLint(APIENTRY* glGetUniformLocation_)(GLuint program, const GLchar* name);
extern void(APIENTRY* glUniform1i_)(GLint location, GLint v0);
extern void(APIENTRY* glUniform1f_)(GLint location, GLfloat v0);
extern void(APIENTRY* glUniform2f_)(GLint location, GLfloat v0, GLfloat v1);
extern void(APIENTRY* glUniform2i_)(GLint location, GLint v0, GLint v1);
extern void(APIENTRY* glUniform3f_)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
extern void(APIENTRY* glUniform4f_)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
extern void(APIENTRY* glUniform3fv_)(GLint location, GLsizei count, const GLfloat* value);
extern void(APIENTRY* glUniformMatrix4fv_)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);

// Vertex arrays
extern void(APIENTRY* glGenVertexArrays_)(GLsizei n, GLuint* arrays);
extern void(APIENTRY* glDeleteVertexArrays_)(GLsizei n, const GLuint* arrays);
extern void(APIENTRY* glBindVertexArray_)(GLuint array);

// Buffers
extern void(APIENTRY* glGenBuffers_)(GLsizei n, GLuint* buffers);
extern void(APIENTRY* glDeleteBuffers_)(GLsizei n, const GLuint* buffers);
extern void(APIENTRY* glBindBuffer_)(GLenum target, GLuint buffer);
extern void(APIENTRY* glBufferData_)(GLenum target, ptrdiff_t size, const void* data, GLenum usage);
extern void(APIENTRY* glBufferSubData_)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
extern void(APIENTRY* glGetBufferSubData_)(GLenum target, GLintptr offset, GLsizeiptr size, void* data);
extern void(APIENTRY* glBindBufferBase_)(GLenum target, GLuint index, GLuint buffer);

// Vertex attribs
extern void(APIENTRY* glEnableVertexAttribArray_)(GLuint index);
extern void(APIENTRY* glVertexAttribPointer_)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);

// Framebuffers
extern void(APIENTRY* glGenFramebuffers_)(GLsizei n, GLuint* framebuffers);
extern void(APIENTRY* glDeleteFramebuffers_)(GLsizei n, const GLuint* framebuffers);
extern void(APIENTRY* glBindFramebuffer_)(GLenum target, GLuint framebuffer);
extern void(APIENTRY* glFramebufferTexture2D_)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
extern void(APIENTRY* glBlitFramebuffer_)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
extern GLenum(APIENTRY* glCheckFramebufferStatus_)(GLenum target);

// Textures / images
extern void(APIENTRY* glActiveTexture_)(GLenum texture);
extern void(APIENTRY* glTexStorage2D_)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
extern void(APIENTRY* glTexStorage3D_)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth);
extern void(APIENTRY* glTexSubImage3D_)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const void* pixels);
extern void(APIENTRY* glBindImageTexture_)(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access, GLenum format);
extern void(APIENTRY* glDrawBuffers_)(GLsizei n, const GLenum* bufs);

// Compute / synchronization
extern void(APIENTRY* glDispatchCompute_)(GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
extern void(APIENTRY* glMemoryBarrier_)(GLbitfield barriers);
extern GLsync(APIENTRY* glFenceSync_)(GLenum condition, GLbitfield flags);
extern GLenum(APIENTRY* glClientWaitSync_)(GLsync sync, GLbitfield flags, GLuint64 timeout);
extern void(APIENTRY* glDeleteSync_)(GLsync sync);

#define glCreateShader glCreateShader_
#define glDeleteShader glDeleteShader_
#define glShaderSource glShaderSource_
#define glCompileShader glCompileShader_
#define glGetShaderiv glGetShaderiv_
#define glGetShaderInfoLog glGetShaderInfoLog_
#define glCreateProgram glCreateProgram_
#define glDeleteProgram glDeleteProgram_
#define glAttachShader glAttachShader_
#define glLinkProgram glLinkProgram_
#define glUseProgram glUseProgram_
#define glGetProgramiv glGetProgramiv_
#define glGetProgramInfoLog glGetProgramInfoLog_
#define glGetUniformLocation glGetUniformLocation_
#define glUniform1i glUniform1i_
#define glUniform1f glUniform1f_
#define glUniform2f glUniform2f_
#define glUniform2i glUniform2i_
#define glUniform3f glUniform3f_
#define glUniform4f glUniform4f_
#define glUniform3fv glUniform3fv_
#define glUniformMatrix4fv glUniformMatrix4fv_
#define glGenVertexArrays glGenVertexArrays_
#define glDeleteVertexArrays glDeleteVertexArrays_
#define glBindVertexArray glBindVertexArray_
#define glGenBuffers glGenBuffers_
#define glDeleteBuffers glDeleteBuffers_
#define glBindBuffer glBindBuffer_
#define glBufferData glBufferData_
#define glBufferSubData glBufferSubData_
#define glGetBufferSubData glGetBufferSubData_
#define glBindBufferBase glBindBufferBase_
#define glEnableVertexAttribArray glEnableVertexAttribArray_
#define glVertexAttribPointer glVertexAttribPointer_
#define glGenFramebuffers glGenFramebuffers_
#define glDeleteFramebuffers glDeleteFramebuffers_
#define glBindFramebuffer glBindFramebuffer_
#define glFramebufferTexture2D glFramebufferTexture2D_
#define glBlitFramebuffer glBlitFramebuffer_
#define glCheckFramebufferStatus glCheckFramebufferStatus_
#define glActiveTexture glActiveTexture_
#define glTexStorage2D glTexStorage2D_
#define glTexStorage3D glTexStorage3D_
#define glTexSubImage3D glTexSubImage3D_
#define glBindImageTexture glBindImageTexture_
#define glDrawBuffers glDrawBuffers_
#define glDispatchCompute glDispatchCompute_
#define glMemoryBarrier glMemoryBarrier_
#define glFenceSync glFenceSync_
#define glClientWaitSync glClientWaitSync_
#define glDeleteSync glDeleteSync_

// Call this after glfwMakeContextCurrent().
bool LoadGLFunctions();
