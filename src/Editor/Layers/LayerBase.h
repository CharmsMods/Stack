#pragma once
#include <string>
#include "ThirdParty/json.hpp"

using json = nlohmann::json;

class FullscreenQuad;
class EditorModule;

enum class NodeSurfacePresentation {
    CompactInline,
    RichExpandedSurface
};

enum class NodeSurfaceDensity {
    Normal,
    Dense,
    UltraDense
};

struct NodeSurfaceSpec {
    NodeSurfacePresentation presentation = NodeSurfacePresentation::CompactInline;
    NodeSurfaceDensity density = NodeSurfaceDensity::Normal;
    float preferredWidth = 334.0f;
    float maxWidth = 420.0f;
    bool usesCanvasTool = false;
};

struct NodeSurfaceContext {
    int nodeId = -1;
    float availableWidth = 0.0f;
    float safeContentWidth = 0.0f;
    float logicalAvailableWidth = 0.0f;
    float logicalSafeContentWidth = 0.0f;
    float layoutScale = 1.0f;
    float contentScale = 1.0f;
    float itemGap = 0.0f;
    float sectionGap = 0.0f;
    bool focused = false;
    NodeSurfaceDensity density = NodeSurfaceDensity::Normal;
    bool canvasToolActive = false;
    const char* canvasToolStatusText = nullptr;
};

// Abstract base class for all rendering layers in the sequential pipeline.
// Each layer receives the previous layer's output texture and renders into the target FBO.
class LayerBase {
public:
    virtual ~LayerBase() = default;

    virtual json Serialize() const = 0;
    virtual void Deserialize(const json& j) = 0;

    virtual const char* GetName() const { return m_InstanceName.empty() ? GetDefaultName() : m_InstanceName.c_str(); }
    virtual const char* GetDefaultName() const = 0;
    virtual const char* GetCategory() const = 0;

    void SetInstanceName(const std::string& name) { m_InstanceName = name; }

    // Compile any GPU shader programs this layer needs
    virtual void InitializeGL() = 0;

    // Render the layer's effect: read from inputTexture, draw into the currently bound FBO
    virtual void Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) = 0;

    // Optional: Render with access to the original source texture
    virtual void ExecuteWithSource(unsigned int inputTexture, unsigned int sourceTexture, int width, int height, FullscreenQuad& quad) {
        Execute(inputTexture, width, height, quad);
    }

    // Draw ImGui controls for this layer's parameters (shown in the Selected tab)
    virtual void RenderUI() = 0;
    virtual void RenderUI(EditorModule* editor) { RenderUI(); }
    virtual NodeSurfaceSpec GetNodeSurfaceSpec() const { return {}; }
    virtual void RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) {
        (void)context;
        RenderUI(editor);
    }

    // Whether this layer is enabled and should be processed
    bool IsEnabled() const { return m_Enabled; }
    void SetEnabled(bool enabled) { m_Enabled = enabled; }

    // Whether this layer is currently selected in the UI
    bool IsSelected() const { return m_Selected; }
    void SetSelected(bool selected) { m_Selected = selected; }

    // Visibility toggle (eye icon in Pipeline tab)
    bool IsVisible() const { return m_Visible; }
    void SetVisible(bool visible) { m_Visible = visible; }

protected:
    bool m_Enabled = true;
    bool m_Selected = false;
    bool m_Visible = true;
    std::string m_InstanceName;
};
