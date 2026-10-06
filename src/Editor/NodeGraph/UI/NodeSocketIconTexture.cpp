#include "Editor/NodeGraph/UI/NodeSocketIconTexture.h"

#include "App/Resources/EmbeddedTabIcons.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"

#include <cstdio>

namespace Stack::Editor::NodeGraphUIVisuals {

NodeSocketIconTexture::~NodeSocketIconTexture() {
    Reset();
}

unsigned int NodeSocketIconTexture::GetImageSocketAperture() {
    if (m_ImageSocketAperture != 0) {
        return m_ImageSocketAperture;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_set_flip_vertically_on_load_thread(0);
    unsigned char* pixels = stbi_load_from_memory(
        EmbeddedTabIcons::GraphImageSocketAperture_png_data,
        static_cast<int>(EmbeddedTabIcons::GraphImageSocketAperture_png_size),
        &width,
        &height,
        &channels,
        4);
    if (!pixels) {
        std::fprintf(stderr, "[NodeSocketIconTexture] Failed to decode the image socket icon.\n");
        return 0;
    }

    m_ImageSocketAperture = GLHelpers::CreateTextureFromPixels(
        pixels, width, height, 4);
    stbi_image_free(pixels);
    return m_ImageSocketAperture;
}

void NodeSocketIconTexture::Reset() {
    if (m_ImageSocketAperture != 0) {
        glDeleteTextures(1, &m_ImageSocketAperture);
        m_ImageSocketAperture = 0;
    }
}

} // namespace Stack::Editor::NodeGraphUIVisuals
