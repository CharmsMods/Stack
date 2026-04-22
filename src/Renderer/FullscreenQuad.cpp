#include "FullscreenQuad.h"

FullscreenQuad::FullscreenQuad() : m_VAO(0), m_VBO(0) {}

FullscreenQuad::~FullscreenQuad() {
    if (m_VBO) glDeleteBuffers(1, &m_VBO);
    if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
}

void FullscreenQuad::Initialize() {
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

void FullscreenQuad::Draw() {
    glBindVertexArray(m_VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}
