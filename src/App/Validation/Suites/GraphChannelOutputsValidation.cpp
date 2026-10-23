#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/ChannelOutputs.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "App/settings/AppearanceTheme.h"
#include "Async/TaskSystem.h"
#include "NodeMath/SourceImageSemantics.h"
#include "Renderer/GLLoader.h"
#include "Renderer/RenderPipeline.h"
#include "ThirdParty/stb_image_write.h"
#include "Utils/ImGuiExtras.h"
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_opengl3.h>
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace Stack::Validation {
namespace {
using namespace EditorNodeGraph;
using namespace Stack::NodeMath;
int checks=0;
void Check(bool pass,const std::string& message) {
    if(!pass)throw std::runtime_error(message);
    ++checks;std::cout<<"PASS "<<message<<'\n';
}
void Wire(Graph& g,int from,const std::string& output,int to,const std::string& input) {
    std::string error;if(!g.TryConnectSockets(from,output,to,input,&error))throw std::runtime_error(error);
}
std::vector<float> Render(const Graph& graph,const std::vector<unsigned char>& pixels,int channels) {
    EditorModule editor;editor.GetNodeGraph()=graph.CloneForAnalysis();
    auto snapshot=editor.BuildGraphSnapshot();snapshot.executionInspectionEnabled=true;
    RenderPipeline pipeline;pipeline.Initialize();pipeline.LoadSourceFromPixels(pixels.data(),8,4,channels);
    pipeline.ExecuteGraph(snapshot);
    const auto texture=pipeline.GetOutputTexture();
    if(!texture)return {};
    glBindTexture(GL_TEXTURE_2D,texture);
    GLint width=0,height=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&width);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&height);
    if(width!=8 || height!=4)return {};
    std::vector<float> result(std::size_t(width)*height*4);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,result.data());return result;
}
bool Same(const std::vector<float>& a,const std::vector<float>& b) {
    if(a.empty() || a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i)if(!std::isfinite(a[i]) || !std::isfinite(b[i]) || std::abs(a[i]-b[i])>1e-6f)return false;
    return true;
}
void Pixels() {
    for(const std::string component:{"r","g","b","a"}) {
        const int channels=component=="a"?3:4;
        std::vector<unsigned char> pixels(8*4*channels);
        for(std::size_t i=0;i<pixels.size();i+=channels) {
            pixels[i]=static_cast<unsigned char>(37+i%40);pixels[i+1]=118;pixels[i+2]=209;
            if(channels==4)pixels[i+3]=177;
        }
        ImagePayload image;image.width=8;image.height=4;image.channels=image.originalChannels=channels;image.pixels=pixels;
        image.sourceColorMetadata.descriptor=DescribeStoredImageValue({},channels,8,4);
        auto& description=image.sourceColorMetadata.descriptor;
        description.color=SemanticField<ColorIdentity>::Known({"srgb-d65",{},ColorRelation::Standard});
        description.transfer=SemanticField<TransferDescriptor>::Known({TransferKind::Linear,0,{}});
        description.reference=SemanticField<ReferenceState>::Known(ReferenceState::Scene);
        Graph graph;graph.Clear();
        const int source=graph.AddImageNode(std::move(image),{})->id;
        int producer=source;
        if(component=="r") {
            producer=graph.AddDataMathNode(DataMathMode::Multiply,{150,0})->id;
            graph.FindNode(producer)->dataMathSettings.constantB=1;
            Wire(graph,source,"imageOut",producer,"imageA");
        }
        const int split=graph.AddChannelSplitNode({300,0})->id;
        graph.FindNode(split)->splitMissingAlphaIsOpaque=channels==3;
        const int gain=graph.AddDataMathNode(DataMathMode::Multiply,{550,0})->id;
        graph.FindNode(gain)->dataMathSettings.domain=ArithmeticDomain::Channel;
        graph.FindNode(gain)->dataMathSettings.constantB=.7f;
        const int combine=graph.AddChannelCombineNode({850,0})->id;
        const int output=graph.AddOutputNode({1100,0},true)->id;
        Wire(graph,producer,"imageOut",split,"imageIn");Wire(graph,split,component,gain,"imageA");
        for(const char* port:{"r","g","b"})Wire(graph,gain,"imageOut",combine,port);
        Wire(graph,combine,"imageOut",output,"imageIn");
        const auto before=Render(graph,pixels,channels);
        Check(!before.empty(),"Baseline component graph renders finite-size output");
        Check(FoldChannelSplits(graph,{split}).size()==1,"GPU fixture split folds to the producer");
        Check(Same(before,Render(graph,pixels,channels)),"Folded component output matches baseline GPU pixels exactly");
        Graph reopened;DeserializeGraphPayload(SerializeGraphPayload(nlohmann::json::array(),graph),reopened,0);
        Check(Same(before,Render(reopened,pixels,channels)),"Reopened component output matches baseline GPU pixels exactly");
        if(component=="r") {
            std::string error;int packed=0;
            Check(graph.CreateCompoundFromSelection({producer},"Direct component",&packed,&error),error);
            Check(Same(before,Render(graph,pixels,channels)),"Packing a producer with component outputs preserves GPU pixels");
            std::vector<int> children;Check(graph.UnpackCompoundNode(packed,&children,&error),error);
            Check(Same(before,Render(graph,pixels,channels)),"Decompiling the component-output producer preserves GPU pixels");
            Check(graph.RepackCompoundGroup(graph.GetGroups().back().id,&packed,&error),error);
            Check(Same(before,Render(graph,pixels,channels)),"Repacking the component-output producer preserves GPU pixels");
        }
        const int index=component=="r"?0:component=="g"?1:component=="b"?2:-1;
        for(std::size_t i=0;i<before.size();i+=4) {
            const float expected=index<0?.7f:float(pixels[(i/4)*channels+index])/255.f*.7f;
            if(std::abs(before[i]-expected)>.003f)throw std::runtime_error("Component output has the wrong channel value.");
        }
        Check(true,"Modified component values agree with the selected image channel");
    }
    using namespace Stack::Editor::NodeGraphUIVisuals;
    GraphStyleTokens tokens;tokens.enabled=true;tokens.monochrome=true;
    const auto r=ChannelColorVec("r",tokens),g=ChannelColorVec("g",tokens),b=ChannelColorVec("b",tokens);
    Check(r.x>r.y*2 && g.y>g.x*2 && b.z>b.x*2,"Neutral themes preserve distinct RGB wire colors");
}

struct Canvas {
    EditorModule& editor;EditorNodeGraphUI ui;
    explicit Canvas(EditorModule& e):editor(e) {ui.Initialize();e.CancelGraphAutoFocusTracking();ImGui::GetIO().AddMousePosEvent(-100,-100);}
    ~Canvas(){ui.Shutdown();}
    void Frame(const std::function<void()>& action={}) {
        ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();ImGuiExtras::BeginFrameInputRouting();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Channel outputs",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        if(action)action();
        ui.Render(&editor);editor.FinishRawLayerGraphFrame();ImGui::End();ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,1280,800);glClearColor(.12f,.12f,.12f,1);glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
    void Settle(int count=35){for(int i=0;i<count;++i)Frame();}
};
const Node* ByUuid(const Graph& graph,const std::string& uuid) {
    for(const auto& node:graph.GetNodes())if(node.instanceUuid==uuid)return &node;
    return nullptr;
}
void Editing(EditorModule& editor,bool raw,const std::filesystem::path& folder) {
    if(raw) {
        auto& graph=editor.GetNodeGraph();graph.Clear();
        RawDevelopmentPayload source;source.recipe=Stack::RawRecipe::MakeDefaultRecipe({});
        const int id=graph.AddRawDevelopmentNode(std::move(source),{})->id;
        const int output=graph.AddOutputNode({900,0},true)->id;Wire(graph,id,"imageOut",output,"imageIn");
        editor.DeserializePipeline(editor.SerializePipeline());
        // A source-free graph alone is an editor document. Supply the same
        // public project metadata as a RAW document, without decoding a photo.
        auto& project=editor.GetProjectSession();
        project.snapshot=std::make_shared<Stack::Project::RawProjectSnapshot>();
        project.snapshot->projectId=editor.GetProjectDocumentId();
        project.snapshot->projectKindHint=StackBinaryFormat::kRawProjectKind;
        Check(editor.IsRawWorkspaceProjectActive(),"Native RAW fixture uses RAW snapshot publication");
        editor.OpenRawLayerMaskGraph({});
    } else {editor.GetNodeGraph().Clear();editor.GetNodeGraph().SetAllowNoOutput(true);editor.AddImageGeneratorNodeAt(ImageGeneratorKind::SolidColor,{80,160});}
    int source=0;
    if(raw) {
        for(const auto& node:editor.GetNodeGraph().GetNodes())if(node.role==Stack::GraphModel::NodeRole::OriginalImage && node.rawLayerOwnerId.empty()){source=node.id;break;}
    } else source=editor.GetNodeGraph().GetSelectedNodeId();
    Check(source>0,"Native channel fixture has an image producer");
    editor.AddChannelSplitNodeAt({430,160});const int split=editor.GetNodeGraph().GetSelectedNodeId();
    const auto splitUuid=editor.GetNodeGraph().FindNode(split)->instanceUuid;
    editor.AddDataMathNodeAt(DataMathMode::Multiply,{700,160});const int gain=editor.GetNodeGraph().GetSelectedNodeId();
    const auto gainUuid=editor.GetNodeGraph().FindNode(gain)->instanceUuid;
    std::string error;Check(editor.ConnectGraphSockets(source,"imageOut",split,"imageIn",&error),error);
    Canvas canvas(editor);canvas.Settle(8);
    const auto start=std::chrono::steady_clock::now();
    canvas.Frame([&]{Check(canvas.ui.ConnectBestOutputToInput(&editor,split,gain,"imageA"),"Native connection folds the split in its graph transaction");});
    const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout<<(raw?"RAW":"Ordinary")<<" connection frame milliseconds: "<<milliseconds<<'\n';
    Check(!ByUuid(editor.GetNodeGraph(),splitUuid),"Native folded split is absent from the authored graph");
    Check(editor.GetNodeGraph().GetSelectedNodeId()==source,"Selection follows the producer when a split folds away");
    Check(canvas.ui.HasLayoutMotion(),"Channel folding starts finite presentation motion");
    canvas.Settle(70);Check(!canvas.ui.HasLayoutMotion(),"Channel folding motion finishes and becomes inactive");
    const auto* math=ByUuid(editor.GetNodeGraph(),gainUuid);
    Check(math && editor.GetNodeGraph().FindInputLink(math->id,"imageA"),"Folded wire remains connected after motion settles");
    const auto* value=math?editor.GetGraphOutputDescription(math->id,"imageOut"):nullptr;
    if(!value || OutputChannelColor(*value)!="r") {
        std::cerr<<"Published component description present: "<<(value!=nullptr)<<'\n';
        if(value) {
            std::cerr<<"Component origin: "<<value->componentOrigin<<"; role: "<<value->componentRole<<'\n';
            for(const auto& diagnostic:value->diagnostics)std::cerr<<diagnostic.message<<'\n';
        }
        for(const auto& diagnostic:editor.GetGraphSemanticDiagnostics())std::cerr<<diagnostic.message<<'\n';
    }
    Check(value && OutputChannelColor(*value)=="r","Published ordinary/RAW semantics retain modified channel color");
    Check(editor.UndoProjectGraphEdit() && ByUuid(editor.GetNodeGraph(),splitUuid),"One undo restores the visible split");
    Check(editor.RedoProjectGraphEdit() && !ByUuid(editor.GetNodeGraph(),splitUuid),"One redo restores the direct component output");
    canvas.Settle(70);
    const auto saved=editor.SerializePipeline();editor.DeserializePipeline(saved);
    if(raw)editor.OpenRawLayerMaskGraph({});
    const auto* reopened=ByUuid(editor.GetNodeGraph(),gainUuid);
    Check(reopened && editor.GetNodeGraph().FindInputLink(reopened->id,"imageA"),"Native serialization retains the folded connection");
    if(!raw) {
        canvas.Settle();
        std::vector<unsigned char> pixels(1280*800*4);glReadPixels(0,0,1280,800,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        for(int y=0;y<400;++y)for(int x=0;x<1280*4;++x)std::swap(pixels[std::size_t(y)*1280*4+x],pixels[std::size_t(799-y)*1280*4+x]);
        Check(stbi_write_png((folder/"direct-channel-output.png").string().c_str(),1280,800,4,pixels.data(),1280*4)!=0,"Native colored component-wire capture saved");
    }
}
}
bool ValidateGraphChannelOutputs(const std::filesystem::path& folder) {
    checks=0;if(!glfwInit())return false;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    auto* window=glfwCreateWindow(1280,800,"Channel outputs validation",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}
    glfwMakeContextCurrent(window);if(!LoadGLFunctions()){glfwDestroyWindow(window);glfwTerminate();return false;}
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1280,800};io.DeltaTime=1.f/60.f;
    StackAppearance::AppearanceManager appearance;appearance.Load();appearance.SetupFonts(io);appearance.ApplyCurrentTheme(io,ImGui::GetStyle());
    ImGui_ImplOpenGL3_Init("#version 430");ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();ImGui::EndFrame();
    Async::TaskSystem::Get().Initialize();
    bool success=false;auto editor=std::make_unique<EditorModule>();editor->SetDocumentPersistenceEnabled(false);editor->Initialize(window,&appearance,false);
    try {
        std::filesystem::create_directories(folder);Pixels();Editing(*editor,false,folder);Editing(*editor,true,folder);
        std::ofstream(folder/"result.json")<<nlohmann::json{{"passed",true},{"checks",checks}}.dump(2)<<'\n';success=true;
    }catch(const std::exception& error){std::cerr<<"Channel outputs validation: "<<error.what()<<'\n';}
    editor->RequestWorkerShutdownForAppClose();Async::TaskSystem::Get().Shutdown();editor.reset();
    ImGui_ImplOpenGL3_Shutdown();ImGui::DestroyContext();glfwDestroyWindow(window);glfwTerminate();return success;
}
}
