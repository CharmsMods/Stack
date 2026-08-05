#pragma once

#include "Renderer/GLLoader.h"

#include <exception>
#include <utility>

namespace Stack::Renderer {

// Owns one texture name while a render result is still being assembled.
// Release transfers the name into a longer-lived cache/result owner.
class ScopedGLTexture {
public:
    ScopedGLTexture() noexcept = default;
    explicit ScopedGLTexture(unsigned int texture) noexcept
        : m_Texture(texture) {}

    ~ScopedGLTexture() {
        Reset();
    }

    ScopedGLTexture(const ScopedGLTexture&) = delete;
    ScopedGLTexture& operator=(const ScopedGLTexture&) = delete;

    ScopedGLTexture(ScopedGLTexture&& other) noexcept
        : m_Texture(other.Release()) {}

    ScopedGLTexture& operator=(ScopedGLTexture&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    unsigned int Get() const noexcept {
        return m_Texture;
    }

    explicit operator bool() const noexcept {
        return m_Texture != 0;
    }

    unsigned int Release() noexcept {
        return std::exchange(m_Texture, 0u);
    }

    void Reset(unsigned int replacement = 0) noexcept {
        if (m_Texture != 0) {
            glDeleteTextures(1, &m_Texture);
        }
        m_Texture = replacement;
    }

private:
    unsigned int m_Texture = 0;
};

class ScopedGLProgram {
public:
    ScopedGLProgram() noexcept = default;
    explicit ScopedGLProgram(unsigned int program) noexcept
        : m_Program(program) {}

    ~ScopedGLProgram() {
        Reset();
    }

    ScopedGLProgram(const ScopedGLProgram&) = delete;
    ScopedGLProgram& operator=(const ScopedGLProgram&) = delete;

    ScopedGLProgram(ScopedGLProgram&& other) noexcept
        : m_Program(other.Release()) {}

    ScopedGLProgram& operator=(ScopedGLProgram&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    unsigned int Get() const noexcept {
        return m_Program;
    }

    explicit operator bool() const noexcept {
        return m_Program != 0;
    }

    unsigned int Release() noexcept {
        return std::exchange(m_Program, 0u);
    }

    void Reset(unsigned int replacement = 0) noexcept {
        if (m_Program != 0) {
            glDeleteProgram(m_Program);
        }
        m_Program = replacement;
    }

private:
    unsigned int m_Program = 0;
};

// Protects a result structure whose texture ownership changes throughout a
// long render routine. Normal returns preserve the result; exception unwinds
// release the currently owned, not-yet-published texture.
class ScopedOwnedGLTextureExceptionCleanup {
public:
    ScopedOwnedGLTextureExceptionCleanup(
        unsigned int& texture,
        bool& owned) noexcept
        : m_Texture(texture),
          m_Owned(owned),
          m_InitialExceptionCount(std::uncaught_exceptions()) {}

    ~ScopedOwnedGLTextureExceptionCleanup() {
        if (std::uncaught_exceptions() > m_InitialExceptionCount &&
            m_Owned &&
            m_Texture != 0) {
            glDeleteTextures(1, &m_Texture);
            m_Texture = 0;
            m_Owned = false;
        }
    }

    ScopedOwnedGLTextureExceptionCleanup(
        const ScopedOwnedGLTextureExceptionCleanup&) = delete;
    ScopedOwnedGLTextureExceptionCleanup& operator=(
        const ScopedOwnedGLTextureExceptionCleanup&) = delete;

private:
    unsigned int& m_Texture;
    bool& m_Owned;
    int m_InitialExceptionCount = 0;
};

} // namespace Stack::Renderer
