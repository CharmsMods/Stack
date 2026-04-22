#pragma once

#include "Persistence/StackBinaryFormat.h"
#include <string>
#include <vector>

enum class LayerKind {
    Image,
    EditorProject
};

struct CompositeLayer {
    std::string id;
    std::string name;
    LayerKind kind = LayerKind::Image;
    float x = 0.0f;
    float y = 0.0f;
    float scale = 1.0f;
    float rotation = 0.0f;
    float opacity = 1.0f;
    int z = 0;
    bool visible = true;
    int imgW = 0;
    int imgH = 0;
    int logicalW = 0;
    int logicalH = 0;
    std::vector<uint8_t> rgba;
    unsigned int tex = 0;

    // For EditorProject kind:
    std::string embeddedProjectJson;
    std::vector<uint8_t> sourceImagePng;
};

class CompositeModule {
public:
    CompositeModule();
    ~CompositeModule();

    void Initialize();
    void Shutdown();
    void RenderUI();

    void NewProject();

    bool HasLayers() const;
    bool IsDirty() const { return m_Dirty; }
    void ClearDirty() { m_Dirty = false; }

    const std::string& GetCurrentProjectName() const { return m_ProjectName; }
    const std::string& GetCurrentProjectFileName() const { return m_ProjectFileName; }
    void SetCurrentProjectName(const std::string& name) { m_ProjectName = name; }
    void SetCurrentProjectFileName(const std::string& fileName) { m_ProjectFileName = fileName; }

    bool ApplyLibraryProject(const StackBinaryFormat::ProjectDocument& document);
    bool BuildProjectDocumentForSave(
        const std::string& displayName,
        StackBinaryFormat::ProjectDocument& outDocument) const;

    // Bridge Support
    CompositeLayer* GetSelectedLayer();
    void UpdateLayerData(const std::string& layerId, const std::string& projectJson, const std::vector<uint8_t>& previewPixels, int w, int h);
    void AddProjectLayer(const std::string& name, const std::string& projectJson, const std::vector<uint8_t>& previewPixels, int texW, int texH, int logicalW, int logicalH);

private:
    void RenderLibraryPicker();
    static void ResizeNearest(const std::vector<uint8_t>& src, int sw, int sh, int dw, int dh, std::vector<uint8_t>& dst);
    void ClearLayersGpu();
    void SyncLayerTextures();
    void AddImageLayerFromFile(const std::string& path);
    void AddProjectLayerFromFile(const std::string& path);
    void RemoveSelectedLayers();
    void RasterizeCompositeRgba(std::vector<uint8_t>& outRgba, int& outW, int& outH) const;
    void EncodePng(const std::vector<uint8_t>& rgba, int w, int h, std::vector<uint8_t>& outPng) const;

    std::vector<CompositeLayer> m_Layers;
    std::string m_SelectedId;
    float m_ViewZoom = 1.0f;
    float m_ViewPanX = 0.0f;
    float m_ViewPanY = 0.0f;
    bool m_ShowChecker = true;
    
    enum class HandleType { None, Move, Rotate, ScaleTR, ScaleTL, ScaleBR, ScaleBL };
    HandleType m_ActiveHandle = HandleType::None;
    float m_StartScale = 1.0f;
    float m_StartRotation = 0.0f;
    float m_StartX = 0.0f;
    float m_StartY = 0.0f;
    float m_StartMouseAngle = 0.0f;
    float m_StartMouseDist = 1.0f;

    bool m_SnapEnabled = true;
    bool m_ShowLibraryPicker = false;
    bool m_LimitProjectResolution = true;
    float m_GridSize = 24.0f;
    float m_RotateSnapStep = 15.0f; // degrees
    float m_ScaleSnapStep = 0.1f;

    float m_CanvasW = 1000.0f;
    float m_CanvasH = 1000.0f;

    std::string m_ProjectName = "Untitled Composite";
    std::string m_ProjectFileName;
    bool m_Dirty = false;
    bool m_Initialized = false;
};
