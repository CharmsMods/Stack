#pragma once
#include <vector>
#include <memory>

class EditorModule;

class EditorScopes {
public:
    EditorScopes();
    ~EditorScopes();

    void Initialize();
    void Render(EditorModule* editor);

private:
    void AnalyzePixels(EditorModule* editor);
    
    // UI Drawing Helpers
    void DrawHistogram();
    void DrawVectorscope();
    void DrawRGBParade();

    // Data buffers
    std::vector<float> m_HistR, m_HistG, m_HistB, m_HistL;
    
    struct ParadeColumn {
        float r[256], g[256], b[256];
    };
    std::vector<ParadeColumn> m_ParadeData;
    
    struct VectorPoint {
        float u, v;
    };
    std::vector<VectorPoint> m_VectorPoints;

    int m_LastWidth = 0;
    int m_LastHeight = 0;
    
    // Throttling
    float m_UpdateTimer = 0.0f;
    const float m_UpdateInterval = 0.05f; // ~20fps analysis
};
