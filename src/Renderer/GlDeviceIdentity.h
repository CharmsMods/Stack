#pragma once

#include "Renderer/GLLoader.h"
#include <array>

namespace Stack::Renderer {
struct GlDeviceIdentity {
    GLFWwindow* context = nullptr;
    std::array<unsigned char, 8> luid{};
    unsigned int nodeMask = 0;
    bool known = false;
};

inline thread_local GlDeviceIdentity currentGlDeviceIdentity;

// Refresh at context initialization, including recycled native/window handles.
// Budget queries then read the identity without recurring extension discovery.
inline void RefreshGlDeviceIdentity() {
    auto& identity = currentGlDeviceIdentity;
    identity = {};
    identity.context = glfwGetCurrentContext();
#if defined(_WIN32)
    if (!identity.context ||
        (!glfwExtensionSupported("GL_EXT_memory_object_win32") &&
         !glfwExtensionSupported("GL_EXT_semaphore_win32"))) return;
    const auto getLuid = reinterpret_cast<void (APIENTRY*)(GLenum, GLubyte*)>(
        glfwGetProcAddress("glGetUnsignedBytevEXT"));
    if (!getLuid) return;
    constexpr GLenum deviceLuid = 0x9599, deviceNodeMask = 0x959A;
    GLint mask = 0;
    getLuid(deviceLuid, identity.luid.data());
    glGetIntegerv(deviceNodeMask, &mask);
    identity.nodeMask = static_cast<unsigned int>(mask);
    identity.known = glGetError() == GL_NO_ERROR && mask != 0;
#endif
}

inline const GlDeviceIdentity& CurrentGlDeviceIdentity() {
    if (currentGlDeviceIdentity.context != glfwGetCurrentContext()) RefreshGlDeviceIdentity();
    return currentGlDeviceIdentity;
}
} // namespace Stack::Renderer
