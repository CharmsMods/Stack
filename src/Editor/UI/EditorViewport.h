#pragma once

class EditorModule;

class EditorViewport {
public:
    EditorViewport();
    ~EditorViewport();

    void Initialize();
    void Render(EditorModule* editor);

private:
    float m_ZoomLevel = 1.0f;
    float m_PanX = 0.0f;
    float m_PanY = 0.0f;
    bool  m_IsLocked = false;
    unsigned int m_CheckerTex = 0;
};
