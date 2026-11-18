#pragma once

#include "Renderer/GLHelpers.h"
#include <functional>
#include <utility>

// The full-screen quad geometry used by every fragment shader to draw onto.
// Matches the role of vs-quad.vert from the web version.
class FullscreenQuad {
public:
    FullscreenQuad();
    ~FullscreenQuad();

    void Initialize();
    void Shutdown();
    void Draw();
    // Request-scoped worker callback; an empty callback leaves ordinary UI
    // draws unchanged. Scissor batches preserve the raster grid, including
    // terminal exports that cooperate with active UI work without being lost.
    void SetCooperativeCancellation(std::function<bool()> callback) { m_Cancellation=std::move(callback); }
    // Separate GPU submission from cancellation polling during CPU preparation.
    // Return true to stop further tiles. The owning worker retains final drain.
    void SetCooperativeSubmission(std::function<bool()> callback) { m_Submission=std::move(callback); }

private:
    std::function<bool()> m_Cancellation;
    std::function<bool()> m_Submission;
    unsigned int m_VAO;
    unsigned int m_VBO;
};
