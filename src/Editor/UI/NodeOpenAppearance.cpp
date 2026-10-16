#include "Editor/UI/NodeOpenTransition.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ViewportTextureCopy.h"
#include <unordered_map>

namespace Stack::Editor {
NodeAppearanceTexture::~NodeAppearanceTexture() {
    if (name) glDeleteTextures(1, &name);
}

void NodeOpenTransition::Appearance::Capture(const ImDrawList& list, int firstVertex, ImVec2 low, ImVec2 high) {
    minimum=low; maximum=high; meshes.clear();
    const auto font=ImGui::GetIO().Fonts->TexRef.GetTexID();
    std::unordered_map<ImTextureID,std::shared_ptr<NodeAppearanceTexture>> textures;
    for (const auto& command:list.CmdBuffer) {
        if (command.UserCallback) continue;
        Mesh mesh; mesh.clip=command.ClipRect;
        for (unsigned i=0;i+2<command.ElemCount;i+=3) {
            unsigned indices[3]; bool owned=true;
            for (int corner=0;corner<3;++corner) {
                indices[corner]=command.VtxOffset+list.IdxBuffer[command.IdxOffset+i+corner];
                owned &= indices[corner]>=static_cast<unsigned>(firstVertex) &&
                    indices[corner]<static_cast<unsigned>(list.VtxBuffer.Size);
            }
            if (owned) for (const auto index:indices) mesh.vertices.push_back(list.VtxBuffer[index]);
        }
        if (mesh.vertices.empty()) continue;
        const auto id=command.GetTexID();
        if (id!=font) {
            // A ready preview can replace the placeholder after the first visit.
            // Keep it in the morph, independently of later preview replacement.
            auto& copy=textures[id];
            if (!copy && id) {
                const Renderer::GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
                glBindTexture(GL_TEXTURE_2D,static_cast<unsigned int>(id));
                GLint width=0,height=0;
                glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);
                glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
                binding.Restore();
                const float scale=std::min(1.f,512.f/std::max(1,std::max(width,height)));
                copy=std::make_shared<NodeAppearanceTexture>();
                copy->name=Renderer::CopyViewportTexture(static_cast<unsigned int>(id),width,height,
                    std::max(1,int(width*scale)),std::max(1,int(height*scale)));
            }
            if (!copy || !copy->name) continue;
            mesh.texture=copy;
        }
        meshes.push_back(std::move(mesh));
    }
}
}
