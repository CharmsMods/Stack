// SPDX-License-Identifier: GPL-3.0-only
// Optional Linux/EGL cloud component probe. This is NOT a Windows app test,
// shipping UI, or mandatory build target. It uses the existing pinned ImGui
// OpenGL backend, production motion adapters and GlassChrome on software EGL.
// Run with one existing output-directory argument; PPM frames are synthetic.
// BACKDROP=bright selects a flat backdrop, otherwise a patterned one is used.
// Keep checks and their side effects active even in release probe builds.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "App/EdgeChrome.h"
#include "Renderer/GLLoader.h"
#include "Animation/SliderMotion.h"
#include "App/ControlMotionAdapter.h"
#include "imgui_impl_opengl3.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <memory>
static bool reduced=false;

namespace Stack::Notifications { bool SystemReducedMotion(){return reduced;
    } }
extern "C" {void (*eglGetProcAddress(const char*))(void);
    unsigned eglInitialize(void*,int*,int*);
    unsigned eglBindAPI(unsigned);
    unsigned eglChooseConfig(void*,const int*,void**,int,int*);
    void* eglCreateContext(void*,void*,void*,const int*);
    unsigned eglMakeCurrent(void*,void*,void*,void*);
    unsigned eglDestroyContext(void*,void*);
    unsigned eglTerminate(void*);
    GLFWglproc glfwGetProcAddress(const char* name){return eglGetProcAddress(name);
}}
namespace {
// One immutable pre-object capture must not recursively include either neighbor.
// This isolated case reuses the existing ImGui/EGL context and framebuffer.
void CheckSharedBackdropCapture(GLuint framebuffer) {
    constexpr int width=640, height=360;
    Stack::Renderer::GlassChrome renderer;
    const ImVec4 tint(.09f,.12f,.15f,1.f);
    const auto palette=Stack::Header::ResolvePalette(tint);
    enum class Feature { None, Near, Far };
    auto render=[&](bool reverse, Feature feature) {
        renderer.BeginFrame(true);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        auto* background=ImGui::GetBackgroundDrawList();
        background->AddRectFilled({0,0},{width,height},IM_COL32(245,245,247,255));
        if (feature!=Feature::None) {
            // Near is outside the left silhouette but inside its16px capture
            // apron. Far is outside the shared padded capture entirely.
            const float x=feature==Feature::Near ? 247.f : 210.f;
            background->AddRectFilled({x,90},{x+2,128},IM_COL32(255,32,200,255));
        }
        ImGui::SetNextWindowPos({0,0});
        ImGui::SetNextWindowSize({width,height});
        ImGui::Begin("Shared capture regression",nullptr,
            ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|
            ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs);
        auto* draw=ImGui::GetWindowDrawList();
        draw->PushClipRect({0,0},{width,height},false);
        assert(renderer.AddBackdrop(draw,{250,90},{332,128},tint));
        for (int i=0;i<2;++i) {
            const int object=reverse ? 1-i : i;
            const ImVec2 min(250.f+44.f*object,90),max(288.f+44.f*object,128);
            Stack::Navigation::GlassObjectBacking(draw,min,max,19,palette);
            assert(renderer.AddObject(draw,min,max,tint,19,1.f,
                Stack::Renderer::GlassChrome::MaterialRole::NavigationControl));
        }
        draw->PopClipRect();
        ImGui::End();
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
        glViewport(0,0,width,height);
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        std::vector<unsigned char> pixels(width*height*4);
        glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        assert(renderer.Error().empty()&&glGetError()==GL_NO_ERROR);
        for (std::size_t i=3;i<pixels.size();i+=4) assert(pixels[i]==255);
        return pixels;
    };
    render(false,Feature::None); // Establish the fixed window before comparisons.
    const auto forward=render(false,Feature::None);
    const auto reverse=render(true,Feature::None);
    const auto near=render(false,Feature::Near);
    const auto far=render(false,Feature::Far);
    std::size_t nearChangedBodyPixels=0;
    for (int y=75;y<145;++y) for (int x=234;x<348;++x) {
        const std::size_t offset=((height-1-y)*width+x)*4;
        // Exactness is scoped to this six-pixel-gap fixture, not arbitrary
        // overlapping optical objects or differently colored shadows.
        for (int c=0;c<4;++c) assert(forward[offset+c]==reverse[offset+c]);
        const float dx=std::min(std::abs(x+.5f-269.f),std::abs(x+.5f-313.f));
        const float dy=y+.5f-109.f;
        if (dx*dx+dy*dy<18.f*18.f) { // Exclude the silhouette's antialias fringe.
            bool changed=false;
            for (int c=0;c<3;++c) {
                assert(forward[offset+c]==far[offset+c]);
                changed|=forward[offset+c]!=near[offset+c];
            }
            nearChangedBodyPixels+=changed ? 1 : 0;
        }
    }
    assert(nearChangedBodyPixels>0);
    renderer.Shutdown();
    assert(glGetError()==GL_NO_ERROR);
    std::cout<<"PASS shared capture: reversed pair crop exact, near apron feature changes "
        <<nearChangedBodyPixels<<" body pixels, far feature leaves body unchanged\n";
}
} // namespace

int main(int argc,char** argv){assert(argc==2);
    std::string folder=argv[1];
    setenv("LIBGL_ALWAYS_SOFTWARE","true",1);
    setenv("MESA_SHADER_CACHE_DISABLE","true",1);

    auto platform=reinterpret_cast<void*(*)(unsigned,void*,const int*)>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    assert(platform);
    void* d=platform(0x31DD,nullptr,nullptr);
    int a,b;
    assert(eglInitialize(d,&a,&b));
    assert(eglBindAPI(0x30A2));
    int attrs[]={0x3033,1,0x3040,8,0x3038};
    void* cfg=nullptr;
    int n;
    assert(eglChooseConfig(d,attrs,&cfg,1,&n)&&n);
    int ca[]={0x3098,4,0x30FB,3,0x30FD,1,0x3038};
    void* ctx=eglCreateContext(d,cfg,nullptr,ca);
    assert(ctx);
    assert(eglMakeCurrent(d,nullptr,nullptr,ctx));
    assert(LoadGLFunctions());
    std::cout<<glGetString(GL_RENDERER)<<std::endl;

    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize={640,360};
    io.DeltaTime=1.f/60;
    assert(ImGui_ImplOpenGL3_Init("#version 430 core"));

    GLuint tex,fbo;
    glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,640,360,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glGenFramebuffers(1,&fbo);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);

    Stack::Renderer::GlassChrome glass;
    Stack::Renderer::GlassChrome sliderGlass;
    std::unique_ptr<Stack::Animation::ControlMotion> firstMotion=std::make_unique<Stack::Animation::ControlMotion>();
    Stack::Animation::ControlMotion libraryMotion,queueMotion,panelMotion;
    std::vector<unsigned char> pixels(640*360*4);
    bool material=true;
    bool showSlider=false,sliderEnabled=true,sliderFocus=false;
    float sliderValue=.5f;
    Stack::Animation::SliderMotion sliderMotion;
    bool sliderChanged=false,sliderActive=false,sliderFocused=false;
    // Opt-in illustrative Stack demo parameters, NOT measured Apple motion.
    // These channels never write the native slider value or its hit rectangle.
    bool velocityDemo=false;
    Stack::Animation::ScalarChannel visualWidth(40.f), visualElevation(1.f);

    struct Result{bool click,active,hover,focus;
        float press,amount;
        ImGuiID id;
        ImRect rect;
    };

    auto frame=[&](float mx,float my,bool down,bool snap=false,bool focus=false,bool enabled=true){reduced=snap;
        io.AddMousePosEvent(mx,my);
        io.AddMouseButtonEvent(0,down);
        glass.BeginFrame(material);
        sliderGlass.BeginFrame(showSlider);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        auto* bg=ImGui::GetBackgroundDrawList();
        bg->AddRectFilled({0,0},{640,360},IM_COL32(120,140,150,255));
        for(int y=52;y<360;y+=16)for(int x=0;x<640;x+=16)if(((x/16+(y-52)/16)%2)==0)bg->AddRectFilled({float(x),float(y)},{float(x+16),float(y+16)},IM_COL32(205,215,218,255));
        if(const char* b=getenv("BACKDROP")){int c=std::string(b)=="bright"?208:31;
            bg->AddRectFilled({0,52},{640,360},IM_COL32(c,c,c,255));
        }
        bg->AddLine({0,78},{160,78},IM_COL32(255,25,150,255),1);
        bg->AddLine({0,69},{160,125},IM_COL32(255,190,10,255),1);
        bg->AddRectFilled({0,0},{640,52},IM_COL32(25,29,34,255));
        bg->AddText({12,18},IM_COL32_WHITE,"Cloud component render, not full Windows app");

        ImGui::SetNextWindowPos({0,52});
        ImGui::SetNextWindowSize({52,308});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{7,7});
        ImGui::Begin("Rail",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
        auto* dl=ImGui::GetWindowDrawList();
        dl->PushClipRect({0,0},{640,360},false);
        glass.AddBackdrop(dl,{0,52},{52,360},{.09f,.12f,.15f,.2f});
        dl->PopClipRect();
        auto pal=Stack::Header::ResolvePalette({.09f,.12f,.15f,1});
        if(focus)ImGui::SetKeyboardFocusHere();
        bool clicked=Stack::Navigation::IconButton("Stack",Stack::Navigation::Glyph::Stack,false,enabled,1,pal,*firstMotion,nullptr,true,&glass);
        auto id=ImGui::GetItemID();
        Result r{clicked,ImGui::IsItemActive(),ImGui::IsItemHovered(),ImGui::IsItemFocused(),firstMotion->Snapshot().press,firstMotion->Snapshot().hover,id,ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax())};
        assert(r.rect.GetWidth()==38&&r.rect.GetHeight()==38);

        const auto s1=Stack::Navigation::UpdateGlassControlState(*firstMotion,false),s2=Stack::Navigation::UpdateGlassControlState(*firstMotion,false);
        assert(s1.press==r.press&&s2.press==r.press&&s1.hover==r.amount&&s2.hover==r.amount&&s1.selection==s2.selection&&s1.focus==s2.focus);

        Stack::Navigation::IconButton("Library",Stack::Navigation::Glyph::Library,false,true,1,pal,libraryMotion,nullptr,true,&glass);
        Stack::Navigation::IconButton("Queue",Stack::Navigation::Glyph::Queue,false,true,1,pal,queueMotion,nullptr,true,&glass);
        Stack::Navigation::IconButton("Panel",Stack::Navigation::Glyph::Panel,false,true,1,pal,panelMotion,nullptr,true,&glass);
        ImGui::End();
        ImGui::PopStyleVar();
        if(showSlider){ImGui::SetNextWindowPos({115,65});
            ImGui::SetNextWindowSize({300,190});
            ImGui::Begin("Actual slider demonstration",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoResize);
            ImGui::SetCursorScreenPos({140,100});
            ImGui::SetNextItemWidth(240);
            ImGui::BeginDisabled(!sliderEnabled);
            if(sliderFocus)ImGui::SetKeyboardFocusHere();
            sliderChanged=ImGui::SliderFloat("##NativeSlider",&sliderValue,0.f,1.f,"%.3f",ImGuiSliderFlags_NoInput);
            const auto nativeId=ImGui::GetItemID();
            const ImRect nativeRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
            const float authoritative=sliderValue;
            sliderActive=ImGui::IsItemActive();
            sliderFocused=ImGui::IsItemFocused();
            const auto input=Stack::UiMotion::LastItemInput();
            const auto ss=sliderMotion.Update(input,sliderValue,0,1);
            const auto again=sliderMotion.Update(input,sliderValue,0,1);
            assert(ss.position==sliderValue&&ss.validRange&&again.control.press==ss.control.press);
            assert(sliderValue==authoritative&&ImGui::GetItemID()==nativeId&&ImGui::GetItemRectMin().x==nativeRect.Min.x&&ImGui::GetItemRectMax().x==nativeRect.Max.x);
            ImGui::EndDisabled();
            ImGui::Text("Native value %.3f | position %.3f",sliderValue,ss.position);
            ImGui::Text("Presentation pressure %.3f",ss.control.press);
            auto* sd=ImGui::GetWindowDrawList();
            sd->AddLine({140,210},{380,210},IM_COL32(48,134,246,255),6);
            sliderGlass.AddBackdrop(sd,{120,180},{410,240},pal.caption);
            const float thumbX=160+200*ss.position;
            float thumbWidth=40.f, thumbElevation=ss.control.Elevation();
            if (velocityDemo) {
                if (input.held) {
                    // Direct manipulation replaces any previous visual spring.
                    visualWidth.Reset(52.f);
                    visualElevation.Reset(.2f);
                } else {
                    visualWidth.Advance(input.deltaSeconds,input.reducedMotion);
                    visualElevation.Advance(input.deltaSeconds,input.reducedMotion);
                }
                assert(std::isfinite(visualWidth.Value())&&std::isfinite(visualElevation.Value()));
                thumbWidth=std::clamp(visualWidth.Value(),24.f,64.f);
                thumbElevation=std::clamp(visualElevation.Value(),0.f,1.f);
                assert(sliderValue==authoritative&&ss.position==sliderValue);
            }
            const ImVec2 thumbLo(thumbX-thumbWidth*.5f,198),thumbHi(thumbX+thumbWidth*.5f,222);
            Stack::Navigation::GlassObjectBacking(sd,thumbLo,thumbHi,12,pal);
            assert(sliderGlass.AddObject(sd,thumbLo,thumbHi,pal.caption,12,thumbElevation));
            ImGui::End();
        }
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glViewport(0,0,640,360);
        glClearColor(0,0,0,1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        assert(glass.Error().empty()&&sliderGlass.Error().empty());
        glReadPixels(0,0,640,360,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        assert(glGetError()==GL_NO_ERROR);
        for(size_t i=3;i<pixels.size();i+=4)assert(pixels[i]==255);
        return r;
    };

    auto save=[&](std::string name){std::ofstream out(folder+"/"+name+".ppm",std::ios::binary);
        assert(out);
        out<<"P6\n640 360\n255\n";
        for(int y=359;y>=0;--y)for(int x=0;x<640;++x)out.write(reinterpret_cast<const char*>(pixels.data()+(y*640+x)*4),3);
        out.close();
        assert(out.good());
    };

    frame(500,300,false,true);
    auto idle=frame(500,300,false,true);
    save("idle");
    assert(!idle.click&&idle.press==0);

    frame(26,78,false);
    auto pressed=frame(26,78,true);
    assert(pressed.press==1&&!pressed.click);
    save("pressed");
    auto held=frame(26,78,true);
    assert(held.press==1);
    auto release=frame(26,78,false);
    assert(release.click&&release.press<1&&release.press>0);
    save("released");
    for(int k=0;k<24;++k){frame(500,300,false);
        save("motion-"+std::to_string(k));
    }save("rest");

    frame(26,78,false,true);
    frame(26,78,true);
    auto leave=frame(500,300,true);
    assert(leave.active&&leave.press<1&&!leave.click);
    auto enter=frame(26,78,true);
    assert(enter.active&&enter.press==1);
    auto outside=frame(500,300,false);
    assert(!outside.click);
    std::cout<<"PASS leave/reentry and outside release\n";

    for(int k=0;k<2;++k){frame(26,78,false,true);
        auto p=frame(26,78,true);
        auto r=frame(26,78,false);
        assert(!p.click&&r.click&&p.press==1);
    }std::cout<<"PASS repeated/interrupted click and once-per-frame state\n";

    frame(26,78,true,true);
    auto reducedRelease=frame(26,78,false,true);
    assert(reducedRelease.press==0);
    std::cout<<"PASS reduced-motion release snaps\n";

    frame(26,78,false,true,false,false);
    auto disabled=frame(26,78,true,true,false,false);
    assert(!disabled.click&&!disabled.active&&disabled.press==0);
    frame(500,300,false,true);
    std::cout<<"PASS disabled control\n";

    io.AddKeyEvent(ImGuiKey_Tab,true);
    frame(500,300,false,true);
    io.AddKeyEvent(ImGuiKey_Tab,false);
    frame(500,300,false,true);

    frame(500,300,false,true,true);
    auto focused=frame(500,300,false,true);
    assert(focused.focus);
    io.AddKeyEvent(ImGuiKey_Space,true);
    auto keyDown=frame(500,300,false,true);
    std::cout<<"keyboard down click="<<keyDown.click<<" active="<<keyDown.active<<" press="<<keyDown.press<<"\n";
    assert(keyDown.click&&keyDown.active&&keyDown.press==1);
    io.AddKeyEvent(ImGuiKey_Escape,true);
    auto escape=frame(500,300,false,true);
    std::cout<<"Escape while Space held active="<<escape.active<<" press="<<escape.press<<" click="<<escape.click<<"\n";
    assert(!escape.active&&escape.press==0&&!escape.click);
    io.AddKeyEvent(ImGuiKey_Escape,false);
    io.AddKeyEvent(ImGuiKey_Space,false);
    frame(500,300,false,true);

    frame(26,78,false,true);
    frame(26,78,true,true);
    io.AddKeyEvent(ImGuiKey_Escape,true);
    auto mouseEscape=frame(26,78,true,true);
    std::cout<<"Escape while mouse held active="<<mouseEscape.active<<" press="<<mouseEscape.press<<"\n";
    assert(!mouseEscape.active&&mouseEscape.press==0);
    io.AddKeyEvent(ImGuiKey_Escape,false);
    auto finalRelease=frame(26,78,false,true);
    assert(!finalRelease.click);
    std::cout<<"PASS native keyboard activation and held-input Escape cancellation\n";


    frame(26,78,false,true);
    frame(26,78,true,true);
    assert(firstMotion->Snapshot().press==1);
    firstMotion.reset();
    firstMotion=std::make_unique<Stack::Animation::ControlMotion>();
    assert(firstMotion->Snapshot().press==0);
    auto recreated=frame(500,300,true);
    assert(recreated.press==0&&!recreated.click);
    auto recreatedRelease=frame(500,300,false);
    assert(!recreatedRelease.click);
    std::cout<<"PASS destroyed/recreated motion owner does not inherit pressure; ImGui action ownership unchanged\n";

    frame(500,300,false,true);
    auto on=pixels;
    material=false;
    frame(500,300,false,true);
    for(int y=0;y<360;++y)for(int x=0;x<640;++x)if(x>=52||y<52||y>=245){const size_t i=((359-y)*640+x)*4;
        for(int c=0;c<4;++c)assert(on[i+c]==pixels[i+c]);
    }std::cout<<"PASS fixed38px hitboxes, alpha255, exterior isolation, GL and disable cleanup\n";

    showSlider=true;
    frame(500,300,false,true);
    save("slider-optical-idle");
    frame(145,110,false,true);
    frame(145,110,true,true);
    assert(sliderActive&&sliderValue==0&&sliderMotion.Snapshot().position==0);
    frame(375,110,true,true);
    assert(sliderValue==1&&sliderMotion.Snapshot().position==1);
    save("slider-optical-held");
    frame(500,110,false,true);
    assert(!sliderActive);
    save("slider-pointer-endpoint");

    sliderEnabled=false;
    const float disabledValue=sliderValue;
    frame(180,110,true,true);
    assert(!sliderActive&&!sliderChanged&&sliderValue==disabledValue&&sliderMotion.Snapshot().control.press==0);
    frame(500,300,false,true);
    sliderEnabled=true;

    io.AddKeyEvent(ImGuiKey_Tab,true);
    frame(500,300,false,true);
    io.AddKeyEvent(ImGuiKey_Tab,false);
    sliderFocus=true;
    frame(500,300,false,true);
    sliderFocus=false;
    frame(500,300,false,true);
    assert(sliderFocused);
    io.AddKeyEvent(ImGuiKey_Space,true);
    frame(500,300,false,true);
    io.AddKeyEvent(ImGuiKey_Space,false);
    frame(500,300,false,true);
    assert(sliderActive&&sliderMotion.Snapshot().control.press==1);
    io.AddKeyEvent(ImGuiMod_Shift,true);
    for(int k=0;k<11;++k){io.AddKeyEvent(ImGuiKey_LeftArrow,true);
        frame(500,300,false,true);
        io.AddKeyEvent(ImGuiKey_LeftArrow,false);
        frame(500,300,false,true);
    }assert(sliderValue==0);
    for(int k=0;k<11;++k){io.AddKeyEvent(ImGuiKey_RightArrow,true);
        frame(500,300,false,true);
        io.AddKeyEvent(ImGuiKey_RightArrow,false);
        frame(500,300,false,true);
    }assert(sliderValue==1);
    io.AddKeyEvent(ImGuiMod_Shift,false);
    io.AddKeyEvent(ImGuiKey_Escape,true);
    frame(500,300,false,true);
    assert(!sliderActive&&sliderMotion.Snapshot().control.press==0);
    io.AddKeyEvent(ImGuiKey_Escape,false);
    frame(500,300,false,true);
    save("slider-keyboard-endpoint");
    std::cout<<"PASS actual native slider pointer/keyboard endpoints, disabled, held Escape, reduced motion, unchanged value/ID/rect and same-frame snapshot\n";

    // Explicit velocity handoff affects presentation geometry only.
    // Width velocity is px/s; elevation velocity is normalized units/s.
    velocityDemo=true;
    auto releaseVisual=[&] {
        assert(visualWidth.SpringTo(40.f,-80.f,Stack::Animation::MotionSpec::Spring(4.f,.55f,.01f)));
        assert(visualElevation.SpringTo(1.f,1.5f,Stack::Animation::MotionSpec::Spring(4.f,.75f,.001f)));
    };
    frame(375,110,false,true);
    frame(375,110,true,false);
    assert(sliderValue==1&&visualWidth.Value()==52&&visualElevation.Value()==.2f);
    save("velocity-held");
    releaseVisual();
    for(int k=0;k<48;++k) {
        frame(375,110,false,false);
        assert(sliderValue==1&&sliderMotion.Snapshot().position==1&&!sliderChanged);
        save("velocity-release-"+std::to_string(k));
    }
    assert(!visualWidth.IsActive()&&!visualElevation.IsActive());
    assert(visualWidth.Value()==40&&visualElevation.Value()==1);
    frame(375,110,true,false);
    releaseVisual();
    frame(375,110,false,false);
    frame(375,110,true,false); // Regrab overrides the incomplete visual release.
    assert(!visualWidth.IsActive()&&!visualElevation.IsActive());
    assert(visualWidth.Value()==52&&visualElevation.Value()==.2f&&sliderValue==1);
    save("velocity-regrab");
    releaseVisual();
    frame(375,110,false,false);
    visualWidth.Cancel(); visualElevation.Cancel();
    const float cancelledWidth=visualWidth.Value(), cancelledElevation=visualElevation.Value();
    frame(375,110,false,false);
    assert(visualWidth.Value()==cancelledWidth&&visualElevation.Value()==cancelledElevation);
    assert(sliderValue==1&&!sliderChanged);
    save("velocity-cancelled");
    releaseVisual();
    frame(375,110,false,true);
    assert(!visualWidth.IsActive()&&!visualElevation.IsActive());
    assert(visualWidth.Value()==40&&visualElevation.Value()==1&&sliderValue==1);
    save("velocity-reduced-motion");
    std::cout<<"PASS opt-in glass width/elevation velocity handoff, regrab/cancel, reduced motion; native slider value/position/hitbox unchanged\n";

    CheckSharedBackdropCapture(fbo);

    sliderGlass.Shutdown();
    glass.Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    glDeleteFramebuffers(1,&fbo);
    glDeleteTextures(1,&tex);
    assert(glGetError()==GL_NO_ERROR);
    eglMakeCurrent(d,nullptr,nullptr,nullptr);
    eglDestroyContext(d,ctx);
    eglTerminate(d);
}
