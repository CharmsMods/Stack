#pragma once

#include "Renderer/GLHelpers.h"

// The full-screen quad geometry used by every fragment shader to draw onto.
// Matches the role of vs-quad.vert from the web version.
class FullscreenQuad {
public:
    FullscreenQuad();
    ~FullscreenQuad();

    void Initialize();
    void Draw();

private:
    unsigned int m_VAO;
    unsigned int m_VBO;
};
