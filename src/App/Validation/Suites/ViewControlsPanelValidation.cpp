#include "Editor/EditorModule.h"
#include "Editor/NodeEditors/NodeEditorPanelState.h"
#include "Project/RawLayerStack.h"
#include "Editor/Internal/RawLab/CurveCanvas.h"
#include "Editor/GraphUI/GraphPlotStyle.h"
#include "App/settings/AppearanceTheme.h"
#include "Renderer/GLLoader.h"
#include "Async/TaskSystem.h"
#include "ThirdParty/stb_image_write.h"
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace Stack::Validation {
bool ValidateViewControlsPanel(const std::filesystem::path& output) {
    if (!glfwInit()) return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(1000,760,"View Controls panel validation",nullptr,nullptr);
    if (!window) {glfwTerminate();return false;}
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {glfwDestroyWindow(window);glfwTerminate();return false;}
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
    io.DisplaySize={1000,760};io.DeltaTime=1.f/60.f;
    StackAppearance::AppearanceManager appearance;
    appearance.SetupFonts(io);appearance.ApplyCurrentTheme(io,ImGui::GetStyle());
    ImGui_ImplOpenGL3_Init("#version 430");
    ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();ImGui::EndFrame();
    Async::TaskSystem::Get().Initialize();
    auto editor=std::make_unique<EditorModule>();
    editor->SetDocumentPersistenceEnabled(false);
    editor->SetWorkspaceAppStatePersistenceEnabled(false);
    editor->Initialize(window,&appearance,false);
    bool success=false;
    const auto require=[](bool value,const char* message){if (!value) throw std::runtime_error(message);};
    try {
        auto& graph=editor->GetNodeGraph();graph.Clear();
        EditorNodeGraph::RawDevelopmentPayload source;source.recipe=RawRecipe::MakeDefaultRecipe({});
        const int start=graph.AddRawDevelopmentNode(std::move(source),{})->id;
        const int end=graph.AddOutputNode({400,0},true)->id;
        require(graph.TryConnect(start,end),"Could not prepare the ordinary RAW document.");
        editor->DeserializePipeline(editor->SerializePipeline());
        auto payload=editor->SerializePipeline();
        Project::RawLayerStackDocument document;std::string error;
        require(document.Load(payload.at("rawLayerStack"),error),error.c_str());
        auto layers=document.State();
        auto* firstExposure=layers.background.graph.AddRawOperationNode(RawRecipe::GraphOperationKind::Exposure,{900,0});
        const auto firstUuid=firstExposure->instanceUuid;
        const auto second=layers.background.graph.AddRawOperationNode(RawRecipe::GraphOperationKind::Exposure,{1000,0})->instanceUuid;
        auto& ordered=layers.background.graph.GetNodes();
        std::stable_partition(ordered.begin(),ordered.end(),[&](const auto& node) {
            return node.instanceUuid==firstUuid || node.instanceUuid==second;
        });
        const auto secondLayer=Project::AddRawAdjustmentLayer(layers,"Second layer");
        Project::FindRawAdjustmentLayer(layers,secondLayer)->graph.AddRawOperationNode(RawRecipe::GraphOperationKind::Exposure,{100,0});
        layers.background.graph.AddConstantChannelNode({})->constantChannelSettings.value=.5f;
        payload["rawLayerStack"]=Project::SerializeRawLayerStack(layers);
        editor->DeserializePipeline(payload);
        require(editor->SetWorkspaceMode(Project::WorkspaceMode::View),"Could not enter View.");
        auto prefs=editor->GetRawWorkspacePresentationPreferences();prefs.viewSectionTab=1;prefs.viewSectionOpen=true;
        editor->ApplyRawWorkspacePresentationPreferences(prefs);
        const auto authored=editor->SerializePipeline().at("rawLayerStack");
        const int selectedBefore=editor->GetRawLayerToolIndex();
        float width=320;
        const auto frame=[&] {
            ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
            editor->RenderRawWorkspaceSectionPanel({20,20},{width,720},width);
            ImGui::Render();glViewport(0,0,1000,760);glClearColor(.08f,.08f,.08f,1);glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        };
        const auto primaries=[&] {
            std::vector<ImGuiWindow*> windows;
            for (auto* item:ImGui::GetCurrentContext()->Windows)
                if (item->Active && std::string(item->Name).find("/Primary_")!=std::string::npos)
                    windows.push_back(item);
            std::sort(windows.begin(),windows.end(),[](auto* a,auto* b){return a->Pos.y<b->Pos.y;});
            return windows;
        };
        io.AddMousePosEvent(-100,-100);
        for (int i=0;i<8;++i) frame();
        require(editor->SerializePipeline().at("rawLayerStack")==authored,"Drawing the panel changed authored values.");
        require(editor->GetRawLayerToolIndex()==selectedBefore,"Drawing multiple tools changed selection.");
        auto controls=primaries();
        require(!controls.empty(),"The Controls tab did not draw registered primary tools.");
        for (auto* item:controls) {
            require(item->Size.x<=width && item->Pos.x>=20,"A primary surface overflowed the panel.");
            require(!item->ScrollbarY || item->ScrollbarSizes.x==0,"A tool has an unexpected internal scrollbar.");
        }
        const auto capture=[&](const char* name) {
            std::filesystem::create_directories(output);
            const int captureWidth=static_cast<int>(width),captureHeight=720;
            const int stride=captureWidth*4;
            std::vector<unsigned char> pixels(std::size_t(stride)*captureHeight);
            glReadPixels(20,20,captureWidth,captureHeight,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            for (int y=0;y<captureHeight/2;++y) for(int x=0;x<stride;++x)
                std::swap(pixels[std::size_t(y)*stride+x],pixels[std::size_t(captureHeight-1-y)*stride+x]);
            require(stbi_write_png((output/name).string().c_str(),captureWidth,captureHeight,4,pixels.data(),stride)!=0,"Could not save the native panel preview.");
        };
        // The section geometry must settle; repeated available-height feedback
        // used to make lower sections move even without a graph edit.
        const auto settled=primaries();
        std::vector<std::pair<ImGuiWindow*,ImVec2>> positions;
        for (auto* item:settled) positions.emplace_back(item,item->Pos);
        const auto stablePositions=[&] {
            for (const auto& [item,position]:positions)
                require(item->Pos.x==position.x && item->Pos.y==position.y,
                    "A settled Controls section moved during drawing or editing.");
        };
        for (int i=0;i<12;++i) {frame();stablePositions();}
        capture("controls-320.png");
        io.AddMousePosEvent(40,controls.front()->Pos.y-(ImGui::GetFontSize()+10.f)*.5f-ImGui::GetStyle().ItemSpacing.y);frame();
        io.AddMouseWheelEvent(0,-12);for (int i=0;i<5;++i) frame();
        bool scrolled=false;
        for (auto* item:ImGui::GetCurrentContext()->Windows) {
            const std::string name=item->Name;
            const auto body=name.rfind("/PanelBody_");
            if (item->Active && body!=std::string::npos && name.find('/',body+1)==std::string::npos) {
                scrolled|=item->Scroll.y>0;
                require(!item->ScrollbarY,"The Controls body shows a scrollbar.");
            }
        }
        require(scrolled,"The Controls body did not scroll with the wheel.");
        capture("controls-bottom-320.png");
        io.AddMouseWheelEvent(0,30);for (int i=0;i<5;++i) frame();
        controls=primaries();
        // Use the rendered first section's geometry to click its second node.
        const float buttonHeight=ImGui::GetFontSize()+10.f;
        const float buttonWidth=std::max(buttonHeight,ImGui::CalcTextSize("3").x+14.f);
        const float versionsStart=controls.front()->Pos.x+ImGui::CalcTextSize("Exposure").x+8.f;
        const ImVec2 secondButton(versionsStart+buttonWidth*1.5f,
            controls.front()->Pos.y-ImGui::GetStyle().ItemSpacing.y-buttonHeight*.5f);
        io.AddMousePosEvent(secondButton.x,secondButton.y);frame();
        io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
        require(editor->GetRawLayerToolIndex()!=selectedBefore,"The numbered selector did not change the active instance.");
        require(editor->SerializePipeline().at("rawLayerStack")==authored,"Selecting a numbered instance changed its parameters.");
        controls=primaries();
        const int numberedTarget=editor->GetRawLayerToolIndex();
        require(controls.size()>1,"The heading selection check needs a second visible section.");
        io.AddMousePosEvent(controls[1]->Pos.x+5.f,controls[1]->Pos.y+5.f);frame();
        io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
        require(editor->GetRawLayerToolIndex()!=numberedTarget,"The other section did not activate its tool.");
        controls=primaries();
        io.AddMousePosEvent(controls.front()->Pos.x+10.f,
            controls.front()->Pos.y-ImGui::GetStyle().ItemSpacing.y-buttonHeight*.5f);frame();
        io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
        require(editor->GetRawLayerToolIndex()==numberedTarget,"Clicking the name did not restore its chosen instance.");
        editor->SetViewSectionTab(2);frame();
        require(editor->GetRawLayerToolIndex()==numberedTarget,"Settings lost the name-selected tool.");
        editor->SetViewSectionTab(1);frame();
        require(editor->SerializePipeline().at("rawLayerStack")==authored,"Selecting a heading changed authored values.");
        controls=primaries();
        const ImVec2 slider(controls.front()->Pos.x+controls.front()->Size.x*.75f,
            controls.front()->Pos.y+50.f*ImGui::GetFontSize()/16.f);
        io.AddMousePosEvent(slider.x,slider.y);frame();
        const auto* tooltip=ImGui::GetCurrentContext()->TooltipPreviousWindow;
        require(tooltip && tooltip->Hidden,"Controls hover text was not suppressed.");
        positions.clear();
        for (auto* item:primaries()) positions.emplace_back(item,item->Pos);
        io.AddMouseButtonEvent(0,true);frame();stablePositions();
        const auto readExposure=[&] {
            require(document.Load(editor->SerializePipeline().at("rawLayerStack"),error),error.c_str());
            return Project::ReadRawLayerOperation(document.State().background,second).preToneExposureEv;
        };
        // Stay captured beyond the horizontal and vertical bounds, then move
        // back in. Every intermediate frame commits through the real history.
        io.AddMousePosEvent(controls.front()->Pos.x-100,slider.y-150);frame();stablePositions();
        require(readExposure()==-10.f,"Dragging outside the left edge stopped updating.");
        io.AddMousePosEvent(controls.front()->Pos.x+width+100,slider.y+150);frame();stablePositions();
        require(readExposure()==10.f,"Dragging outside the right edge stopped updating.");
        io.AddMousePosEvent(slider.x+12,slider.y);frame();stablePositions();
        require(readExposure()>0.f && readExposure()<10.f,"Returning from an edge lost the captured slider.");
        for (int i=0;i<8;++i) {frame();stablePositions();}
        io.AddMouseButtonEvent(0,false);frame();
        for (int i=0;i<8;++i) {frame();stablePositions();}
        require(ImGui::GetActiveID()==0,"Releasing an edge drag left a control active.");
        for (auto* item:primaries())
            require(item->Scroll.x==0 && item->Scroll.y==0,"A primary surface scrolled inside the Controls body.");
        const auto edited=editor->SerializePipeline().at("rawLayerStack");
        require(edited!=authored,"A panel slider did not commit its node draft.");
        require(document.Load(edited,error),error.c_str());
        require(Project::ReadRawLayerOperation(document.State().background,firstUuid).preToneExposureEv==0 &&
            Project::ReadRawLayerOperation(document.State().background,second).preToneExposureEv!=0,
            "The panel edited the wrong numbered node.");
        require(editor->UndoRawLayerEdit(),"Panel editing could not be undone.");
        require(editor->SerializePipeline().at("rawLayerStack")==authored,"Undo did not restore the panel's instance edit.");
        require(editor->RedoRawLayerEdit(),"Panel editing could not be redone.");
        require(editor->SerializePipeline().at("rawLayerStack")==edited,"Redo did not restore the panel's instance edit.");
        controls=primaries();
        const ImVec2 thirdButton(versionsStart+buttonWidth*2.5f,
            controls.front()->Pos.y-ImGui::GetStyle().ItemSpacing.y-buttonHeight*.5f);
        io.AddMousePosEvent(thirdButton.x,thirdButton.y);frame();
        io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
        require(editor->GetRawLayerToolLabels().size()==1 && editor->GetRawLayerToolIndex()==0,
            "The numbered selector did not select the instance in the other layer.");
        controls=primaries();
        const ImVec2 otherSlider(controls.front()->Pos.x+controls.front()->Size.x*.65f,
            controls.front()->Pos.y+50.f*ImGui::GetFontSize()/16.f);
        io.AddMousePosEvent(otherSlider.x,otherSlider.y);frame();
        io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
        require(document.Load(editor->SerializePipeline().at("rawLayerStack"),error),error.c_str());
        const auto* otherLayer=Project::FindRawAdjustmentLayer(document.State(),secondLayer);
        require(otherLayer && Project::ReadRawLayerOperation(*otherLayer,
            Project::FindRawOperation(*otherLayer,RawRecipe::GraphOperationKind::Exposure)->instanceUuid).preToneExposureEv!=0,
            "The other layer's selected tool did not receive its edit.");
        require(Project::ReadRawLayerOperation(document.State().background,firstUuid).preToneExposureEv==0,
            "An edit in the other layer changed the first instance.");
        editor->SetViewSectionTab(2);
        require(editor->GetRawWorkspacePresentationPreferences().viewSectionTab==2,"Settings did not replace Controls.");
        editor->SetViewSectionTab(1);editor->SetViewSectionPanelOpen(false);
        require(!editor->ViewSectionPanelOpen(),"Closing the View panel did not take effect.");
        editor->SetViewSectionPanelOpen(true);
        require(editor->GetRawWorkspacePresentationPreferences().viewSectionTab==1,"Reopening the panel lost Controls.");
        width=240;for (int i=0;i<8;++i) frame();capture("controls-240.png");
        Editor::NodeEditorPanelState reveal;reveal.visible=false;reveal.floatingRevealStarted=10;
        require(reveal.Reveal(10,false)==0 && reveal.Reveal(10.09,false)>.49f &&
            reveal.Reveal(10.18,false)==1 && reveal.Reveal(10,true)==1,"Floating reveal timing or reduced motion failed.");
        // Exercise the shared curve widget's normalized coordinates and native
        // capture independently of the slider check, without editor state hooks.
        Editor::RawLabInternal::CurveCanvasState curveState;
        RawRecipe::CurveAmountState amount;
        std::vector<RawRecipe::RawBezierCurvePoint> points{{0,0,{1},{1}},{.5f,.5f,{1},{1}},{1,1,{1},{1}}};
        ImVec2 curveCenter;
        const auto curveFrame=[&] {
            ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();
            ImGui::SetNextWindowPos({400,80});ImGui::SetNextWindowSize({320,340});
            ImGui::Begin("Curve coordinate check",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoDecoration);
            const auto origin=ImGui::GetCursorScreenPos();
            const auto style=Editor::GraphUI::ResolveGraphPlotStyle();
            curveCenter={origin.x+140.f,origin.y+(250.f-style.controlHeight-ImGui::GetStyle().ItemSpacing.y)*.5f};
            Editor::RawLabInternal::DrawCurveCanvas("Curve",points,amount,curveState,true,{},ImVec2(280,250));
            ImGui::End();ImGui::Render();
        };
        io.AddMousePosEvent(-100,-100);curveFrame();curveFrame();
        io.AddMousePosEvent(curveCenter.x,curveCenter.y);curveFrame();
        io.AddMouseButtonEvent(0,true);curveFrame();
        require(curveState.gesture && curveState.dragged==1,"The curve did not capture its rendered middle point.");
        io.AddMousePosEvent(350,20);curveFrame();
        require(curveState.gesture && points[1].y==1.f,"Curve dragging stopped beyond the top/left edges.");
        io.AddMousePosEvent(900,600);curveFrame();
        require(curveState.gesture && points[1].y==0.f,"Curve dragging stopped beyond the bottom/right edges.");
        io.AddMousePosEvent(curveCenter.x,curveCenter.y);curveFrame();
        require(std::abs(points[1].x-.5f)<.001f && std::abs(points[1].y-.5f)<.001f,
            "The curve's return coordinates did not match its displayed center.");
        io.AddMouseButtonEvent(0,false);curveFrame();
        require(!curveState.gesture && ImGui::GetActiveID()==0,"The curve retained its gesture after release.");
        // Preferences use the production serializer, separately from image data.
        RawWorkspace::AppState settings;settings.viewSectionTab=1;settings.viewSectionOpen=false;
        const auto preferenceFile=output/"presentation.json";
        require(RawWorkspace::SaveAppState(preferenceFile,settings,&error),error.c_str());
        RawWorkspace::AppState restored;
        require(RawWorkspace::LoadAppState(preferenceFile,restored,&error),error.c_str());
        require(restored.viewSectionTab==1 && !restored.viewSectionOpen,"Controls presentation did not survive app-state reload.");
        std::cout<<"View Controls: inline version selection, name activation and Settings ownership, hidden hover text, stable section layout through edits, continuous slider and curve outside-edge dragging and release, idle drawing, numbered instances across layers, targeted slider editing, undo/redo, panel tabs/closing, scrollbar-free scrolling, narrow layout, persisted preferences and finite reveal passed.\n";
        success=true;
    } catch (const std::exception& exception) {std::cerr<<exception.what()<<'\n';}
    editor->RequestWorkerShutdownForAppClose();Async::TaskSystem::Get().Shutdown();editor.reset();
    ImGui_ImplOpenGL3_Shutdown();ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();
    return success;
}
}
