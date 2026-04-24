#pragma once
#include <string>
#include "ThirdParty/json.hpp"

using json = nlohmann::json;

class FullscreenQuad;

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
    virtual void RenderUI(class EditorModule* editor) { RenderUI(); }

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
