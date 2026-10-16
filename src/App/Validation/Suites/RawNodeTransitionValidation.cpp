#include "Editor/EditorModule.h"
#include "Renderer/GLLoader.h"
#include <imgui_internal.h>
#include <array>
#include <cmath>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace Stack::Validation {
void ValidateRawNodeTransitions(EditorModule& editor,
    const std::function<void(const std::function<void()>&)>& frame) {
    const auto require = [](bool value, const char* error) {
        if (!value) throw std::runtime_error(error);
    };
    {
        Stack::Editor::NodeOpenTransition motion;
        Stack::Editor::NodeOpenTransition::Appearance origin, destination;
        origin.minimum={40,90}; origin.maximum={150,220};
        destination.minimum={410,220}; destination.maximum={550,370};
        motion.Begin(origin,"geometry",0);
        ImVec2 low{600,100},high{900,650};
        motion.Present(low,high,0);
        low={600,100}; high={900,650}; motion.Present(low,high,.23);
        const auto beforeLow=low,beforeHigh=high;
        require(motion.Close(destination,"geometry",.23),"Interrupted return did not start");
        motion.Present(low,high,.23);
        require(std::abs(low.x-beforeLow.x)<.01f && std::abs(low.y-beforeLow.y)<.01f &&
            std::abs(high.x-beforeHigh.x)<.01f && std::abs(high.y-beforeHigh.y)<.01f,
            "Retargeting an interrupted opening jumped");
        motion.SetReturnOffset({17,8}); motion.Present(low,high,.46);
        require(std::abs(low.x-destination.minimum.x-17)<.01f && std::abs(low.y-destination.minimum.y-8)<.01f &&
            std::abs(high.x-destination.maximum.x-17)<.01f && std::abs(high.y-destination.maximum.y-8)<.01f,
            "Return missed the destination node after chrome moved");
        std::cout<<"PASS interrupted return continuity and exact destination bounds\n";
        motion.Reset();
        low={600,100}; high={900,650}; motion.Present(low,high,1);
        require(motion.Close(destination,"gallery",1,.4f),"Gallery return required a previous node opening");
        motion.Present(low,high,1);
        require(std::abs(low.x-600)<.01f && std::abs(high.y-650)<.01f &&
            std::abs(motion.BlurOpacity(1)-.4f)<.001f,"Gallery return did not start from its visible photo/surround");
        motion.Present(low,high,1.76);
        require(std::abs(low.x-destination.minimum.x)<.01f && std::abs(high.y-destination.maximum.y)<.01f,
            "Gallery return missed its node");
        std::cout<<"PASS Gallery return without a previous opening\n";
    }
    const auto step = [&] { frame({}); };
    const auto settle = [&] {
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        do { step(); } while (std::chrono::steady_clock::now()<until);
        std::cout<<editor.GetActiveRawWorkspacePresentationDiagnosticForValidation()<<'\n'<<std::flush;
    };
    editor.SetWorkspaceMode(Stack::Project::WorkspaceMode::View);
    settle();
    frame([&] {
        editor.ReturnToGraphFromNode();
        require(editor.IsReturningToGraph(),"Gallery Escape skipped the return animation");
    });
    settle();
    require(!editor.IsViewMode(),"Gallery Escape did not finish in Graph");
    const auto samplePhoto = [&] {
        unsigned texture=0; int width=0,height=0;
        require(editor.TryGetActiveRawWorkspacePresentationTexture(texture,width,height),"Missing photo texture after node opening");
        require(std::max(width,height)>=256,"Node opening published a placeholder-sized photo");
        std::vector<float> pixels(std::size_t(width)*height*4);
        glBindTexture(GL_TEXTURE_2D,texture);
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());
        glBindTexture(GL_TEXTURE_2D,0);
        double horizontalDetail=0;
        for(int y=height/5;y<height*4/5;++y) for(int x=width/5;x<width*4/5-1;++x)
            for(int c=0;c<3;++c) {
                const auto index=(std::size_t(y)*width+x)*4+c;
                require(std::isfinite(pixels[index]),"Nonfinite photo after node opening");
                horizontalDetail+=std::abs(pixels[index]-pixels[index+4]);
            }
        require(horizontalDetail/(width*height)>.0001,"Photo became a flat vertical gradient");
        const auto* photo=editor.GetRawImageBackdrop();
        require(photo,"Missing displayed photo bounds");
        std::array<float,8*8*3> samples{};
        for(int y=0;y<8;++y) for(int x=0;x<8;++x) {
            const int sx=int(photo->imageMinimum.x+(photo->imageMaximum.x-photo->imageMinimum.x)*(.2f+.6f*(x+.5f)/8));
            const int sy=int(700-photo->imageMinimum.y-(photo->imageMaximum.y-photo->imageMinimum.y)*(.2f+.6f*(y+.5f)/8));
            require(sx>=0 && sx<1000 && sy>=0 && sy<700,"Photo sample lies outside the framebuffer");
            float pixel[4]{}; glReadPixels(sx,sy,1,1,GL_RGBA,GL_FLOAT,pixel);
            for(int c=0;c<3;++c) samples[(y*8+x)*3+c]=pixel[c];
        }
        double displayedDetail=0;
        for(int y=0;y<8;++y) for(int x=0;x<7;++x) for(int c=0;c<3;++c)
            displayedDetail+=std::abs(samples[(y*8+x)*3+c]-samples[(y*8+x+1)*3+c]);
        require(displayedDetail/(8*7*3)>.004,"Displayed photo became a blank/gradient despite having an image texture");
        return samples;
    };
    EditorNodeGraphUI capture;
    capture.Initialize();
    std::vector<std::string> nodes;
    editor.OpenRawLayerMaskGraph({});
    frame([&] {
        for(const auto& node:editor.GetNodeGraph().GetNodes()) {
            if(node.kind!=EditorNodeGraph::NodeKind::Output) continue;
            const auto origin=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();
            const auto appearance=capture.CaptureNodeAppearance(&editor,node.id,origin,{origin.x+size.x,origin.y+size.y});
            require(std::any_of(appearance.meshes.begin(),appearance.meshes.end(),[](const auto& mesh) {
                return mesh.texture && mesh.texture->name;
            }),"Loaded image node lost its textured appearance during capture");
            std::cout<<"PASS loaded node texture retained in morph\n";
            break;
        }
    });
    for(const auto& node:editor.GetNodeGraph().GetNodes())
        if(node.kind==EditorNodeGraph::NodeKind::RawOperation &&
            node.role==Stack::GraphModel::NodeRole::Ordinary) nodes.push_back(node.instanceUuid);
    require(nodes.size()>=4,"RAW transition fixture is missing its default tools");
    nodes.resize(4);
    for(int cycle=0;cycle<2;++cycle) for(const auto& uuid:nodes) {
        frame([&] { editor.SetWorkspaceMode(Stack::Project::WorkspaceMode::Graph); });
        step();
        require(!editor.WorkspacePanelOpen(),"Entering Graph restored its node library");
        frame([&] {
            int id=-1;
            for(const auto& node:editor.GetNodeGraph().GetNodes()) if(node.instanceUuid==uuid) id=node.id;
            const auto origin=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();
            auto appearance=capture.CaptureNodeAppearance(&editor,id,origin,{origin.x+size.x,origin.y+size.y});
            require(appearance.Valid() && !appearance.meshes.empty(),"Node appearance capture is empty");
            // Exercise the actual captured drawing on every opening, including
            // repeated visits whose previews have already loaded.
            Stack::Editor::NodeOpenTransition motion;
            motion.Begin(appearance,"repeat",0);
            ImVec2 low{600,100},high{900,650}; motion.Present(low,high,0);
            low={600,100}; high={900,650}; motion.Present(low,high,.09);
            require(high.x-low.x>appearance.maximum.x-appearance.minimum.x,
                "Node did not expand during its shape phase");
            ImDrawList drawing(ImGui::GetDrawListSharedData()); drawing._ResetForNewFrame();
            drawing.PushClipRectFullScreen(); motion.DrawNode(drawing,.09);
            require(!drawing.VtxBuffer.empty(),"Repeated opening omitted its node drawing");
            low={600,100}; high={900,650}; motion.Present(low,high,.76); motion.Tick("repeat",.76,false);
            require(motion.Close(appearance,"repeat",1),"Repeated return did not start");
            motion.Present(low,high,1.67);
            drawing._ResetForNewFrame(); drawing.PushClipRectFullScreen(); motion.DrawNode(drawing,1.67);
            require(!drawing.VtxBuffer.empty(),"Repeated return omitted its node drawing");
            require(editor.OpenRawProjectNodeEditor(id),"Could not open RAW node");
            editor.ConsumeOpenRawLabTabRequest();
            editor.SetWorkspaceMode(Stack::Project::WorkspaceMode::View);
            editor.BeginNodeOpenTransition(std::move(appearance));
        });
        settle();
        samplePhoto();
        // Switching tools in View must change the reverse destination too.
        const int next=(editor.GetRawLayerToolIndex()+1)%4;
        frame([&] { editor.RequestRawLayerToolIndex(next); });
        settle();
        samplePhoto();
        std::string expected;
        const auto& session=editor.GetProjectSession();
        if(session.nodeEditor) expected=session.nodeEditor->nodeUuid;
        frame([&] { editor.ReturnToGraphFromNode(); });
        settle();
        require(!editor.IsViewMode() && !editor.WorkspacePanelOpen(),"Escape did not return to Graph with its library closed");
        unsigned returnedTexture=0; int returnedWidth=0,returnedHeight=0;
        require(editor.TryGetActiveRawWorkspacePresentationTexture(returnedTexture,returnedWidth,returnedHeight) &&
            std::max(returnedWidth,returnedHeight)>=256,"Returning to Graph reduced the photo to placeholder dimensions");
        const auto* selected=editor.GetNodeGraph().FindNode(editor.GetNodeGraph().GetSelectedNodeId());
        require(selected && selected->instanceUuid==expected,"Escape returned to a different tool node");
        std::cout<<"PASS RAW node open / switch tool / Escape: "<<uuid<<" cycle "<<cycle+1<<'\n'<<std::flush;
    }
    capture.Shutdown();
}
}
