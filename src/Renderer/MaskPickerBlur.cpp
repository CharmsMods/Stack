#include "Renderer/MaskPickerBlur.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ToolDrawerShaders.h"
#include <array>
#include <cmath>

namespace Stack::Renderer::MaskPickerBlur {
namespace {
struct Region {ImGuiID slot=0;std::uint64_t revision=0;int x=0,y=0,w=0,h=0,tw=0,th=0,frame=-10;
    GLuint textures[2]{},fbos[2]{};};
std::array<Region,8> regions{};
GLuint program=0,vao=0;
void Release(Region& region){
    glDeleteFramebuffers(2,region.fbos);glDeleteTextures(2,region.textures);region={};
}
void Prepare(const ImDrawList*,const ImDrawCmd* cmd){
    auto& r=*static_cast<Region*>(cmd->UserCallbackData);
    GLState::FramebufferState saved(true);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,saved.drawFramebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,r.fbos[0]);
    glBlitFramebuffer(r.x,r.y,r.x+r.w,r.y+r.h,0,0,r.tw,r.th,GL_COLOR_BUFFER_BIT,GL_LINEAR);
    glUseProgram(program);glBindVertexArray(vao);glViewport(0,0,r.tw,r.th);
    glActiveTexture(GL_TEXTURE0);glBindSampler(0,0);glUniform1i(0,0);
    glUniform4f(2,0,0,1,1);glUniform2f(3,.0001f,.0001f);glUniform1f(4,4.f);
    for(int pass=0;pass<2;++pass){
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER,r.fbos[1-pass]);glBindTexture(GL_TEXTURE_2D,r.textures[pass]);
        glUniform2f(1,pass==0?1.f/r.tw:0,pass==1?1.f/r.th:0);glDrawArrays(GL_TRIANGLES,0,3);
    }
    saved.Restore(true);
}
}
void Draw(ImDrawList* draw,ImGuiID slot,ImVec2 low,ImVec2 high,std::uint64_t revision){
    const auto* view=ImGui::GetMainViewport();const auto scale=ImGui::GetIO().DisplayFramebufferScale;
    const int x=int(std::round((low.x-view->Pos.x)*scale.x)),y=int(std::round((view->Pos.y+view->Size.y-high.y)*scale.y));
    const int w=int(std::round((high.x-low.x)*scale.x)),h=int(std::round((high.y-low.y)*scale.y));
    if(w<=1 || h<=1)return;
    const int frame=ImGui::GetFrameCount();
    Region* found=nullptr;
    for(auto& r:regions)if(r.slot==slot){found=&r;break;}
    if(!found)for(auto& r:regions)if(r.frame<frame-1){Release(r);found=&r;break;}
    if(!found)return;
    auto& r=*found;
    bool refresh=r.frame<frame-1 || r.revision!=revision || r.x!=x || r.y!=y || r.w!=w || r.h!=h;
    const int tw=(w+5)/6,th=(h+5)/6;
    if(!program){
        program=GLHelpers::CreateShaderProgram(R"(#version 430 core
out vec2 uv;void main(){uv=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(uv*2.-1.,0,1);})",ToolDrawerShaders::Blur);
        if(!program)return;
        glGenVertexArrays(1,&vao);
    }
    if(r.tw!=tw || r.th!=th){
        GLState::FramebufferState saved;GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
        Release(r);
        for(int i=0;i<2;++i){r.textures[i]=GLHelpers::CreateEmptyTexture(tw,th);r.fbos[i]=GLHelpers::CreateFBO(r.textures[i]);}
        binding.Restore();saved.Restore();r.tw=tw;r.th=th;refresh=true;
    }
    r.slot=slot;r.frame=frame;r.x=x;r.y=y;r.w=w;r.h=h;r.revision=revision;
    if(refresh){draw->AddCallback(Prepare,&r);draw->AddCallback(ImDrawCallback_ResetRenderState,nullptr);}
    draw->AddImage((ImTextureID)(intptr_t)r.textures[0],low,high,{0,1},{1,0});
}
void Shutdown(){for(auto& r:regions)Release(r);if(program)glDeleteProgram(program);if(vao)glDeleteVertexArrays(1,&vao);program=vao=0;}
}

