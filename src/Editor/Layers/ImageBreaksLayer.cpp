#include "ImageBreaksLayer.h"

#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_ImageBreaksVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_ImageBreaksFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uCols;
uniform float uRows;
uniform float uShiftX;
uniform float uShiftY;
uniform float uShiftBlur;
uniform float uSeed;
uniform float uSquareDensity;
uniform float uGridSize;
uniform float uSquareDistance;
uniform float uSquareBlur;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec2 uv = vUV;

    float colIndex = floor(uv.x * max(1.0, uCols));
    float rowIndex = floor(uv.y * max(1.0, uRows));

    float randX = hash12(vec2(rowIndex, uSeed)) * 2.0 - 1.0;
    float randY = hash12(vec2(colIndex, uSeed + 1.23)) * 2.0 - 1.0;

    vec2 shiftedUV = uv + vec2(
        randX * uShiftX,
        randY * uShiftY
    );

    if (uSquareDensity > 0.0) {
        vec2 squareIndex = floor(uv * max(1.0, uGridSize));
        float squareHash = hash12(squareIndex + uSeed * 13.37);

        if (squareHash < uSquareDensity) {
            float blockX = hash12(squareIndex + 42.0) * 2.0 - 1.0;
            float blockY = hash12(squareIndex + 99.0) * 2.0 - 1.0;
            shiftedUV += vec2(blockX, blockY) * uSquareDistance;
        }
    }

    shiftedUV = fract(shiftedUV);

    float blurRadius = 0.0;

    if (uShiftBlur > 0.0) {
        float shiftEdgeDist = 1.0;
        if (uShiftX > 0.0) {
            float rowFract = fract(uv.y * max(1.0, uRows));
            shiftEdgeDist = min(shiftEdgeDist, min(rowFract, 1.0 - rowFract));
        }
        if (uShiftY > 0.0) {
            float colFract = fract(uv.x * max(1.0, uCols));
            shiftEdgeDist = min(shiftEdgeDist, min(colFract, 1.0 - colFract));
        }

        if (shiftEdgeDist < 1.0) {
            float edgeThickness = 0.1 * uShiftBlur;
            if (shiftEdgeDist < edgeThickness) {
                float intensity = 1.0 - (shiftEdgeDist / max(edgeThickness, 0.0001));
                blurRadius = max(blurRadius, intensity * uShiftBlur * 0.015);
            }
        }
    }

    if (uSquareDensity > 0.0 && uSquareBlur > 0.0) {
        vec2 squareIndex = floor(uv * max(1.0, uGridSize));
        float squareHash = hash12(squareIndex + uSeed * 13.37);

        if (squareHash < uSquareDensity) {
            float edgeX = min(fract(uv.x * max(1.0, uGridSize)), 1.0 - fract(uv.x * max(1.0, uGridSize)));
            float edgeY = min(fract(uv.y * max(1.0, uGridSize)), 1.0 - fract(uv.y * max(1.0, uGridSize)));
            float squareEdgeDist = min(edgeX, edgeY);
            float edgeThickness = 0.1 * uSquareBlur;
            if (squareEdgeDist < edgeThickness) {
                float intensity = 1.0 - (squareEdgeDist / max(edgeThickness, 0.0001));
                blurRadius = max(blurRadius, intensity * uSquareBlur * 0.015);
            }
        }
    }

    if (blurRadius > 0.0) {
        vec4 color = vec4(0.0);
        float weight = 0.0;
        for (float x = -1.0; x <= 1.0; x += 1.0) {
            for (float y = -1.0; y <= 1.0; y += 1.0) {
                vec2 tapUV = fract(shiftedUV + vec2(x, y) * blurRadius);
                color += texture(uInputTex, tapUV);
                weight += 1.0;
            }
        }
        FragColor = color / max(weight, 0.0001);
    } else {
        FragColor = texture(uInputTex, shiftedUV);
    }
}
)";

ImageBreaksLayer::ImageBreaksLayer() {}

ImageBreaksLayer::~ImageBreaksLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void ImageBreaksLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_ImageBreaksVert, s_ImageBreaksFrag);
}

void ImageBreaksLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;

    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uCols"), m_Columns);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uRows"), m_Rows);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uShiftX"), m_ShiftX);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uShiftY"), m_ShiftY);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uShiftBlur"), m_ShiftBlur);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSeed"), m_Seed);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSquareDensity"), m_SquareDensity);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uGridSize"), m_GridSize);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSquareDistance"), m_SquareDistance);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSquareBlur"), m_SquareBlur);

    quad.Draw();
    glUseProgram(0);
}

void ImageBreaksLayer::RenderUI() {
    ImGui::SliderFloat("Columns", &m_Columns, 1.0f, 200.0f, "%.0f");
    ImGui::SliderFloat("Rows", &m_Rows, 1.0f, 200.0f, "%.0f");
    ImGui::SliderFloat("Horizontal Shift", &m_ShiftX, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Vertical Shift", &m_ShiftY, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Shift Edge Blur", &m_ShiftBlur, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Random Seed", &m_Seed, 0.0f, 100.0f, "%.0f");
    ImGui::Separator();
    ImGui::TextDisabled("Square Block Displacements");
    ImGui::SliderFloat("Square Density", &m_SquareDensity, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Grid Size", &m_GridSize, 1.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Square Distance", &m_SquareDistance, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Square Edge Blur", &m_SquareBlur, 0.0f, 1.0f, "%.2f");
}

json ImageBreaksLayer::Serialize() const {
    json j;
    j["type"] = "ImageBreaks";
    j["columns"] = m_Columns;
    j["rows"] = m_Rows;
    j["shiftX"] = m_ShiftX;
    j["shiftY"] = m_ShiftY;
    j["shiftBlur"] = m_ShiftBlur;
    j["seed"] = m_Seed;
    j["squareDensity"] = m_SquareDensity;
    j["gridSize"] = m_GridSize;
    j["squareDistance"] = m_SquareDistance;
    j["squareBlur"] = m_SquareBlur;
    return j;
}

void ImageBreaksLayer::Deserialize(const json& j) {
    if (j.contains("columns")) m_Columns = j["columns"];
    if (j.contains("ibCols")) m_Columns = j["ibCols"];
    if (j.contains("rows")) m_Rows = j["rows"];
    if (j.contains("ibRows")) m_Rows = j["ibRows"];
    if (j.contains("shiftX")) m_ShiftX = j["shiftX"];
    if (j.contains("ibShiftX")) m_ShiftX = j["ibShiftX"];
    if (j.contains("shiftY")) m_ShiftY = j["shiftY"];
    if (j.contains("ibShiftY")) m_ShiftY = j["ibShiftY"];
    if (j.contains("shiftBlur")) m_ShiftBlur = j["shiftBlur"];
    if (j.contains("ibShiftBlur")) m_ShiftBlur = j["ibShiftBlur"];
    if (j.contains("seed")) m_Seed = j["seed"];
    if (j.contains("ibSeed")) m_Seed = j["ibSeed"];
    if (j.contains("squareDensity")) m_SquareDensity = j["squareDensity"];
    if (j.contains("ibSqDensity")) m_SquareDensity = j["ibSqDensity"];
    if (j.contains("gridSize")) m_GridSize = j["gridSize"];
    if (j.contains("ibSqGrid")) m_GridSize = j["ibSqGrid"];
    if (j.contains("squareDistance")) m_SquareDistance = j["squareDistance"];
    if (j.contains("ibSqDist")) m_SquareDistance = j["ibSqDist"];
    if (j.contains("squareBlur")) m_SquareBlur = j["squareBlur"];
    if (j.contains("ibSqBlur")) m_SquareBlur = j["ibSqBlur"];
}
