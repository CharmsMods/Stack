#pragma once

#include "Renderer/GLLoader.h"

namespace Stack::Renderer::GLState {

struct FramebufferState {
    GLint readFramebuffer = 0;
    GLint drawFramebuffer = 0;
    GLint readBuffer = 0;
    GLint drawBuffer = 0;
    GLint viewport[4] = { 0, 0, 0, 0 };
    bool hasViewport = false;

    explicit FramebufferState(bool captureViewport = false)
        : hasViewport(captureViewport) {
        glGetIntegerv(
            GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        glGetIntegerv(
            GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glGetIntegerv(GL_READ_BUFFER, &readBuffer);
        glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
        if (captureViewport) {
            glGetIntegerv(GL_VIEWPORT, viewport);
        }
    }

    void Restore(bool restoreViewport = false) const {
        glBindFramebuffer(
            GL_READ_FRAMEBUFFER,
            static_cast<GLuint>(readFramebuffer));
        glBindFramebuffer(
            GL_DRAW_FRAMEBUFFER,
            static_cast<GLuint>(drawFramebuffer));
        glReadBuffer(static_cast<GLenum>(readBuffer));
        glDrawBuffer(static_cast<GLenum>(drawBuffer));
        if (restoreViewport && hasViewport) {
            glViewport(
                viewport[0], viewport[1],
                viewport[2], viewport[3]);
        }
    }
};

struct PixelPackState {
    GLint buffer = 0;
    GLint alignment = 4;
    GLint rowLength = 0;
    GLint skipRows = 0;
    GLint skipPixels = 0;

    PixelPackState() {
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &buffer);
        glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
    }

    void ConfigureTightCpuReadback() const {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    }

    void Restore() const {
        glBindBuffer(
            GL_PIXEL_PACK_BUFFER,
            static_cast<GLuint>(buffer));
        glPixelStorei(GL_PACK_ALIGNMENT, alignment);
        glPixelStorei(GL_PACK_ROW_LENGTH, rowLength);
        glPixelStorei(GL_PACK_SKIP_ROWS, skipRows);
        glPixelStorei(GL_PACK_SKIP_PIXELS, skipPixels);
    }
};

struct PixelUnpackState {
    GLint buffer = 0;
    GLint alignment = 4;
    GLint rowLength = 0;
    GLint skipRows = 0;
    GLint skipPixels = 0;

    PixelUnpackState() {
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &skipRows);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &skipPixels);
    }

    void ConfigureTightCpuUpload() const {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    }

    void Restore() const {
        glBindBuffer(
            GL_PIXEL_UNPACK_BUFFER,
            static_cast<GLuint>(buffer));
        glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, skipRows);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, skipPixels);
    }
};

struct TextureBinding {
    GLenum target = GL_TEXTURE_2D;
    GLint binding = 0;

    TextureBinding(GLenum textureTarget, GLenum bindingQuery)
        : target(textureTarget) {
        glGetIntegerv(bindingQuery, &binding);
    }

    void Restore() const {
        glBindTexture(target, static_cast<GLuint>(binding));
    }
};

} // namespace Stack::Renderer::GLState
