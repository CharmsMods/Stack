#pragma once

namespace Stack::Editor::NodeGraphUIVisuals {

class NodeSocketIconTexture {
public:
    NodeSocketIconTexture() = default;
    ~NodeSocketIconTexture();

    NodeSocketIconTexture(const NodeSocketIconTexture&) = delete;
    NodeSocketIconTexture& operator=(const NodeSocketIconTexture&) = delete;

    unsigned int GetImageSocketAperture();
    void Reset();

private:
    unsigned int m_ImageSocketAperture = 0;
};

} // namespace Stack::Editor::NodeGraphUIVisuals
