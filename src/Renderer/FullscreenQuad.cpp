#include "FullscreenQuad.h"
#include "Async/UiInteraction.h"
#include "Renderer/CooperativeGpuScheduling.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>

FullscreenQuad::FullscreenQuad() : m_VAO(0), m_VBO(0) {}

FullscreenQuad::~FullscreenQuad() {
    Shutdown();
}

void FullscreenQuad::Initialize() {
    if (m_VAO != 0 && m_VBO != 0) {
        return;
    }
    Shutdown();

    // Two triangles covering the full NDC space (-1 to 1), with UV coords (0 to 1)
    float quadVertices[] = {
        // positions   // texCoords
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &m_VAO);
    glGenBuffers(1, &m_VBO);

    glBindVertexArray(m_VAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // Position attribute (location=0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    // TexCoord attribute (location=1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void FullscreenQuad::Shutdown() {
    m_Cancellation={};
    m_Submission={};
    if (m_VBO) {
        glDeleteBuffers(1, &m_VBO);
        m_VBO = 0;
    }
    if (m_VAO) {
        glDeleteVertexArrays(1, &m_VAO);
        m_VAO = 0;
    }
}

void FullscreenQuad::Draw() {
    glBindVertexArray(m_VAO);
    GLint viewport[4]{};
    if(m_Cancellation)glGetIntegerv(GL_VIEWPORT,viewport);
    const bool interacting = Async::UiInteraction::Active();
    if(!m_Cancellation || std::int64_t(viewport[2])*viewport[3] <=
            Stack::Renderer::CooperativeRasterPixels(interacting)) {
        if (m_Cancellation && m_Cancellation()) {
            glBindVertexArray(0);
            throw std::runtime_error("Render superseded by a newer snapshot.");
        }
        glDrawArrays(GL_TRIANGLES,0,6);
        if (m_Submission && m_Submission()) {
            glBindVertexArray(0);
            throw std::runtime_error("Render superseded by a newer snapshot.");
        }
    } else {
        const bool scissor=glIsEnabled(GL_SCISSOR_TEST)==GL_TRUE;
        GLint previous[4]{};glGetIntegerv(GL_SCISSOR_BOX,previous);
        const int left=scissor ? std::max(viewport[0],previous[0]) : viewport[0];
        const int bottom=scissor ? std::max(viewport[1],previous[1]) : viewport[1];
        const int right=scissor ? std::min(viewport[0]+viewport[2],previous[0]+previous[2]) : viewport[0]+viewport[2];
        const int top=scissor ? std::min(viewport[1]+viewport[3],previous[1]+previous[3]) : viewport[1]+viewport[3];
        glEnable(GL_SCISSOR_TEST);
        bool canceled=false;
        const int tileWidth = Stack::Renderer::kCooperativeRasterTileWidth;
        for(int y=bottom;y<top && !canceled;) {
            // Input can begin during an idle pass. Resize subsequent rows
            // without changing the pixel grid or revisiting submitted pixels.
            const int tileHeight = std::min(top-y,
                Stack::Renderer::CooperativeRasterTileHeight(Async::UiInteraction::Active()));
            for(int x=left;x<right;x+=tileWidth) {
                if(m_Cancellation()){canceled=true;break;}
                glScissor(x,y,std::min(tileWidth,right-x),tileHeight);
                glDrawArrays(GL_TRIANGLES,0,6);
                if(m_Submission && m_Submission()){canceled=true;break;}
            }
            y+=tileHeight;
        }
        glScissor(previous[0],previous[1],previous[2],previous[3]);
        if(!scissor)glDisable(GL_SCISSOR_TEST);
        if(canceled) {
            glBindVertexArray(0);
            throw std::runtime_error("Render superseded by a newer snapshot.");
        }
    }
    glBindVertexArray(0);
}
