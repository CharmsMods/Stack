#include "App/Validation/ValidationSuites.h"
#include "App/UiPerformanceProbe.h"
#include "App/UiPerformanceTrace.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/GraphEditorCommands.h"
#include "Editor/Layers/SplitBlurLayers.h"
#include "Project/GraphSnapshotLookup.h"
#include "Graph/LayerOperationParameters.h"
#include "Project/RawLayerStackSnapshot.h"
#include "App/Validation/Suites/RawViewportValidationFixture.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/NodeGraph/GraphExpansionLayout.h"
#include "Notifications/NotificationPresenter.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "Editor/NodeGraph/UI/GraphLayoutMotionTiming.h"
#include "NodeMath/SourceImageSemantics.h"
#include "Persistence/StackBinaryFormat.h"
#include "Renderer/GLLoader.h"
#include "Renderer/FullscreenQuad.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ScopedGLObjects.h"
#include "Renderer/RenderPipeline.h"
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "Utils/ImGuiExtras.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace Stack::Validation {
nlohmann::json CheckGraphWireRouteCache();
namespace {
using namespace EditorNodeGraph;
using namespace NodeMath;
struct Checks {
    std::filesystem::path folder;
    nlohmann::json results=nlohmann::json::array();
    int failures=0;
    void Check(bool pass,const std::string& id,const std::string& detail) {
        results.push_back({{"id",id},{"passed",pass},{"detail",detail}});
        std::ofstream(folder/"checks.jsonl",std::ios::app)<<results.back().dump()<<'\n';
        std::cout<<(pass?"PASS ":"FAIL ")<<id<<": "<<detail<<'\n';if(!pass)++failures;
    }
    void Near(double actual,double expected,const std::string& id) {
        Check(std::isfinite(actual) && std::abs(actual-expected)<=1e-6+1e-5*std::abs(expected),id,
            "actual="+std::to_string(actual)+", expected="+std::to_string(expected));
    }
    void Save(const Graph& graph,const char* filename) {
        const auto payload=SerializeGraphPayload(nlohmann::json::array(),graph);
        std::ofstream(folder/filename)<<payload.dump(2);
        // Use Stack's existing preset writer to retain binary image data.
        // JSON is a readable inspection copy, not a substitute encoding.
        auto path=folder/filename;path.replace_extension(".stackpreset");
        StackBinaryFormat::NodePresetDocument document;
        document.metadata.id=GenerateCanonicalUuid();document.metadata.displayName=path.stem().string();
        document.metadata.nodeCount=static_cast<std::uint32_t>(graph.GetNodes().size());
        document.graphPayload=payload;
        if(!StackBinaryFormat::WriteNodePresetFile(path,document))throw std::runtime_error("Could not save graph example "+path.string());
        const auto restored=Load(filename);
        bool sameImages=true;
        for(const auto& node:graph.GetNodes())if(node.kind==NodeKind::Image) {
            const auto* other=restored.FindNode(node.id);
            const auto& pixels=node.image.pixels.empty() && node.image.sharedPixels ? *node.image.sharedPixels : node.image.pixels;
            sameImages &= other && pixels.size()==other->image.pixels.size() && std::equal(pixels.begin(),pixels.end(),other->image.pixels.begin());
        }
        Check(sameImages,"G08.disk",std::string(filename)+": native preset file reload retains exact source pixels");
    }
    Graph Load(const char* filename) const {
        auto path=folder/filename;path.replace_extension(".stackpreset");StackBinaryFormat::NodePresetDocument document;
        if(!StackBinaryFormat::ReadNodePresetFile(path,document))throw std::runtime_error("Could not reload graph example "+path.string());
        Graph graph;DeserializeGraphPayload(document.graphPayload,graph,0);return graph;
    }
};

std::vector<float> Read(RenderPipeline& pipeline,int width,int height) {
    const auto texture=pipeline.GetOutputTexture();
    if(!texture)return {};
    glBindTexture(GL_TEXTURE_2D,texture);
    GLint w=0,h=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&w);glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_HEIGHT,&h);
    if(w!=width || h!=height)return {};
    std::vector<float> result(std::size_t(w)*h*4);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,result.data());return result;
}
double Difference(const std::vector<float>& a,const std::vector<float>& b) {
    if(a.empty()||a.size()!=b.size())return INFINITY;
    double maximum=0;for(std::size_t i=0;i<a.size();++i){if(!std::isfinite(a[i])||!std::isfinite(b[i]))return INFINITY;maximum=std::max(maximum,double(std::abs(a[i]-b[i])));}return maximum;
}
RenderGraphNode RenderNode(int id,RenderGraphNodeKind kind) {
    RenderGraphNode node;node.nodeId=id;node.kind=kind;node.definitionId="stack:validation/graph-modularity";return node;
}
std::vector<float> MathPixels(ArithmeticMode mode,std::array<float,4> a,float b,float mask=-1) {
    RenderPipeline pipeline;pipeline.Initialize();
    const unsigned char blank[4]={0,0,0,255};pipeline.LoadSourceFromPixels(blank,1,1,4);
    glBindTexture(GL_TEXTURE_2D,pipeline.GetSourceTexture());glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,1,1,0,GL_RGBA,GL_FLOAT,a.data());
    auto input=RenderNode(1,RenderGraphNodeKind::Image),math=RenderNode(2,RenderGraphNodeKind::DataMath),output=RenderNode(3,RenderGraphNodeKind::Output);
    math.dataMathMode=mode;math.dataMathSettings.constantB=b;
    RenderGraphSnapshot snapshot;snapshot.outputNodeId=3;snapshot.outputSocketId="imageOut";
    snapshot.nodes={input,math,output};snapshot.links={{1,"imageOut",2,"imageA"},{2,"imageOut",3,"imageIn"}};
    if(mask>=0) {
        auto split=RenderNode(4,RenderGraphNodeKind::ChannelSplit),coverage=RenderNode(5,RenderGraphNodeKind::ConstantChannel);
        coverage.constantChannelValue=mask;snapshot.nodes.push_back(split);snapshot.nodes.push_back(coverage);
        snapshot.links.push_back({1,"imageOut",4,"imageIn"});snapshot.links.push_back({4,"r",5,"matchExtent"});snapshot.links.push_back({5,"channelOut",2,"maskIn"});
    }
    pipeline.ExecuteGraph(snapshot);return Read(pipeline,1,1);
}

const CompoundDefinition& Definition(const char* id) {
    const auto& catalog=EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
    const std::string full=std::string("stack:compound/")+id;
    const auto found=std::find_if(catalog.begin(),catalog.end(),[&](const auto& d){return d.identity.id==full;});
    if(found==catalog.end())throw std::runtime_error("Missing recipe "+full);return *found;
}
int AddCompound(Graph& graph,const char* id) {
    const auto& catalog=EditorNodeGraphDefinitions::GetShippedCompoundTemplates();const auto& definition=Definition(id);
    std::string error;
    const auto closure=CollectCompoundDependencyClosure(catalog,{definition.identity},&error);
    if(closure.empty())throw std::runtime_error(error);
    for(const auto& reference:closure)if(!graph.AddCompoundDefinition(*FindExactCompoundDefinition(catalog,reference),&error))throw std::runtime_error(error);
    return graph.AddCompoundNode(definition.identity,{300,0})->id;
}
void Wire(Graph& graph,int a,const char* out,int b,const char* in) {
    std::string error;if(!graph.TryConnectSockets(a,out,b,in,&error))throw std::runtime_error(error);
}
struct Fixture {Graph graph;int source=0,effect=0,output=0,width=33,height=33;std::vector<unsigned char> pixels;std::vector<float> linearPixels;};
Fixture Build(const char* id,bool impulse=true,int width=33,int height=33) {
    Fixture f;f.width=width;f.height=height;f.graph.Clear();f.pixels.resize(std::size_t(width)*height*4);
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){const auto i=(std::size_t(y)*width+x)*4;f.pixels[i+3]=255;
        if(!impulse || (x==width/2 && y==height/2))f.pixels[i]=f.pixels[i+1]=f.pixels[i+2]=255;}
    ImagePayload image;image.width=width;image.height=height;image.channels=image.originalChannels=4;image.label="Linear sRGB fixture";image.pixels=f.pixels;
    image.sourceColorMetadata.descriptor=DescribeStoredImageValue({},4,width,height);
    auto& d=image.sourceColorMetadata.descriptor;
    d.color=SemanticField<ColorIdentity>::Known({"srgb-d65",{},ColorRelation::Standard});
    d.transfer=SemanticField<TransferDescriptor>::Known({TransferKind::Linear,0,{}});
    d.reference=SemanticField<ReferenceState>::Known(ReferenceState::Scene);
    d.alpha=SemanticField<AlphaMode>::Known(AlphaMode::Straight);
    f.source=f.graph.AddImageNode(std::move(image),{})->id;
    f.effect=AddCompound(f.graph,id);f.output=f.graph.AddOutputNode({800,0},true)->id;
    if (std::string(id)=="colorize") {
        const int split=f.graph.AddChannelSplitNode({})->id;
        Wire(f.graph,f.source,"imageOut",split,"imageIn");Wire(f.graph,split,"r",f.effect,"image-in");
    } else if(std::string(id)=="average-three" || std::string(id)=="linear-mix") {
        Wire(f.graph,f.source,"imageOut",f.effect,"a");
        const int b=f.graph.AddValueNode(MakeUniformScalar(0),{})->id;
        f.graph.FindNode(b)->value.value.payload=std::string(id)=="linear-mix"?1.:0.;
        Wire(f.graph,b,"valueOut",f.effect,"b");
        if(std::string(id)=="average-three") {
            const int c=f.graph.AddValueNode(MakeUniformScalar(1),{})->id;
            Wire(f.graph,c,"valueOut",f.effect,"c");
        }
    } else Wire(f.graph,f.source,"imageOut",f.effect,"image-in");
    Wire(f.graph,f.effect,"image-out",f.output,"imageIn");
    return f;
}
std::vector<float> Render(const Fixture& fixture,const Graph& graph) {
    EditorModule editor;editor.GetNodeGraph()=graph.CloneForAnalysis();
    auto snapshot=editor.BuildGraphSnapshot();snapshot.executionInspectionEnabled=true;
    // Use the public prepared-source texture path to retain HDR fixture data.
    // All operation nodes, bindings, descriptors and lowering are production.
    if (!fixture.linearPixels.empty())
        for (auto& node : snapshot.nodes) if (node.nodeId == fixture.source) node.image.pixels = {};
    RenderPipeline pipeline;pipeline.Initialize();pipeline.LoadSourceFromPixels(fixture.pixels.data(),fixture.width,fixture.height,4);
    if (!fixture.linearPixels.empty()) {
        glBindTexture(GL_TEXTURE_2D,pipeline.GetSourceTexture());
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,fixture.width,fixture.height,0,GL_RGBA,GL_FLOAT,fixture.linearPixels.data());
    }
    pipeline.ExecuteGraph(snapshot);
    auto result=Read(pipeline,fixture.width,fixture.height);
    if(result.empty())std::cerr<<"Render failed: "<<pipeline.GetLastGraphExecutionStats().lastSpecializedFailure<<'\n';
    return result;
}
void Set(Graph& graph,int id,const char* parameter,double value){SetCompoundParameterOverride(graph.FindNode(id)->compound.instance,parameter,value);}
void Fill(Fixture& f,std::array<float,4> value) {
    f.linearPixels.resize(std::size_t(f.width)*f.height*4);
    for(std::size_t i=0;i<f.linearPixels.size();i+=4)std::copy(value.begin(),value.end(),f.linearPixels.begin()+i);
    // Keep the saved finite-range example source consistent with the actual
    // float fixture. HDR tests still use the prepared float source above.
    for(std::size_t i=0;i<f.pixels.size();++i)
        f.pixels[i]=static_cast<unsigned char>(std::lround(std::clamp(f.linearPixels[i],0.f,1.f)*255.f));
    auto& source=f.graph.FindNode(f.source)->image;
    source.pixels=f.pixels;source.sharedPixels.reset();source.pixelsFingerprint=0;
}
std::vector<float> Signal(const Fixture& f,const char* socket) {
    auto graph=f.graph.CloneForAnalysis();
    for(auto& link:graph.EditLinks())if(link.toNodeId==f.output)link.fromSocketId=socket;
    return Render(f,graph);
}
void SavePixels(const std::filesystem::path& path,const std::vector<float>& pixels,int width,int height) {
    if(pixels.empty())return;
    // PFM is the actual scene-linear floating RGB, before any display curve.
    std::ofstream file(path,std::ios::binary);file<<"PF\n"<<width<<' '<<height<<"\n-1.0\n";
    for(std::size_t i=0;i<pixels.size();i+=4)file.write(reinterpret_cast<const char*>(pixels.data()+i),3*sizeof(float));
}

void Numerics(Checks& c) {
    const auto multiply=MathPixels(ArithmeticMode::Multiply,{.8f,.7f,.6f,.37f},.5f);
    c.Check(multiply.size()==4,"N01","RGB multiplication renders");
    if(multiply.size()==4){c.Near(multiply[0],.4,"N01.R");c.Near(multiply[1],.35,"N01.G");c.Near(multiply[2],.3,"N01.B");c.Near(multiply[3],.37,"N01.alpha");}
    for(float b:{0.f,1.f}){auto p=MathPixels(ArithmeticMode::Multiply,{.8f,.7f,.6f,.37f},b);if(p.size()==4)c.Near(p[0],.8*b,"N02");else c.Check(false,"N02","No render");}
    for(float m:{0.f,.5f,1.f}){auto p=MathPixels(ArithmeticMode::Multiply,{.2f,.2f,.2f,1},2,m);if(p.size()==4)c.Near(p[0],.2*(1+m),"N03");else c.Check(false,"N03","No render");}
    auto sub=MathPixels(ArithmeticMode::Subtract,{.2f,.2f,.2f,1},.7f);c.Check(sub.size()==4,"N07.render","Subtraction renders");if(sub.size()==4)c.Near(sub[0],-.5,"N07");
    for(auto [a,b,expected]:std::array<std::array<float,3>,4>{{{.6f,.2f,3},{1,0,0},{1,1e-8f,1e8f},{.6f,-.2f,-3}}}) {
        auto p=MathPixels(ArithmeticMode::Divide,{a,a,a,1},b);if(p.size()==4)c.Near(p[0],expected,"N08");else c.Check(false,"N08","No render");
    }
    auto hdr=MathPixels(ArithmeticMode::Multiply,{4,4,4,1},2);if(hdr.size()==4)c.Near(hdr[0],8,"N11");
    for(auto [b,expected]:std::array<std::pair<float,double>,3>{{{.20f,1.18},{.03f,1.01},{0,.98}}}) {
        auto p=MathPixels(ArithmeticMode::Add,{.98f,.98f,.98f,1},b);c.Check(p.size()==4,"N06.render","Add renders without saturation");if(p.size()==4)c.Near(p[0],expected,"N06");
    }
    auto zero=MathPixels(ArithmeticMode::Multiply,{0,0,0,1},100);if(zero.size()==4)c.Near(zero[0],0,"N12");
    ArithmeticSettings settings;settings.domain=ArithmeticDomain::Values;double value=0;std::string error;
    c.Check(EvaluateUniformArithmetic(ArithmeticMode::Average,settings,{0,0,1},value,error),"N09","Uniform average evaluates");c.Near(value,1./3,"N09.value");
    {
        auto f=Build("average-three",false,1,1);Fill(f,{0,0,0,1});f.graph.RemoveNode(f.effect);
        const int split=f.graph.AddChannelSplitNode({})->id,average=f.graph.AddDataMathNode(DataMathMode::Average,{})->id;
        const int zeroValue=f.graph.AddValueNode(MakeUniformScalar(0),{})->id,oneValue=f.graph.AddValueNode(MakeUniformScalar(1),{})->id;
        Wire(f.graph,f.source,"imageOut",split,"imageIn");Wire(f.graph,split,"r",average,"imageA");
        Wire(f.graph,zeroValue,"valueOut",average,"imageB");Wire(f.graph,oneValue,"valueOut",average,"imageC");Wire(f.graph,average,"imageOut",f.output,"imageIn");
        const auto p=Render(f,f.graph);c.Check(p.size()==4,"N09.native-render","Variable-input Average mixes one spatial field with two uniform inputs");if(p.size()==4)c.Near(p[0],1./3,"N09.native");
    }
    c.Check(EvaluateUniformArithmetic(ArithmeticMode::Divide,settings,{1,0},value,error)&&value==0&&!error.empty(),"N08.diagnostic","Zero division returns zero with an explicit diagnostic");
    for(double p:{0.,50.,100.,200.}) {auto f=Build("percent",false,1,1);Fill(f,{.4f,.4f,.4f,1});Set(f.graph,f.effect,"percent",p);auto pixels=Render(f,f.graph);if(pixels.size()==4)c.Near(pixels[0],.4*p/100,"N04");else c.Check(false,"N04","Percent does not render");}
    {auto f=Build("percent",false,1,1);Set(f.graph,f.effect,"percent",21.26);auto p=Render(f,f.graph);auto q=MathPixels(ArithmeticMode::Multiply,{1,1,1,1},.2126f);c.Check(Difference(p,q)<1e-6,"N05","Percent and fractional Multiply agree");}
    for(float v:{0.f,.25f,1.f}){auto f=Build("invert-mask",false,1,1);Fill(f,{v,v,v,1});auto p=Render(f,f.graph);if(p.size()==4)c.Near(p[0],1-v,"N10");else c.Check(false,"N10","No render");}
    {auto f=Build("colorize",false,1,1);Fill(f,{.2f,.2f,.2f,1});
        Set(f.graph,f.effect,"g",.25);Set(f.graph,f.effect,"b",.05);auto p=Render(f,f.graph);
        c.Check(p.size()==4,"N13.render","Colorize renders");if(p.size()==4){c.Near(p[0],.2,"N13.R");c.Near(p[1],.05,"N13.G");c.Near(p[2],.01,"N13.B");}}
    {auto f=Build("channel-mixer",false,1,1);Fill(f,{0,1,0,.4f});Set(f.graph,f.effect,"rr",0);Set(f.graph,f.effect,"rg",1);auto p=Render(f,f.graph);
        c.Check(p.size()==4,"N14.render","Channel Mixer renders");if(p.size()==4){c.Near(p[0],1,"N14");c.Near(p[3],.4,"N14.alpha");}c.Save(f.graph,"standalone-channel-mixer.json");}
    for(const char* id:{"average-three","linear-mix"}) {
        auto f=Build(id,false,1,1);Fill(f,{0,0,0,1});if(std::string(id)=="linear-mix")Set(f.graph,f.effect,"factor",.25);
        auto p=Render(f,f.graph);c.Check(p.size()==4,std::string("G18.")+id,"Standalone recipe with uniform operands renders");
        if(p.size()==4)c.Near(p[0],std::string(id)=="average-three"?1./3:.25,std::string(id)=="average-three"?"N09.gpu":"G18.mix");
    }
}

void ContractsAndPersistence(Checks& c) {
    auto f=Build("luminance-srgb",false,1,1);Fill(f,{.4f,.6f,.8f,1});
    Set(f.graph,f.effect,"r",50);Set(f.graph,f.effect,"g",25);Set(f.graph,f.effect,"b",0);
    const auto expected=Render(f,f.graph);if(expected.size()==4)c.Near(expected[0],.35,"C04.custom");
    Graph loaded;const auto saved=SerializeGraphPayload(nlohmann::json::array(),f.graph);
    DeserializeGraphPayload(saved,loaded,0);
    c.Check(Difference(expected,Render(f,loaded))<1e-6,"G08.compact","Custom luminance weights survive compact save/load");
    std::string error;std::vector<int> children;
    c.Check(loaded.UnpackCompoundNode(f.effect,&children,&error),"C04.decompile",error);
    Graph partial;DeserializeGraphPayload(SerializeGraphPayload(nlohmann::json::array(),loaded),partial,0);
    c.Check(Difference(expected,Render(f,partial))<1e-6 && !partial.GetGroups().empty(),"G08.partial","Partly expanded graph restores pixels and retained boundary");
    if(!partial.GetGroups().empty())c.Check(partial.RepackCompoundGroup(partial.GetGroups().back().id,nullptr,&error) &&
        Difference(expected,Render(f,partial))<1e-6,"C04.repack","Custom weights survive repacking after reload");
    const auto before=SerializeGraphPayload(nlohmann::json::array(),partial);
    for(int version:{0,kEditorNodeGraphSchemaVersion-1}) {
        auto obsolete=saved;obsolete["nodeGraph"]["version"]=version;bool rejected=false;
        try {DeserializeGraphPayload(obsolete,partial,0);}catch(const std::exception& e){rejected=std::string(e.what()).find("Unsupported graph model generation")!=std::string::npos;}
        c.Check(rejected && SerializeGraphPayload(nlohmann::json::array(),partial)==before,"G20.reject","Obsolete graph format rejects without changing the live graph");
    }
    Graph cycle;cycle.SetAllowNoOutput(true);int a=cycle.AddDataMathNode(DataMathMode::Multiply,{})->id,b=cycle.AddDataMathNode(DataMathMode::Multiply,{})->id;
    Wire(cycle,a,"imageOut",b,"imageA");c.Check(!cycle.TryConnectSockets(b,"imageOut",a,"imageA",&error),"G13.cycle","Cyclic data dependency is rejected");
    auto recursive=Definition("percent");recursive.dependencies.push_back(recursive.identity);
    const auto recursiveIssues=ValidateCompoundDefinition(recursive);
    c.Check(std::any_of(recursiveIssues.begin(),recursiveIssues.end(),[](const auto& issue){return issue.message.find("cannot depend on itself")!=std::string::npos;}) &&
        !cycle.AddCompoundDefinition(recursive,&error),"G13.recursion","Self-referencing compound has an explicit recursion diagnostic and is rejected");
    auto percent=Build("percent",false,1,1);Fill(percent,{.4f,.4f,.4f,.3f});
    int value=percent.graph.AddValueNode(MakeUniformScalar(50,{UnitKind::Percent,{}}),{})->id;
    Wire(percent.graph,value,"valueOut",percent.effect,"param:percent");auto p=Render(percent,percent.graph);
    c.Check(p.size()==4,"G16.percent-render","Explicit percentage constant connects to Percent");if(p.size()==4)c.Near(p[0],.2,"G16.percent");
    auto integer=MakeUniformScalar(50);integer.logicalType=LogicalValueType::Integer;integer.payload=std::int64_t(50);
    percent.graph.RemoveLink(value,"valueOut",percent.effect,"param:percent");
    value=percent.graph.AddValueNode(integer,{})->id;Wire(percent.graph,value,"valueOut",percent.effect,"param:percent");p=Render(percent,percent.graph);
    c.Check(p.size()==4 && std::abs(p[0]-.2)<1e-6,"G16.integer","Integer drives percentage division through uniform floating arithmetic");
    auto mixed=Build("channel-mixer",false,1,1);mixed.graph.RemoveNode(mixed.effect);
    const int splitA=mixed.graph.AddChannelSplitNode({})->id,splitB=mixed.graph.AddChannelSplitNode({})->id;
    const int assign=mixed.graph.AddTechnicalImageNode(TechnicalImageOperation::AssignLinearDisplayP3,{})->id;
    const int combine=mixed.graph.AddChannelCombineNode({})->id;
    Wire(mixed.graph,mixed.source,"imageOut",splitA,"imageIn");Wire(mixed.graph,mixed.source,"imageOut",assign,"imageIn");Wire(mixed.graph,assign,"imageOut",splitB,"imageIn");
    Wire(mixed.graph,splitA,"r",combine,"r");Wire(mixed.graph,splitB,"g",combine,"g");Wire(mixed.graph,splitA,"b",combine,"b");Wire(mixed.graph,combine,"imageOut",mixed.output,"imageIn");
    c.Check(Render(mixed,mixed.graph).empty(),"G16.basis","Combining channels from conflicting declared RGB bases is rejected");
    auto bad=Build("percent",false,1,1);int missing=bad.graph.AddConstantChannelNode({})->id;
    Wire(bad.graph,missing,"channelOut",bad.effect,"mask-in");
    c.Check(Render(bad,bad.graph).empty(),"G12","A connected mask with missing extent blocks execution");
    auto ownership=Build("percent",false,1,1);ownership.graph.FindNode(ownership.effect)->rawLayerOwnerId="validation-owner";
    error.clear();
    c.Check(ownership.graph.UnpackCompoundNode(ownership.effect,&children,&error),"G19.decompile",error);
    c.Check(std::count_if(ownership.graph.GetNodes().begin(),ownership.graph.GetNodes().end(),[](const auto& n){return n.kind==NodeKind::Output;})==1 &&
        std::all_of(children.begin(),children.end(),[&](int id){return ownership.graph.FindNode(id)->rawLayerOwnerId=="validation-owner";}),
        "G19.ownership","Expansion keeps one final Output and carries the existing layer owner");
}

void BlurResources(Checks& c) {
    // Exercise the serial executor shared by independent Gaussian nodes, with
    // actual HDR/alpha output comparison against fresh uncached executors.
    constexpr int width=33,height=25;
    std::vector<float> pixels(width*height*4);
    for (int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const auto i=(y*width+x)*4;
        pixels[i]=x==16 && y==12 ? 8.f : .125f;
        pixels[i+1]=float(x)/width;pixels[i+2]=float(y)/height;
        pixels[i+3]=float((x+y)%7+1)/8;
    }
    const Renderer::GLState::FramebufferState destination(true);
    Renderer::ScopedGLTexture input(GLHelpers::CreateTextureFromData(pixels.data(),width,height,GL_RGBA32F,GL_RGBA,GL_FLOAT));
    Renderer::ScopedGLTexture output(GLHelpers::CreateStorageTexture(width,height,GL_RGBA32F));
    const auto fbo=output ? GLHelpers::CreateFBO(output.Get()) : 0;
    if(!input || !fbo) throw std::runtime_error("Blur resource fixture allocation failed.");
    FullscreenQuad quad;quad.Initialize();GaussianBlurLayer shared;shared.InitializeGL();
    const auto draw=[&](GaussianBlurLayer& layer,float sigma) {
        layer.Deserialize({{"amount",sigma},{"sigmaMode",true}});
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,width,height);
        layer.Execute(input.Get(),width,height,quad);
        std::vector<float> result(pixels.size());glBindTexture(GL_TEXTURE_2D,output.Get());
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,result.data());return result;
    };
    bool equal=true;
    for(float sigma : {1.f,2.f,1.f,2.f}) {
        const auto actual=draw(shared,sigma);
        GaussianBlurLayer fresh;fresh.InitializeGL();const auto expected=draw(fresh,sigma);
        equal &= Difference(actual,expected)<1e-6;
        for(std::size_t i=3;i<actual.size();i+=4) equal &= actual[i]==pixels[i];
    }
    const auto stats=shared.GetResourceStats();
    c.Check(equal,"M04.resource-parity","Alternating Gaussian radii match fresh HDR/alpha execution");
    c.Check(stats.kernelUploads==2 && stats.scratchAllocations==1 && stats.scratchReuses==3 &&
        stats.retainedScratchBytes==std::uint64_t(width)*height*16,
        "M04.resource-reuse","Two radii retain two kernels and reuse one bounded scratch allocation");
    std::vector<float> channel(width*height);
    for(std::size_t i=0;i<channel.size();++i)channel[i]=pixels[i*4];
    input.Reset(GLHelpers::CreateTextureFromData(channel.data(),width,height,GL_R32F,GL_RED,GL_FLOAT));
    const auto scalar=draw(shared,1.f);
    GaussianBlurLayer scalarReference;scalarReference.InitializeGL();
    const auto expectedScalar=draw(scalarReference,1.f);
    const bool scalarStorage=shared.GetResourceStats().retainedScratchBytes==std::uint64_t(width)*height*4;
    input.Reset(GLHelpers::CreateTextureFromData(pixels.data(),width,height,GL_RGBA32F,GL_RGBA,GL_FLOAT));
    const auto color=draw(shared,1.f);
    GaussianBlurLayer colorReference;colorReference.InitializeGL();
    const auto expectedColor=draw(colorReference,1.f);
    c.Check(scalarStorage && Difference(scalar,expectedScalar)<1e-6 && Difference(color,expectedColor)<1e-6 &&
        shared.GetResourceStats().scratchAllocations==3 && shared.GetResourceStats().kernelUploads==2,
        "M04.resource-format","Scalar/color transitions replace scratch storage without stale pixels or kernel churn");
    glDeleteFramebuffers(1,&fbo);destination.Restore(true);
}

void NativeDecomposition(Checks& c) {
    for(bool enabled : {true,false}) {
        auto f=Build("percent",false,3,1);f.graph.RemoveNode(f.effect);
        Fill(f,{0,0,0,1});f.linearPixels[0]=-.5f;f.linearPixels[4]=.25f;f.linearPixels[8]=2.f;
        const int split=f.graph.AddChannelSplitNode({})->id;
        f.effect=f.graph.AddMaskUtilityNode(MaskUtilityKind::Invert,{300,0})->id;
        f.graph.FindNode(f.effect)->maskUtilitySettings.enabled=enabled;
        Wire(f.graph,f.source,"imageOut",split,"imageIn");Wire(f.graph,split,"r",f.effect,"maskIn");
        Wire(f.graph,f.effect,"maskOut",f.output,"imageIn");
        const auto native=Render(f,f.graph);
        EditorModule editor;editor.SetDocumentPersistenceEnabled(false);editor.GetNodeGraph()=f.graph.CloneForAnalysis();
        std::string error;
        const bool opened=editor.UnpackCompoundNode(f.effect,&error);
        c.Check(opened,"Native.invert-open",error.empty()?"Native Invert exposes an exact arithmetic recipe":error);
        if(!opened)continue;
        const auto expanded=Render(f,editor.GetNodeGraph());
        const bool exact=native.size()==12 && Difference(native,expanded)<1e-6 &&
            native[0]==(enabled?1.5f:-.5f) && native[4]==(enabled?.75f:.25f) && native[8]==(enabled?-1.f:2.f);
        c.Check(exact,"Native.invert-parity","Enabled/bypassed native and decompiled forms preserve signed/HDR Channel samples");
        const int group=editor.GetNodeGraph().GetGroups().empty() ? -1 : editor.GetNodeGraph().GetGroups().back().id;
        const bool packed=editor.RepackCompoundGroup(group,&error);
        c.Check(packed && Difference(native,Render(f,editor.GetNodeGraph()))<1e-6,
            "Native.invert-repack",error.empty()?"Repacking retains the exact native arithmetic result":error);
    }
}

void ColorAndSpatial(Checks& c) {
    BlurResources(c);
    const std::array<double,3> exact={.21263900587151036,.7151686787677559,.07219231536073371};
    for(int channel=0;channel<3;++channel){auto f=Build("luminance-srgb",false,1,1);std::array<float,4> v{0,0,0,1};v[channel]=1;Fill(f,v);
        auto p=Render(f,f.graph);c.Check(p.size()==4,"C01.render","Luminance primary renders");if(p.size()==4)c.Near(p[0],exact[channel],"C01.matrix");
        Set(f.graph,f.effect,"r",21.26);Set(f.graph,f.effect,"g",71.52);Set(f.graph,f.effect,"b",7.22);p=Render(f,f.graph);
        if(p.size()==4)c.Near(p[0],std::array<double,3>{.2126,.7152,.0722}[channel],"C01.rounded");}
    for(float gray:{.5f,1.f,4.f}){auto f=Build("luminance-srgb",false,1,1);Fill(f,{gray,gray,gray,1});auto p=Render(f,f.graph);
        c.Check(p.size()==4,"C02.render","Linear gray renders");if(p.size()==4)c.Near(p[0],gray,"C02.HDR");}
    {auto f=Build("luminance-rec2020",false,1,1);Fill(f,{1,0,0,1});
        c.Check(Render(f,f.graph).empty(),"C03.mismatch","sRGB input is rejected by the Rec.2020 luminance definition");
        f.graph.FindNode(f.source)->image.sourceColorMetadata.descriptor.color.value.identity="rec2020-d65";
        auto p=Render(f,f.graph);c.Check(p.size()==4,"C03.render","Declared linear Rec.2020 renders");if(p.size()==4)c.Near(p[0],.2627002120112671,"C03.matrix");}
    {auto f=Build("luminance-srgb",false,1,1);Fill(f,{.5f,.5f,.5f,1});
        f.graph.FindNode(f.source)->image.sourceColorMetadata.descriptor.transfer.value.kind=TransferKind::Srgb;
        c.Check(Render(f,f.graph).empty(),"C04.encoded","Encoded sRGB requires explicit decoding");
        f.graph.RemoveLink(f.source,"imageOut",f.effect,"image-in");const int decode=f.graph.AddTechnicalImageNode(TechnicalImageOperation::SrgbDecode,{})->id;
        Wire(f.graph,f.source,"imageOut",decode,"imageIn");Wire(f.graph,decode,"imageOut",f.effect,"image-in");auto p=Render(f,f.graph);
        c.Check(p.size()==4,"C04.render","Explicit sRGB decode renders");if(p.size()==4)c.Near(p[0],.21404114048223255,"C04.linearized");}
    auto f=Build("halation-srgb",false,33,33);Fill(f,{4,4,4,1});Set(f.graph,f.effect,"radius",.05);
    auto blurred=Signal(f,"blurred");c.Check(!blurred.empty(),"M04.render","Gaussian constant HDR field renders");
    if(!blurred.empty())for(int i:{0,16*33*4+16*4,int(blurred.size())-4})c.Near(blurred[i],4,"M04.constant");
    Set(f.graph,f.effect,"radius",0);c.Check(Difference(Signal(f,"extracted"),Signal(f,"blurred"))<1e-6,"M04.zero","Sigma zero is exact bypass");
    auto impulse=Build("halation-srgb");Fill(impulse,{0,0,0,1});const auto center=(std::size_t(16)*33+16)*4;impulse.linearPixels[center]=4;
    Set(impulse.graph,impulse.effect,"radius",.05);Set(impulse.graph,impulse.effect,"source-low",0);Set(impulse.graph,impulse.effect,"source-high",.1);
    auto one=Signal(impulse,"blurred");double sum=0,other=0;for(std::size_t i=0;i<one.size();i+=4){sum+=one[i];other+=std::abs(one[i+1])+std::abs(one[i+2]);}
    c.Near(sum,4,"M03.energy");c.Near(other,0,"M05.channels");impulse.linearPixels[center]=8;auto two=Signal(impulse,"blurred");
    for(auto& v:one)v*=2; // Alpha is not emitted energy; compare RGB only.
    bool doubled=!one.empty()&&one.size()==two.size();for(std::size_t i=0;doubled&&i<one.size();++i)if(i%4!=3)doubled=std::abs(one[i]-two[i])<1e-5;
    c.Check(doubled,"M02","Doubling selected HDR emission doubles blurred RGB");
    Set(impulse.graph,impulse.effect,"protection",0);auto protection=Signal(impulse,"protection-mask");
    c.Check(!protection.empty()&&std::all_of(protection.begin(),protection.end(),[](float v){return std::abs(v-1)<1e-6;}),"M10","Protection amount zero returns white");
    auto source=Signal(impulse,"source-mask"),extracted=Signal(impulse,"extracted");Set(impulse.graph,impulse.effect,"protection-low",3);Set(impulse.graph,impulse.effect,"protection-high",5);
    c.Check(Difference(source,Signal(impulse,"source-mask"))<1e-6&&Difference(extracted,Signal(impulse,"extracted"))<1e-6,"M08.independent","Protection controls preserve source selection and extraction");
    Set(impulse.graph,impulse.effect,"red-low",.4);c.Check(Difference(source,Signal(impulse,"source-mask"))<1e-6,"M06","Red-mask controls preserve source selection");
    Set(impulse.graph,impulse.effect,"red-low",10);Set(impulse.graph,impulse.effect,"red-high",20);
    auto b=Signal(impulse,"blurred"),halo=Signal(impulse,"halo");const auto adjacent=center+4;
    c.Check(!halo.empty()&&!b.empty(),"M07.render","Masked red-gain fixture renders");
    if(!halo.empty()&&!b.empty())c.Near(halo[adjacent],.1*b[adjacent],"M07.black-bypass");
    Set(impulse.graph,impulse.effect,"red-mask",0);halo=Signal(impulse,"halo");
    if(!halo.empty()&&!b.empty())c.Near(halo[adjacent],.18*b[adjacent],"M07.off-uniform");
    Set(impulse.graph,impulse.effect,"protection",1);Set(impulse.graph,impulse.effect,"linked-protection",1);
    protection=Signal(impulse,"protection-mask");bool bounded=!protection.empty();for(std::size_t i=0;i<protection.size();i+=4)bounded&=protection[i]>=0&&protection[i]<=1;
    c.Check(bounded,"M08.linked","Linked protection is bounded");halo=Signal(impulse,"halo");
    if(!halo.empty()){c.Near(halo[center],0,"M09.center");c.Check(halo[adjacent]>0,"M09.neighbor","Protected center still scatters into dark neighbors");}
    Set(impulse.graph,impulse.effect,"aura-radius",.09);Set(impulse.graph,impulse.effect,"aura",0);auto local=Signal(impulse,"blurred");
    Set(impulse.graph,impulse.effect,"aura",100);Set(impulse.graph,impulse.effect,"local",0);auto wide=Signal(impulse,"blurred");
    Set(impulse.graph,impulse.effect,"local",100);auto both=Signal(impulse,"blurred");bool sumLobes=!local.empty()&&wide.size()==local.size()&&both.size()==local.size();
    for(std::size_t i=0;sumLobes&&i<local.size();++i)if(i%4!=3)sumLobes=std::abs(local[i]+wide[i]-both[i])<1e-5;
    c.Check(sumLobes,"M12","Independent local and wide lobes add from the same extracted source");
    Fill(impulse,{0,0,0,1});impulse.linearPixels[0]=8;auto corner=Signal(impulse,"blurred");
    c.Check(!corner.empty()&&corner[corner.size()-4]==0,"M11.corner","Corner emission does not wrap to the opposite corner (full frame)");
    Fill(impulse,{0,0,0,1});auto black=Render(impulse,impulse.graph);bool blackRgb=!black.empty();for(std::size_t i=0;i<black.size();i+=4)blackRgb&=black[i]==0&&black[i+1]==0&&black[i+2]==0;c.Check(blackRgb,"H01","All-black stays black");
    Fill(impulse,{0,0,0,1});impulse.linearPixels[center]=8;impulse.linearPixels[center+3]=0;auto hidden=Signal(impulse,"extracted");
    bool noEmission=!hidden.empty();for(std::size_t i=0;i<hidden.size();i+=4)noEmission&=hidden[i]==0&&hidden[i+1]==0&&hidden[i+2]==0;c.Check(noEmission,"H09.hidden","Hidden transparent RGB emits no light");
    auto rgba=Build("halation-srgb");Fill(rgba,{.75f,.75f,.75f,1});auto rgbaPixels=Render(rgba,rgba.graph);
    auto rgb=rgba.graph.CloneForAnalysis();auto& descriptor=rgb.FindNode(rgba.source)->image.sourceColorMetadata.descriptor;
    const auto components=MakeImageComponentSet({ImageComponent::Red,ImageComponent::Green,ImageComponent::Blue});
    descriptor.presentImageComponents=SemanticField<ImageComponentSet>::Known(components);descriptor.channels=SemanticField<ChannelDescriptor>::Known(MakeImageChannelDescriptor(components));
    descriptor.alpha=SemanticField<AlphaMode>::Known(AlphaMode::Absent);
    c.Check(Difference(rgbaPixels,Render(rgba,rgb))<2e-5,"H09.RGB","Declared RGB uses explicit opaque-alpha fallback and matches opaque RGBA");
    const int overall=rgba.graph.AddValueNode(MakeUniformScalar(0),{})->id;
    Wire(rgba.graph,overall,"valueOut",rgba.effect,"mask-in");auto masked=Render(rgba,rgba.graph);
    c.Check(Difference(masked,rgba.linearPixels)<1e-6,"H02.mask","Black overall destination mask returns the original RGBA");
}

void CanvasInteractionCase(Checks& c, bool raw) {
    namespace Probe = Stack::UiPerformance::Probe;
    namespace Perf = Stack::UiPerformance;
    auto f=Build("halation");
    EditorModule editor;editor.GetNodeGraph()=f.graph.CloneForAnalysis();
    editor.SetDocumentPersistenceEnabled(false);
    if (raw) {
        auto& sourceGraph=editor.GetNodeGraph();sourceGraph.Clear();
        RawDevelopmentPayload source;source.recipe=Stack::RawRecipe::MakeDefaultRecipe({});
        const int sourceId=sourceGraph.AddRawDevelopmentNode(std::move(source),{})->id;
        const int outputId=sourceGraph.AddOutputNode({800,0},true)->id;
        sourceGraph.TryConnect(sourceId,outputId);
        editor.DeserializePipeline(editor.SerializePipeline());editor.OpenRawLayerMaskGraph({});
        const auto& catalog=EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
        const auto found=std::find_if(catalog.begin(),catalog.end(),[](const auto& d){return d.identity.id=="stack:compound/halation";});
        editor.AddCompoundTemplateNodeAt(std::size_t(found-catalog.begin()),{300,320});
        f.effect=editor.GetNodeGraph().GetSelectedNodeId();
    }
    const std::string prefix=raw?"RawDrag.":"Drag.";
    const auto* effect=editor.GetNodeGraph().FindNode(f.effect);
    c.Check(effect && effect->kind==NodeKind::Compound && effect->compound.instance.definition.id=="stack:compound/halation",
        prefix+"fixture","The canvas contains the shipped default Halation compound");
    EditorNodeGraphUI ui;
    EditorModule* canvasEditor=&editor;
    ImGui_ImplOpenGL3_Init("#version 430");
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1400,1000};io.DeltaTime=1.f/60;
    Probe::Enable(c.folder);Probe::WatchEditor(&editor);Probe::WatchNode(f.effect);
    Perf::EnableFrameCapture(true);
    std::function<void()> canvasAction;
    bool drawCanvas = true;
    const auto frame=[&] {
        Probe::BeginFrame();Perf::BeginFrame();
        ImGui_ImplOpenGL3_NewFrame();ImGui::NewFrame();ImGuiExtras::BeginFrameInputRouting();
        ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Halation interaction",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        if(canvasAction){auto action=std::move(canvasAction);canvasAction={};action();}
        const auto start=std::chrono::steady_clock::now();
        if(drawCanvas) {ui.Render(canvasEditor);canvasEditor->FinishRawLayerGraphFrame();}
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        ImGui::End();ImGui::Render();Probe::Presented();
        Perf::FinishFrame({},elapsed,0,elapsed,0,0,0,0);
        return elapsed;
    };
    io.AddMousePosEvent(1300,900);frame();frame();
    const auto region=Probe::Last(Probe::Visual::Node).region;
    const auto before=editor.GetNodeGraph().FindNode(f.effect)->position;
    const auto renderRevision=editor.GetRenderRevision();
    const auto processingRevision=editor.GetProjectSession().rawLayers.ProcessingRevision();
    const float x=region.x+region.width*.5f,y=region.y+12;
    io.AddMousePosEvent(x,y);frame();io.AddMouseButtonEvent(0,true);frame();
    double total=0,peak=0,logical=0;std::uint64_t rebuilds=0;Perf::FrameSample worst;
    for(int i=0;i<12;++i) {
        io.AddMousePosEvent(x+10+i*2,y+8+i);const auto ms=frame();total+=ms;if(ms>peak)worst=Perf::LastFrame();peak=std::max(peak,ms);
        const auto& sample=Perf::LastFrame();logical+=sample.milliseconds[std::size_t(Perf::Stage::LogicalLayout)];
        rebuilds+=sample.counters[std::size_t(Perf::Counter::LogicalRebuild)];
    }
    io.AddMouseButtonEvent(0,false);frame();
    editor.GetProjectSession().graphHistory.EndGesture();
    const auto after=editor.GetNodeGraph().FindNode(f.effect)->position;
    c.Check(after.x!=before.x && after.y!=before.y,prefix+"motion","Actual ImGui mouse events move the compact Halation node");
    c.Check(editor.GetRenderRevision()==renderRevision,prefix+"render","Node placement does not invalidate image processing; before="+
        std::to_string(renderRevision)+", after="+std::to_string(editor.GetRenderRevision()));
    if(raw)c.Check(editor.GetProjectSession().rawLayers.ProcessingRevision()==processingRevision,
        prefix+"processing","Placement leaves the RAW processing revision unchanged");
    c.Check(peak<30,prefix+"frames","12 graph draw/drag frames: mean="+std::to_string(total/12)+" ms, peak="+std::to_string(peak)+" ms, logical="+std::to_string(logical)+" ms, rebuilds="+std::to_string(rebuilds)+", peak links="+std::to_string(worst.milliseconds[std::size_t(Perf::Stage::Links)])+", peak logical="+std::to_string(worst.milliseconds[std::size_t(Perf::Stage::LogicalLayout)])+", peak projection="+std::to_string(worst.milliseconds[std::size_t(Perf::Stage::Projection)]));
    const auto uuid=editor.GetNodeGraph().FindNode(f.effect)->instanceUuid;
    if (raw) {
        Stack::Project::RawLayerStackState restored;std::string error;
        const bool loaded=Stack::Project::DeserializeRawLayerStack(
            Stack::Project::SerializeRawLayerStack(editor.GetProjectSession().rawLayers.State()),restored,error);
        const auto projection=Stack::Project::BuildRawProjectGraph(restored);
        const auto saved=std::find_if(projection.graph.GetNodes().begin(),projection.graph.GetNodes().end(),
            [&](const auto& n){return n.instanceUuid==uuid;});
        c.Check(loaded && saved!=projection.graph.GetNodes().end() && saved->position.x==after.x && saved->position.y==after.y,
            prefix+"persistence","Final drag placement survives the RAW document serializer and projection rebuild");
    }
    const auto position=[&] {for(const auto& n:editor.GetNodeGraph().GetNodes())if(n.instanceUuid==uuid)return n.position;return Vec2{};};
    const bool undone=raw?editor.UndoRawLayerEdit():editor.UndoProjectGraphEdit();const auto undoPosition=position();
    const bool redone=raw?editor.RedoRawLayerEdit():editor.RedoProjectGraphEdit();const auto redoPosition=position();
    c.Check(undone && undoPosition.x==before.x && redone && redoPosition.x==after.x,
        prefix+"history","One drag undo/redo: before="+std::to_string(before.x)+", after="+std::to_string(after.x)+
        ", undo="+std::to_string(undoPosition.x)+", redo="+std::to_string(redoPosition.x));
    for (const auto& n:editor.GetNodeGraph().GetNodes()) if(n.instanceUuid==uuid) f.effect=n.id;
    Probe::WatchNode(f.effect);
    frame();frame();
    auto moved=Probe::Last(Probe::Visual::Node).region;
    auto view=ui.GetGraphCaptureViewportSnapshot();
    float pinX=moved.x-Stack::Editor::NodeGraphUILayout::kSocketCenterOffset*view.zoom;
    float pinY=moved.y+9.5f*view.zoom;
    io.AddMousePosEvent(pinX,pinY);frame();io.AddMouseButtonEvent(0,true);
    const double wireStart=frame();double wireTotal=0,wirePeak=0;bool dragging=false;
    for(int i=0;i<12;++i) {
        io.AddMousePosEvent(moved.x+30+i*2,moved.y+15);const auto ms=frame();wireTotal+=ms;wirePeak=std::max(wirePeak,ms);
        dragging |= editor.GetNodeGraph().GetSocketPreviewIntent(f.effect)==SocketPreviewIntent::ImageConnection;
    }
    c.Check(dragging,prefix+"wire-motion","Actual ImGui pin press starts an image connection drag");
    c.Check(wirePeak<30 && wireStart<30,prefix+"wire-frames","12 wire drag frames: mean="+std::to_string(wireTotal/12)+
        " ms, peak="+std::to_string(wirePeak)+" ms, press="+std::to_string(wireStart)+" ms");
    if(raw && dragging) {
        int source=0;
        for(const auto& n:editor.GetNodeGraph().GetNodes())
            if(n.role==Stack::GraphModel::NodeRole::OriginalImage && n.rawLayerOwnerId.empty()) source=n.id;
        Probe::WatchNode(source);frame();auto sourceRegion=Probe::Last(Probe::Visual::Node).region;
        const bool wasDisconnected=editor.GetNodeGraph().FindAnyInputLink(f.effect,"image-in")==nullptr;
        io.AddMousePosEvent(sourceRegion.x+sourceRegion.width*.5f,sourceRegion.y+sourceRegion.height*.5f);frame();
        io.AddMouseButtonEvent(0,false);const double release=frame();
        const auto* connected=editor.GetNodeGraph().FindAnyInputLink(f.effect,"image-in");
        c.Check(wasDisconnected && connected && connected->fromNodeId==source,prefix+"wire-drop",
            "Releasing the wire on Source preparation commits a validated RAW graph connection");
        c.Check(release<30,prefix+"wire-release","Body-drop release including authoritative RAW commit: "+std::to_string(release)+" ms");
        c.Check(editor.RemoveGraphLink(source,"imageOut",f.effect,"image-in"),prefix+"pin-reset","Disconnect the accepted wire for a direct pin connection");
        // Start a distinct gesture outside ImGui's double-click interval.
        for(int settle=0;settle<20;++settle)frame();
        Probe::WatchNode(f.effect);frame();moved=Probe::Last(Probe::Visual::Node).region;
        view=ui.GetGraphCaptureViewportSnapshot();pinX=moved.x-Stack::Editor::NodeGraphUILayout::kSocketCenterOffset*view.zoom;
        pinY=moved.y+9.5f*view.zoom;
        Probe::WatchNode(source);frame();sourceRegion=Probe::Last(Probe::Visual::Node).region;
        io.AddMousePosEvent(pinX,pinY);frame();io.AddMouseButtonEvent(0,true);frame();
        io.AddMousePosEvent(moved.x+30,moved.y+15);frame();
        c.Check(editor.GetNodeGraph().GetSocketPreviewIntent(f.effect)==SocketPreviewIntent::ImageConnection,
            prefix+"pin-motion","The second gesture presents wire feedback on its hovered node");
        const float outputX=sourceRegion.x+sourceRegion.width+Stack::Editor::NodeGraphUILayout::kSocketCenterOffset*view.zoom;
        io.AddMousePosEvent(outputX,sourceRegion.y+sourceRegion.height*.5f);frame();
        io.AddMouseButtonEvent(0,false);const double pinRelease=frame();
        connected=editor.GetNodeGraph().FindAnyInputLink(f.effect,"image-in");
        c.Check(connected && connected->fromNodeId==source,prefix+"pin-drop","Direct pin-to-pin release commits the RAW connection");
        c.Check(pinRelease<30,prefix+"pin-release","Pin release including authoritative RAW commit: "+std::to_string(pinRelease)+" ms");
    } else {io.AddMousePosEvent(-100,-100);frame();io.AddMouseButtonEvent(0,false);frame();}
    // Exercise collision displacement on the production ordinary/RAW canvas,
    // including a sibling frame, finite retargeting, cancellation and ownership.
    const auto effectUuid=editor.GetNodeGraph().FindNode(f.effect)->instanceUuid;
    const auto idFor=[&](const std::string& uuid){for(const auto& n:editor.GetNodeGraph().GetNodes())if(n.instanceUuid==uuid)return n.id;return -1;};
    const auto serialized=[&]{
        if(raw)return Stack::Project::SerializeRawLayerStack(editor.GetProjectSession().rawLayers.State());
        auto authored=SerializeGraphPayload(nlohmann::json::array(),editor.GetNodeGraph());
        // Selection is presentation state. RAW projection IDs are also transient;
        // compare its owned document above rather than the rebuilt canvas IDs.
        authored["nodeGraph"].erase("selectedNodeId");return authored;
    };
    std::string isolateError;
    c.Check(Stack::Editor::ApplyGraphCommand(editor,[&](auto& g){g.FindNode(f.effect)->position.y+=600;},&isolateError),
        prefix+"push-isolate",isolateError);
    for(int settle=0;settle<20;++settle)frame();
    const auto primaryPosition=editor.GetNodeGraph().FindNode(f.effect)->position;
    const auto primaryBounds=ui.MeasureNodeForLayout(editor.GetNodeGraph(),*editor.GetNodeGraph().FindNode(f.effect));
    editor.AddValueNodeAt(MakeUniformScalar(1),{primaryPosition.x+primaryBounds.max.x+90,primaryPosition.y});
    const int neighbor=editor.GetNodeGraph().GetSelectedNodeId();
    frame();frame();
    const auto neighborLocal=ui.MeasureNodeForLayout(editor.GetNodeGraph(),*editor.GetNodeGraph().FindNode(neighbor));
    const auto neighborPosition=editor.GetNodeGraph().FindNode(neighbor)->position;
    int siblingGroup=0;std::string placementError;
    c.Check(Stack::Editor::ApplyGraphCommand(editor,[&](auto& g){siblingGroup=g.AddGroup("Drag neighbor",
        {neighborPosition.x+neighborLocal.min.x-20,neighborPosition.y+neighborLocal.min.y-40},
        {neighborLocal.max.x-neighborLocal.min.x+40,neighborLocal.max.y-neighborLocal.min.y+60})->id;},&placementError),
        prefix+"push-setup",placementError);
    Probe::WatchNode(f.effect);editor.GetNodeGraph().SelectNode(f.effect);frame();frame();
    const auto pushBefore=serialized();
    const auto groupStart=editor.GetNodeGraph().FindGroup(siblingGroup)->position;
    const auto pushRender=editor.GetRenderRevision(),pushProcessing=editor.GetProjectSession().rawLayers.ProcessingRevision();
    auto pushRegion=Probe::Last(Probe::Visual::Node).region;
    const float pushX=pushRegion.x+pushRegion.width*.5f,pushY=pushRegion.y+12;
    for(int i=0;i<20;++i)frame();
    io.AddMousePosEvent(pushX,pushY);frame();io.AddMouseButtonEvent(0,true);frame();
    c.Check(Perf::LastFrame().counters[std::size_t(Perf::Counter::LayoutPrepare)]==1,prefix+"push-prepare","Geometry is prepared once on the pointer press");
    bool quietHold=true;
    for(int i=0;i<5;++i){frame();quietHold &= Perf::LastFrame().counters[std::size_t(Perf::Counter::LayoutPrepare)]==0 &&
        Perf::LastFrame().counters[std::size_t(Perf::Counter::LayoutSolve)]==0 && serialized()==pushBefore;}
    c.Check(quietHold,prefix+"push-stationary","Holding the pointer still performs no preparation, solve or authored edit");
    double pushTotal=0,pushPeak=0;std::uint64_t pushSolves=0,pushPrepares=0;
    for(int i=1;i<=8;++i){io.AddMousePosEvent(pushX+14*i,pushY);const double ms=frame();pushTotal+=ms;pushPeak=std::max(pushPeak,ms);
        pushSolves+=Perf::LastFrame().counters[std::size_t(Perf::Counter::LayoutSolve)];pushPrepares+=Perf::LastFrame().counters[std::size_t(Perf::Counter::LayoutPrepare)];}
    const auto pushed=editor.GetNodeGraph().FindNode(neighbor)->position;
    const auto pushedGroup=editor.GetNodeGraph().FindGroup(siblingGroup)->position;
    const auto heldPosition=editor.GetNodeGraph().FindNode(f.effect)->position;
    const float dragZoom=ui.GetGraphCaptureViewportSnapshot().zoom;
    c.Check(std::abs((heldPosition.x-primaryPosition.x)-112/dragZoom)<.01f &&
        (pushed.x!=neighborPosition.x || pushed.y!=neighborPosition.y) &&
        std::abs((pushed.x-neighborPosition.x)-(pushedGroup.x-groupStart.x))<.01f &&
        std::abs((pushed.y-neighborPosition.y)-(pushedGroup.y-groupStart.y))<.01f,
        prefix+"push-motion","Held node follows the pointer; the neighboring node/frame move rigidly out of its way");
    c.Check(pushSolves==8 && pushPrepares==0 && pushPeak<30,prefix+"push-cost","8 changed inputs: mean="+std::to_string(pushTotal/8)+
        " ms, peak="+std::to_string(pushPeak)+" ms; solves="+std::to_string(pushSolves)+", preparations="+std::to_string(pushPrepares));
    c.Check(ui.HasLayoutMotion() || Stack::Notifications::SystemReducedMotion(),prefix+"push-animation","Displaced nodes and frames have finite presentation motion");
    Probe::WatchNode(neighbor);frame();
    const auto displayed=Probe::Last(Probe::Visual::Node).region;
    const auto camera=Probe::Last(Probe::Visual::Camera).region;
    const auto animatedView=ui.GetGraphCaptureViewportSnapshot();
    const float displayedX=(displayed.x-camera.x-animatedView.panX)/animatedView.zoom;
    const float displayedY=(displayed.y-camera.y-animatedView.panY)/animatedView.zoom;
    c.Check(Stack::Notifications::SystemReducedMotion() || std::hypot(displayedX-pushed.x,displayedY-pushed.y)>.01f,
        prefix+"push-tween","Actual native geometry interpolates toward the displaced target rather than snapping");
    io.AddMousePosEvent(pushX,pushY);frame();
    const auto returned=editor.GetNodeGraph().FindNode(neighbor)->position;
    const auto returnedFrame=editor.GetNodeGraph().FindGroup(siblingGroup)->position;
    c.Check(ImGui::IsMouseDown(0) && std::hypot(returned.x-neighborPosition.x,returned.y-neighborPosition.y)<.01f &&
        std::hypot(returnedFrame.x-groupStart.x,returnedFrame.y-groupStart.y)<.01f &&
        Perf::LastFrame().counters[std::size_t(Perf::Counter::LayoutSolve)]==1,
        prefix+"push-return-live","Vacated neighbors and their frame return to rest while the pointer is still held, including return to the exact press position");
    io.AddMousePosEvent(pushX+112,pushY);frame();
    io.AddMousePosEvent(pushX+126,pushY);io.AddMouseButtonEvent(0,false);frame();
    c.Check(std::abs((editor.GetNodeGraph().FindNode(f.effect)->position.x-primaryPosition.x)-126/dragZoom)<.01f &&
        Perf::LastFrame().counters[std::size_t(Perf::Counter::LayoutSolve)]==1,prefix+"push-final-release",
        "A final pointer update delivered with mouse-up is preserved inside the same gesture");
    const auto pushAfter=serialized();
    bool noIdleSolve=true;for(int i=0;i<22;++i){frame();noIdleSolve &= Perf::LastFrame().counters[std::size_t(Perf::Counter::LayoutSolve)]==0;}
    c.Check(noIdleSolve && !ui.HasLayoutMotion() && serialized()==pushAfter && editor.GetRenderRevision()==pushRender &&
        (!raw || editor.GetProjectSession().rawLayers.ProcessingRevision()==pushProcessing),prefix+"push-settle",
        "Release settles exactly with no further solve, authored edit or image-processing invalidation");
    Probe::WatchNode(f.effect);frame();
    auto nextPress=Probe::Last(Probe::Visual::Node).region;
    io.AddMousePosEvent(nextPress.x+nextPress.width*.5f,nextPress.y+12);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(nextPress.x+nextPress.width*.5f-126,nextPress.y+12);frame();
    const auto nextGestureRest=editor.GetNodeGraph().FindNode(neighbor)->position;
    c.Check(ImGui::IsMouseDown(0) && std::hypot(nextGestureRest.x-neighborPosition.x,nextGestureRest.y-neighborPosition.y)<.01f,
        prefix+"push-rest-next-gesture","Automatic resting positions survive release and are reclaimed during the next live drag; actual="+
        std::to_string(nextGestureRest.x)+","+std::to_string(nextGestureRest.y)+"; rest="+std::to_string(neighborPosition.x)+","+
        std::to_string(neighborPosition.y)+"; press="+std::to_string(nextPress.x)+","+std::to_string(nextPress.y)+"; solve="+
        std::to_string(Perf::LastFrame().counters[std::size_t(Perf::Counter::LayoutSolve)]));
    io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);io.AddMouseButtonEvent(0,false);frame();
    for(int i=0;i<20;++i)frame();
    c.Check(serialized()==pushAfter,prefix+"push-rest-cancel","Cancelling the second drag restores the preceding settled layout and its history");
    const bool pushUndo=raw?editor.UndoRawLayerEdit():editor.UndoProjectGraphEdit();const auto pushUndone=serialized();
    frame();
    c.Check(ui.HasLayoutMotion() || Stack::Notifications::SystemReducedMotion(),prefix+"push-undo-animation",
        "Undo presents restored placements with finite motion without running a layout solve");
    for(int i=0;i<20;++i)frame();
    const bool pushRedo=raw?editor.RedoRawLayerEdit():editor.RedoProjectGraphEdit();
    c.Check(pushUndo && pushRedo && pushUndone==pushBefore && serialized()==pushAfter,prefix+"push-history",
        "One undo/redo restores the held node, every displaced neighbor and group frame; undo="+std::to_string(pushUndo)+", redo="+std::to_string(pushRedo)+
        "; undo diff="+nlohmann::json::diff(pushBefore,pushUndone).dump().substr(0,1500)+"; redo diff="+nlohmann::json::diff(pushAfter,serialized()).dump().substr(0,1500));
    f.effect=idFor(effectUuid);Probe::WatchNode(f.effect);editor.GetNodeGraph().SelectNode(f.effect);frame();frame();
    c.Check(ui.HasLayoutMotion() || Stack::Notifications::SystemReducedMotion(),prefix+"push-redo-animation",
        "Redo animates the displacement again using stable authored identities");
    for(int i=0;i<20;++i)frame();
    const auto cancelBefore=serialized();pushRegion=Probe::Last(Probe::Visual::Node).region;
    io.AddMousePosEvent(pushRegion.x+pushRegion.width*.5f,pushRegion.y+12);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(pushRegion.x+pushRegion.width*.5f+50,pushRegion.y+12);frame();
    io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);io.AddMouseButtonEvent(0,false);frame();
    c.Check(serialized()==cancelBefore,prefix+"push-cancel","Escape restores every gesture placement through its original owner");
    for(int i=0;i<22;++i)frame();
    const bool cancelUndo=raw?editor.UndoRawLayerEdit():editor.UndoProjectGraphEdit();const auto afterCancelUndo=serialized();
    const bool cancelRedo=raw?editor.RedoRawLayerEdit():editor.RedoProjectGraphEdit();
    c.Check(cancelUndo && cancelRedo && afterCancelUndo==pushBefore && serialized()==pushAfter,prefix+"push-cancel-history",
        "A cancelled drag adds no undo entry and its return animation settles; undo diff="+nlohmann::json::diff(pushBefore,afterCancelUndo).dump().substr(0,1500)+
        "; redo diff="+nlohmann::json::diff(pushAfter,serialized()).dump().substr(0,1500));
    f.effect=idFor(effectUuid);Probe::WatchNode(f.effect);editor.GetNodeGraph().SelectNode(f.effect);frame();frame();
    for(int i=0;i<20;++i)frame();pushRegion=Probe::Last(Probe::Visual::Node).region;
    io.AddMousePosEvent(pushRegion.x+pushRegion.width*.5f,pushRegion.y+12);frame();io.AddMouseButtonEvent(0,true);frame();
    io.AddMousePosEvent(pushRegion.x+pushRegion.width*.5f+30,pushRegion.y+12);frame();
    const auto originalFrozen=serialized();EditorModule other;other.SetDocumentPersistenceEnabled(false);other.GetNodeGraph()=f.graph.CloneForAnalysis();
    const auto otherBefore=SerializeGraphPayload(nlohmann::json::array(),other.GetNodeGraph());canvasEditor=&other;
    io.AddMousePosEvent(pushRegion.x+pushRegion.width*.5f+100,pushRegion.y+12);frame();
    c.Check(serialized()==originalFrozen && SerializeGraphPayload(nlohmann::json::array(),other.GetNodeGraph())==otherBefore &&
        Perf::LastFrame().counters[std::size_t(Perf::Counter::LayoutSolve)]==0,prefix+"push-owner",
        "Switching the rendered document drops old gesture state without moving either owner's nodes");
    io.AddMouseButtonEvent(0,false);frame();canvasEditor=&editor;frame();
    f.effect=idFor(effectUuid);Probe::WatchNode(f.effect);
    io.AddMousePosEvent(-100,-100);io.AddMouseButtonEvent(0,false);frame();
    // A workspace retains its own canvas while another workspace is drawn.
    // Advance ImGui input without drawing this canvas, then return promptly.
    for (bool newPress : {false, true}) {
        for(int settle=0;settle<20;++settle)frame();
        const auto pressRegion=Probe::Last(Probe::Visual::Node).region;
        const float x=pressRegion.x+pressRegion.width*.5f, y=pressRegion.y+12;
        io.AddMousePosEvent(x,y);frame();io.AddMouseButtonEvent(0,true);frame();
        io.AddMousePosEvent(x+18,y);frame();
        const auto held=serialized();
        drawCanvas=false;
        io.AddMouseButtonEvent(0,false);frame();
        if(newPress) {io.AddMousePosEvent(1300,900);io.AddMouseButtonEvent(0,true);frame();}
        else frame(); // Consume the release edge while the owner is hidden.
        drawCanvas=true;
        io.AddMousePosEvent(1250,850);frame();
        c.Check(serialized()==held && Perf::LastFrame().counters[std::size_t(Perf::Counter::LayoutSolve)]==0,
            prefix+(newPress?"push-hidden-new-press":"push-hidden-release"),
            "Returning to a retained canvas never applies an abandoned node drag at the later pointer");
        io.AddMouseButtonEvent(0,false);frame();
    }
    const auto processingBeforeOpen=editor.GetProjectSession().rawLayers.ProcessingRevision();
    std::unordered_set<std::string> previousNodes;
    for(const auto& node:editor.GetNodeGraph().GetNodes())previousNodes.insert(node.instanceUuid);
    std::string layoutError;
    bool opened=false;
    canvasAction=[&]{opened=ui.DecompileNode(&editor,f.effect,&layoutError);
        ui.FitGraphPreviewToCanvas(&editor,editor.GetNodeGraph(),io.DisplaySize);};
    frame();
    c.Check(opened,prefix+"decompile",layoutError);
    const auto count=[&](Perf::Counter counter){return Perf::LastFrame().counters[std::size_t(counter)];};
    const bool reduced=Stack::Notifications::SystemReducedMotion();
    const auto roomVisible=count(Perf::Counter::VisibleNodes);
    c.Check(reduced || (count(Perf::Counter::LayoutOrigin)==1 && count(Perf::Counter::LayoutRevealingNodes)==0 &&
        count(Perf::Counter::LayoutTracingLinks)==0 && roomVisible<=previousNodes.size()-1),
        prefix+"decompile-room-first","The original native node is retained while space opens; internal nodes and their wires remain hidden");
    io.DeltaTime=.35f;frame();io.DeltaTime=1.f/60;
    c.Check(reduced || (ui.HasLayoutMotion() && count(Perf::Counter::LayoutOrigin)==1 &&
        count(Perf::Counter::LayoutRevealingNodes)==0 && count(Perf::Counter::LayoutTracingLinks)==0),
        prefix+"decompile-slow-first-frame","A 350 ms first-frame gap cannot discard or skip the room phase");
    std::unordered_map<int,GraphRect> planned;
    for(const auto& node:editor.GetNodeGraph().GetNodes())planned[node.id]=ui.MeasureNodeForLayout(editor.GetNodeGraph(),node);
    const auto targets=SerializeGraphPayload(nlohmann::json::array(),editor.GetNodeGraph());
    const auto renderAfterOpen=editor.GetRenderRevision();
    const auto processingAfterOpen=editor.GetProjectSession().rawLayers.ProcessingRevision();
    c.Check(ui.HasLayoutMotion() || Stack::Notifications::SystemReducedMotion(),prefix+"motion-start",
        "Displaced neighbors have a finite presentation transition, or immediate reduced-motion placement");
    frame();
    c.Check(ui.HasLayoutMotion() || Stack::Notifications::SystemReducedMotion(),prefix+"motion-advance",
        "The transition survives its first real canvas frame with the same owner and revision");
    double layoutTotal=0,layoutPeak=0;
    bool sawReveal=false,sawTrace=false,roomBeforeReveal=true,traceAfterReveal=true;
    for(int i=0;i<55;++i){const auto ms=frame();layoutTotal+=ms;layoutPeak=std::max(layoutPeak,ms);
        const bool reveal=count(Perf::Counter::LayoutRevealingNodes)>0,trace=count(Perf::Counter::LayoutTracingLinks)>0;
        if(reveal){sawReveal=true;roomBeforeReveal &= i>=7 && !trace;}
        if(trace){sawTrace=true;traceAfterReveal &= sawReveal && !reveal;}
    }
    c.Check(reduced || (sawReveal && sawTrace && roomBeforeReveal && traceAfterReveal),prefix+"decompile-phases",
        "Actual native draw submissions show room movement, internal reveal, then partial wire tracing in order");
    c.Check(!ui.HasLayoutMotion() && SerializeGraphPayload(nlohmann::json::array(),editor.GetNodeGraph())==targets &&
        editor.GetRenderRevision()==renderAfterOpen && (!raw || editor.GetProjectSession().rawLayers.ProcessingRevision()==processingAfterOpen),
        prefix+"motion-settled","Animation ends exactly, without authoring positions or invalidating pixels; 55 frames mean="+
        std::to_string(layoutTotal/55)+" ms, peak="+std::to_string(layoutPeak)+" ms");
    frame();frame();
    c.Check(Perf::LastFrame().counters[std::size_t(Perf::Counter::LogicalRebuild)]==0 && !ui.HasLayoutMotion(),
        prefix+"layout-idle","Settled canvas reuses logical geometry and has no active layout motion");
    ui.FitGraphPreviewToCanvas(&editor,editor.GetNodeGraph(),io.DisplaySize);frame();frame();frame();
    std::vector<GraphRect> actual;std::vector<bool> newlyExposed;std::vector<int> actualIds;
    for(const auto& node:editor.GetNodeGraph().GetNodes()) {
        auto r=ui.MeasureNodeForLayout(editor.GetNodeGraph(),node);
        r.min.x+=node.position.x;r.min.y+=node.position.y;r.max.x+=node.position.x;r.max.y+=node.position.y;actual.push_back(r);
        newlyExposed.push_back(!previousNodes.count(node.instanceUuid));
        actualIds.push_back(node.id);
    }
    bool nativeClear=true;for(std::size_t i=0;i<actual.size();++i)for(std::size_t j=i+1;j<actual.size();++j)
        if(newlyExposed[i] || newlyExposed[j])nativeClear &= !(actual[i].min.x<actual[j].max.x && actual[i].max.x>actual[j].min.x && actual[i].min.y<actual[j].max.y && actual[i].max.y>actual[j].min.y);
    std::string conflicts;
    for(std::size_t i=0;i<actual.size() && conflicts.size()<1600;++i)for(std::size_t j=i+1;j<actual.size() && conflicts.size()<1600;++j)
        if((newlyExposed[i] || newlyExposed[j]) && actual[i].min.x<actual[j].max.x && actual[i].max.x>actual[j].min.x &&
            actual[i].min.y<actual[j].max.y && actual[i].max.y>actual[j].min.y) {
            for(auto index:{i,j}) {const auto* n=editor.GetNodeGraph().FindNode(actualIds[index]);const auto p=planned.at(n->id);
                conflicts+=" ["+std::to_string(n->id)+" "+n->title+" "+std::to_string(actual[index].min.x)+","+std::to_string(actual[index].min.y)+".."+
                    std::to_string(actual[index].max.x)+","+std::to_string(actual[index].max.y)+" planned="+std::to_string(p.max.y-p.min.y)+"]";
            }
        }
    c.Check(nativeClear,prefix+"native-footprints","Native controls measured after real drawing keep non-overlapping persistent footprints"+conflicts);
    if(raw)c.Check(processingAfterOpen>processingBeforeOpen,prefix+"decompile-processing",
        "Topology is committed once through the RAW owner; subsequent animation is presentation only");
    // A local, off-row serial connection uses the same transaction and native
    // footprint provider in both hosts. Keep the existing complex graph intact.
    std::string aUuid,bUuid,dUuid;int a=0,b=0,d=0;
    c.Check(Stack::Editor::ApplyGraphCommand(editor,[&](auto& g){
        a=g.AddValueNode(MakeUniformScalar(1),{-4000,-2400})->id;
        b=g.AddDataMathNode(DataMathMode::Multiply,{-3500,-2050})->id;
        d=g.AddDataMathNode(DataMathMode::Multiply,{-3000,-2400})->id;
        aUuid=g.FindNode(a)->instanceUuid;bUuid=g.FindNode(b)->instanceUuid;dUuid=g.FindNode(d)->instanceUuid;
        Wire(g,a,"valueOut",b,"imageA");
    },&layoutError),prefix+"connect-layout-setup",layoutError);
    frame();for(int i=0;i<20;++i)frame();
    a=idFor(aUuid);b=idFor(bUuid);d=idFor(dUuid);const auto connectionBefore=serialized();
    c.Check(editor.ConnectGraphSockets(b,"imageOut",d,"imageA",&layoutError,
        [&](const auto& g,const auto& n){return ui.MeasureNodeForLayout(g,n);}),prefix+"connect-layout",layoutError);
    a=idFor(aUuid);b=idFor(bUuid);d=idFor(dUuid);
    const auto pa=editor.GetNodeGraph().FindNode(a)->position,pb=editor.GetNodeGraph().FindNode(b)->position,pd=editor.GetNodeGraph().FindNode(d)->position;
    const auto connectionAfter=serialized();
    c.Check(pa.y==pb.y && pb.y==pd.y && pa.x<pb.x && pb.x<pd.x,prefix+"connect-alignment",
        "Connecting an off-row middle node aligns its unbranched chain immediately in authored layout");
    frame();c.Check(reduced || ui.HasLayoutMotion(),prefix+"connect-animation","Connection alignment animates from the displayed geometry");
    for(int i=0;i<20;++i)frame();
    const bool linkUndo=raw?editor.UndoRawLayerEdit():editor.UndoProjectGraphEdit();const auto connectionUndone=serialized();
    const bool linkRedo=raw?editor.RedoRawLayerEdit():editor.RedoProjectGraphEdit();
    c.Check(linkUndo && linkRedo && connectionUndone==connectionBefore && serialized()==connectionAfter,prefix+"connect-history",
        "One undo/redo restores both the new wire and its complete layout change");
    frame();for(int i=0;i<20;++i)frame();
    c.Check(!ui.HasLayoutMotion() && count(Perf::Counter::LayoutSolve)==0 && count(Perf::Counter::LogicalRebuild)==0,
        prefix+"connect-idle","Connection layout finishes with no idle solving or logical geometry rebuild");
    ui.ResetProjectInteractionState();
    c.Check(!ui.HasLayoutMotion(),prefix+"layout-reset","Project reset releases all layout tracks");
    ui.Shutdown();Probe::Finish();Perf::EnableFrameCapture(false);ImGui_ImplOpenGL3_Shutdown();
}

void CanvasInteraction(Checks& c) {CanvasInteractionCase(c,false);CanvasInteractionCase(c,true);}

void RawOutput(Checks& c) {
    using namespace Stack::Project;
    auto raw=MakeViewportValidationRaw(128,96);
    RenderGraphSnapshot source;
    auto developed=RenderNode(1,RenderGraphNodeKind::RawDevelopment);
    developed.rawDevelopment.embeddedRawData=raw;
    developed.rawDevelopment.recipe=Stack::RawRecipe::MakeDefaultRecipe(raw->metadata.sourcePath);
    developed.authoredDocumentId="raw-modularity";
    source.nodes={developed,RenderNode(2,RenderGraphNodeKind::Output)};
    source.links={{1,"imageOut",2,"imageIn"}};source.outputNodeId=2;source.outputSocketId="imageOut";
    RawLayerStackState state;
    const auto initialState=state;
    auto& graph=state.background.graph;
    const int end=FindRawRole(state.background,Stack::GraphModel::NodeRole::LayerResult)->id;
    const auto incoming=*graph.FindAnyInputLink(end,"imageIn");
    const int effect=AddCompound(graph,"halation");
    const std::string uuid=graph.FindNode(effect)->instanceUuid;
    GraphOutputContext context;
    for(const auto& node:graph.GetNodes())if(node.role==Stack::GraphModel::NodeRole::OriginalImage ||
        node.role==Stack::GraphModel::NodeRole::CurrentImage)
        context.sourceDescriptors[GraphOutputIdentity(node.id,"imageOut")]=RawLayerSceneDescriptor(Raw::RawWorkingSpace::LinearRec2020D65);
    {
        ScopedGraphOutputAnalysis semantics(graph,context);
        Wire(graph,incoming.fromNodeId,incoming.fromSocketId.c_str(),effect,"image-in");
        Wire(graph,effect,"image-out",end,"imageIn");
    }
    const auto lower=[&](const RawLayerStackState& authored,RenderGraphSnapshot input,GraphOutputDescriptions* descriptions=nullptr) {
        std::string error;
        if(!LowerRawLayerStack(input,authored,1,1,error,0,120,30,nullptr,descriptions))throw std::runtime_error(error);
        return input;
    };
    GraphOutputDescriptions descriptions;
    auto packed=lower(state,source,&descriptions);
    std::string error;
    const bool admitted=IsRawLayerOutputAvailable(packed,error);
    c.Check(admitted,"RAW.output.admitted",admitted?"Default RAW -> packed Halation -> End -> presentation is available":error+"; chain="+graph.GetOutputConnectionDiagnostic());
    const auto result=ResolveGraphSnapshotOutput(packed,{state.background.id,uuid,"image-out"});
    const auto mask=ResolveGraphSnapshotOutput(packed,{state.background.id,uuid,"source-mask"});
    c.Check(result && mask && result->nodeId>0 && mask->nodeId>0 && result->nodeId!=mask->nodeId,
        "RAW.output.aliases","RAW composition preserves distinct authored result and mask endpoint/port aliases");
    c.Check(descriptions.count("raw/"+state.background.id+"/"+GraphOutputIdentity(effect,"image-out")) &&
        descriptions.count("raw/"+state.background.id+"/"+GraphOutputIdentity(effect,"source-mask")) &&
        descriptions.count("raw/"+state.background.id+"/"+uuid+"/image-out") &&
        descriptions.count("raw/"+state.background.id+"/"+uuid+"/source-mask"),
        "RAW.output.descriptors","Public compound result and mask descriptions survive RAW composition");
    RenderPipeline pipeline;pipeline.Initialize();pipeline.SetPreviewMaxDimension(128);
    const auto render=[&](const RenderGraphSnapshot& snapshot) {
        pipeline.ExecuteGraph(snapshot);
        return Read(pipeline,pipeline.GetCanvasWidth(),pipeline.GetCanvasHeight());
    };
    const auto pixels=render(packed);
    c.Check(!pixels.empty() && std::all_of(pixels.begin(),pixels.end(),[](float value){return std::isfinite(value);}) &&
        std::any_of(pixels.begin(),pixels.end(),[](float value){return value>0 && value<.99f;}),
        "RAW.output.gpu","Mosaic RAW source through packed Halation publishes finite nonblank pixels; failure="+
            pipeline.GetLastGraphExecutionStats().lastSpecializedFailure+"; reduction="+pipeline.GetLastGraphExecutionStats().lastReductionFailure);
    if(result) {
        auto preview=packed;
        const bool inspected=ConfigureRawLayerOutputPreview(preview,result->nodeId,result->socketId,error) && !render(preview).empty();
        c.Check(inspected,
            "RAW.output.inspect",error.empty()?"Packed compound output inspection uses its lowered port and shared View transform":error);
    }
    auto expanded=state;
    std::vector<int> children;
    {
        ScopedGraphOutputAnalysis semantics(expanded.background.graph,context);
        if(!expanded.background.graph.UnpackCompoundNode(effect,&children,&error))throw std::runtime_error(error);
    }
    const auto& complete=expanded.background.graph.GetConnectedOutputNodeIds();
    c.Check(std::find(complete.begin(),complete.end(),end)!=complete.end(),
        "RAW.output.completeness","Decompiled uniform controls retain a complete RAW End");
    c.Check(Difference(pixels,render(lower(expanded,source)))<2e-5,"RAW.output.unpacked","One-level RAW decompile preserves the rendered pixels");
    int repacked=0;
    {
        ScopedGraphOutputAnalysis semantics(expanded.background.graph,context);
        if(!expanded.background.graph.RepackCompoundGroup(expanded.background.graph.GetGroups().back().id,&repacked,&error))throw std::runtime_error(error);
    }
    c.Check(Difference(pixels,render(lower(expanded,source)))<2e-5,"RAW.output.repacked","RAW repack preserves the rendered pixels");
    const auto neutralPixels=render(lower(initialState,source));
    for(bool beforeStart:{true,false})for(bool halation:{true,false}) {
        auto projection=BuildRawProjectGraph(initialState);
        auto& canvas=projection.graph;
        const auto start=std::find_if(canvas.GetNodes().begin(),canvas.GetNodes().end(),[](const auto& node) {
            return node.role==Stack::GraphModel::NodeRole::CurrentImage;
        })->id;
        const int target=beforeStart?start:projection.output;
        const auto edge=*canvas.FindAnyInputLink(target,"imageIn");
        const int inserted=halation?AddCompound(canvas,"halation"):
            canvas.AddDataMathNode(DataMathMode::Multiply,{600,200})->id;
        if(!halation)canvas.FindNode(inserted)->dataMathSettings.constantB=.5f;
        const auto insertedUuid=canvas.FindNode(inserted)->instanceUuid;
        Wire(canvas,edge.fromNodeId,edge.fromSocketId.c_str(),inserted,halation?"image-in":"imageA");
        Wire(canvas,inserted,halation?"image-out":"imageOut",target,"imageIn");
        RawLayerStackState routed;
        if(!ApplyRawProjectGraph(initialState,projection,canvas,Stack::GraphModel::LayerParameterRecords(canvas),
                kRawBackgroundId,routed,error))throw std::runtime_error(error);
        const auto compiled=lower(routed,source);
        const auto routedPixels=render(compiled);
        const auto label=std::string("RAW.route.")+(beforeStart?"before-start.":"after-end.")+(halation?"halation":"multiply");
        const bool imageAvailable=IsRawLayerOutputAvailable(compiled,error) && !routedPixels.empty() &&
            std::all_of(routedPixels.begin(),routedPixels.end(),[](float value){return std::isfinite(value);}) &&
            std::any_of(routedPixels.begin(),routedPixels.end(),[](float value){return value>0 && value<.99f;});
        c.Check(imageAvailable,label,"Image operations outside layer caps publish finite nonblank RAW GPU output: "+
            error+pipeline.GetLastGraphExecutionStats().lastSpecializedFailure);
        if(!halation)c.Check(!routedPixels.empty() && Difference(neutralPixels,routedPixels)>.001,
            label+".effect","A non-neutral Multiply changes actual pixels across the layer boundary");
        RawLayerStackState reopened;
        const auto saved=SerializeRawLayerStack(routed);
        c.Check(DeserializeRawLayerStack(saved,reopened,error) &&
            SerializeRawLayerStack(reopened)==saved && Difference(routedPixels,render(lower(reopened,source)))<2e-5,
            label+".reopen","Arbitrary image endpoints survive current-format persistence and GPU lowering");
        if(!beforeStart) {
            const auto resultPort=ResolveGraphSnapshotOutput(compiled,{kRawBackgroundId,insertedUuid,halation?"image-out":"imageOut"});
            const auto endStage=compiled.rawLayerStageNodeIds.at(std::string(kRawBackgroundId)+"/output");
            c.Check(resultPort && std::any_of(compiled.links.begin(),compiled.links.end(),[&](const auto& link) {
                return link.fromNodeId==endStage;
            }),label+".blended-end","A same-layer consumer uses the layer End's blended output");
        }
    }
    auto incompatible=source;
    incompatible.nodes.front().rawDevelopment.recipe.technical.workingSpace=Raw::RawWorkingSpace::LinearSrgbD65;
    const auto rejected=lower(state,std::move(incompatible));
    const bool unavailable=!IsRawLayerOutputAvailable(rejected,error);
    c.Check(unavailable && !rejected.semanticDiagnostics.empty() && !rejected.unavailableOutputs.empty(),
        "RAW.output.diagnostics","A mismatched source keeps its RAW lowering diagnostics and unavailable producer: "+error);
}

void CompactPorts(Checks& c) {
    for(const auto* recipe:{"halation","halation-srgb"}) {
        auto f=Build(recipe);const auto* node=f.graph.FindNode(f.effect);
        const auto visible=f.graph.GetSockets(*node,true),all=f.graph.GetSockets(*node,false);
        const auto primary=[](const auto& s) {return s.id=="image-in" || s.id=="image-out" || s.id=="mask-in";};
        c.Check(visible.size()==3 && std::all_of(visible.begin(),visible.end(),primary),"Ports.compact",
            std::string(recipe)+": only Image, Result and Mask are visible by default");
        c.Check(all.size()>visible.size() && std::all_of(all.begin(),all.end(),[&](const auto& s) {
            return primary(s) || s.visibilityTier==SocketVisibilityTier::Advanced;
        }),"Ports.advanced","Inspection and shared-value ports retain addressable advanced declarations");
        std::string error;std::vector<int> children;
        bool roundtrip=f.graph.UnpackCompoundNode(f.effect,&children,&error);
        int packed=-1;
        if(roundtrip)roundtrip=f.graph.RepackCompoundGroup(f.graph.GetGroups().back().id,&packed,&error);
        if(roundtrip) {
            const auto ports=f.graph.GetSockets(*f.graph.FindNode(packed),true);
            roundtrip=ports.size()==3 && std::all_of(ports.begin(),ports.end(),primary);
            c.Save(f.graph,(std::string(recipe)+"-compact-ports.json").c_str());
            const auto loaded=c.Load((std::string(recipe)+"-compact-ports.json").c_str());
            roundtrip &= loaded.GetSockets(*loaded.FindNode(packed),true).size()==3;
        }
        c.Check(roundtrip,"Ports.repack",error.empty()?"Advanced visibility survives one-level decompile, repack and disk reload":error);
    }
}

void LayoutTransactions(Checks& c) {
    using namespace Stack::Project;
    RawLayerStackDocument owner,other;
    auto state=owner.State();state.background.graph.AddGroup("Placement",{20,30},{300,200});
    std::string error;c.Check(owner.Apply(std::move(state),error),"Layout.setup",error);
    auto projection=BuildRawProjectGraph(owner.State());
    const auto before=SerializeRawLayerStack(owner.State()),otherBefore=SerializeRawLayerStack(other.State());
    const auto processing=owner.ProcessingRevision();
    auto edited=projection.graph.CloneForAnalysis();
    auto& group=edited.GetGroups().front();group.position.x+=17;group.size.y+=11;
    auto node=std::find_if(edited.GetNodes().begin(),edited.GetNodes().end(),[](const auto& n){return !n.rawLayerOwnerId.empty();});
    node->position.x+=29;
    edited.FindNode(projection.preparedSource)->position.x+=13;
    edited.FindNode(projection.output)->position.y+=27;
    c.Check(owner.ApplyGraphLayout(projection,edited,true,error) && owner.ProcessingRevision()==processing,
        "Layout.gesture","Node and group placement uses one non-processing gesture");
    RawLayerStackState savedAnchors;
    const bool anchorsLoaded=DeserializeRawLayerStack(SerializeRawLayerStack(owner.State()),savedAnchors,error);
    const auto anchorProjection=BuildRawProjectGraph(savedAnchors);
    c.Check(anchorsLoaded && anchorProjection.graph.FindNode(anchorProjection.preparedSource)->position.x==13 &&
        anchorProjection.graph.FindNode(anchorProjection.output)->position.y==27,"Layout.anchors",
        "Synthetic RAW source/output positions share placement history and survive serializer/projection reload");
    c.Check(SerializeRawLayerStack(other.State())==otherBefore,"Layout.owner","Another document retains its state and history");
    c.Check(owner.CancelGesture() && SerializeRawLayerStack(owner.State())==before && owner.ProcessingRevision()==processing,
        "Layout.cancel","Cancellation restores all placement without a processing edit");
    node->position.x=std::numeric_limits<float>::quiet_NaN();
    c.Check(!owner.ApplyGraphLayout(projection,edited,true,error) && SerializeRawLayerStack(owner.State())==before && !owner.GestureActive(),
        "Layout.reject","Non-finite placement rejects before changing document or history");
}

void ExpansionPlacement(Checks& c) {
    {
        namespace Motion=Stack::Editor::GraphLayoutMotionTiming;
        const double elapsed=Motion::Wires+Motion::Trace*.5;
        c.Check(Motion::TraceProgress(elapsed,0)>.49f && Motion::TraceProgress(elapsed,1)==0 &&
            Motion::TraceProgress(Motion::Total,1)==1,"Expansion.trace-order",
            "Wire tracing starts in left columns, advances continuously and reaches the rightmost ports exactly");
    }
    {
        Graph geometry;geometry.Clear();
        const int held=geometry.AddValueNode(MakeUniformScalar(0),{0,0})->id;
        const int next=geometry.AddValueNode(MakeUniformScalar(1),{180,0})->id;
        const int last=geometry.AddValueNode(MakeUniformScalar(2),{360,0})->id;
        const int far=geometry.AddValueNode(MakeUniformScalar(3),{-6000,6000})->id;
        const auto baseline=SerializeGraphPayload(nlohmann::json::array(),geometry);
        const auto small=[](const Graph&,const Node&){return GraphRect{{0,0},{120,80}};};
        Stack::GraphLayout::RoomGesture room;Stack::GraphLayout::PlacementEdit edit;std::string reason;
        bool moved=room.Begin(geometry,{held},small,reason) && room.Move({120,0},edit,reason) && Stack::GraphLayout::ApplyPlacement(geometry,edit,reason);
        c.Check(moved && geometry.FindNode(held)->position.x==120 && geometry.FindNode(next)->position.x>=276 &&
            geometry.FindNode(last)->position.x>=432 && geometry.FindNode(far)->position.x==-6000,
            "Room.propagation","A pointer move propagates through a chain without moving a distant node: "+reason);
        const auto valid=SerializeGraphPayload(nlohmann::json::array(),geometry);
        c.Check(!room.Move({30000,0},edit,reason) && SerializeGraphPayload(nlohmann::json::array(),geometry)==valid &&
            room.Move({120,0},edit,reason) && edit.Empty(),"Room.reject","Rejected input leaves both the graph and last valid gesture state intact");
        c.Check(room.Move({},edit,reason) && Stack::GraphLayout::ApplyPlacement(geometry,edit,reason) &&
            SerializeGraphPayload(nlohmann::json::array(),geometry)==baseline,"Room.recenter",
            "Reversing a live pointer displacement restores the chain's resting geometry exactly");
        c.Check(room.Move({120,0},edit,reason) && Stack::GraphLayout::ApplyPlacement(geometry,edit,reason),"Room.repush",reason);
        const auto restored=room.Restore();
        c.Check(Stack::GraphLayout::ApplyPlacement(geometry,restored,reason) && SerializeGraphPayload(nlohmann::json::array(),geometry)==baseline,
            "Room.restore","The compact placement snapshot restores every propagated displacement exactly");
        geometry.FindNode(next)->position={300,0};geometry.FindNode(last)->position={600,0};
        c.Check(room.Begin(geometry,{held,last},small,reason) && room.Move({40,0},edit,reason) && Stack::GraphLayout::ApplyPlacement(geometry,edit,reason) &&
            geometry.FindNode(held)->position.x==40 && geometry.FindNode(last)->position.x==640 && geometry.FindNode(next)->position.x==300,
            "Room.selection","A separated multi-selection stays rigid without reserving its empty middle space");
    }
    {
        Graph g;g.Clear();const int held=g.AddValueNode(MakeUniformScalar(0),{})->id;
        const int a=g.AddValueNode(MakeUniformScalar(1),{500,0})->id,b=g.AddValueNode(MakeUniformScalar(2),{700,0})->id;
        g.AddGroup("Rigid after manual editing",{480,-30},{360,140});
        Stack::GraphLayout::RoomGesture room;Stack::GraphLayout::PlacementEdit edit;std::string error;
        const auto small=[](const Graph&,const Node&){return GraphRect{{0,0},{120,80}};};
        c.Check(room.Begin(g,{held},small,error,{},{},false,{{a,{300,0}}}) && room.Move({40,0},edit,error) &&
            Stack::GraphLayout::ApplyPlacement(g,edit,error) && g.FindNode(a)->position.x==500 && g.FindNode(b)->position.x==700,
            "Room.rigid-rest","An obsolete per-member rest preference cannot deform an unheld sibling group");
    }
    {
        Graph g;g.Clear();const int source=g.AddValueNode(MakeUniformScalar(1),{})->id;
        const int mid=g.AddDataMathNode(DataMathMode::Multiply,{450,200})->id;
        const int end=g.AddDataMathNode(DataMathMode::Multiply,{1000,0})->id;
        const int side=g.AddDataMathNode(DataMathMode::Multiply,{500,-400})->id;
        Wire(g,source,"valueOut",mid,"imageA");Wire(g,source,"valueOut",side,"imageA");Wire(g,mid,"imageOut",end,"imageA");
        std::string error;const auto small=[](const Graph&,const Node&){return GraphRect{{0,0},{120,80}};};
        c.Check(Stack::GraphLayout::ArrangeConnection(g,mid,end,small,error) && g.FindNode(source)->position.x==0 &&
            g.FindNode(source)->position.y==0 && g.FindNode(mid)->position.y==0 && g.FindNode(end)->position.y==0 &&
            g.FindNode(side)->position.x==500 && g.FindNode(side)->position.y==-400,"Connection.junction",
            "A shared source anchors the new serial chain without flattening its separate branch");
    }
    auto f=Build("halation-srgb");
    EditorModule editor;editor.SetDocumentPersistenceEnabled(false);editor.GetNodeGraph()=f.graph.CloneForAnalysis();
    auto& initial=editor.GetNodeGraph();
    const int neighbor=initial.AddValueNode(MakeUniformScalar(1),{650,130})->id;
    const int far=initial.AddValueNode(MakeUniformScalar(2),{-8000,7000})->id;
    const int sibling=initial.AddGroup("Keep together",{620,80},{400,320})->id;
    const auto siblingStart=initial.FindGroup(sibling)->position,neighborStart=initial.FindNode(neighbor)->position;
    const auto saved=SerializeGraphPayload(nlohmann::json::array(),initial);
    const auto measure=[](const Graph&,const Node& node) {return GraphRect{{-14,-2},
        {node.kind==NodeKind::Compound?334.f:270.f,node.kind==NodeKind::Compound?310.f:float(100+(node.id%3)*53)}};};
    const auto rect=[&](const Node& node){auto r=measure(editor.GetNodeGraph(),node);r.min.x+=node.position.x;r.max.x+=node.position.x;r.min.y+=node.position.y;r.max.y+=node.position.y;return r;};
    const auto overlap=[](GraphRect a,GraphRect b){return a.min.x<b.max.x && a.max.x>b.min.x && a.min.y<b.max.y && a.max.y>b.min.y;};
    std::string error;const auto start=std::chrono::steady_clock::now();
    c.Check(editor.UnpackCompoundNode(f.effect,&error,measure),"Expansion.open",error);
    const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    auto& graph=editor.GetNodeGraph();
    int root=-1;for(const auto& g:graph.GetGroups())if(g.compoundBoundary.is_object())root=g.id;
    const auto* boundary=graph.FindGroup(root);
    if(!boundary){c.Check(false,"Expansion.boundary","Retained boundary missing");return;}
    const auto children=boundary->compoundMembers;
    bool clean=true,forward=true,contained=true;
    const GraphRect frame{boundary->position,{boundary->position.x+boundary->size.x,boundary->position.y+boundary->size.y}};
    for(int id:children) {
        const auto a=rect(*graph.FindNode(id));contained &= a.min.x>=frame.min.x && a.min.y>=frame.min.y+28 && a.max.x<=frame.max.x && a.max.y<=frame.max.y;
        for(int other:children)if(other>id)clean &= !overlap(a,rect(*graph.FindNode(other)));
    }
    for(const auto& link:graph.GetLinks())if(std::find(children.begin(),children.end(),link.fromNodeId)!=children.end() &&
        std::find(children.begin(),children.end(),link.toNodeId)!=children.end())
        forward &= rect(*graph.FindNode(link.fromNodeId)).max.x<rect(*graph.FindNode(link.toNodeId)).min.x;
    c.Check(clean && forward && contained,"Expansion.geometry","Variable footprints, forward internal wires and measured frame containment; command="+std::to_string(elapsed)+" ms");
    bool outside=true;for(const auto& n:graph.GetNodes())if(std::find(children.begin(),children.end(),n.id)==children.end())outside &= !overlap(rect(n),frame);
    c.Check(outside && graph.FindNode(f.source)->position.x<frame.min.x && graph.FindNode(f.output)->position.x>frame.max.x,
        "Expansion.room","Upstream/downstream neighbors make room on their flow sides; all outside nodes clear the expansion");
    const auto siblingAfter=graph.FindGroup(sibling)->position,neighborAfter=graph.FindNode(neighbor)->position;
    c.Check(siblingAfter.x-siblingStart.x==neighborAfter.x-neighborStart.x && siblingAfter.y-siblingStart.y==neighborAfter.y-neighborStart.y &&
        (neighborAfter.x!=neighborStart.x || neighborAfter.y!=neighborStart.y) && graph.FindNode(far)->position.x==-8000 && graph.FindNode(far)->position.y==7000,
        "Expansion.rigid","Sibling frame/member travel together; an unrelated distant node stays exactly fixed");
    const auto expanded=SerializeGraphPayload(nlohmann::json::array(),graph);
    const bool undo=editor.UndoProjectGraphEdit();const auto undone=SerializeGraphPayload(nlohmann::json::array(),editor.GetNodeGraph());
    const bool redo=editor.RedoProjectGraphEdit();
    c.Check(undo && redo && undone==saved && SerializeGraphPayload(nlohmann::json::array(),editor.GetNodeGraph())==expanded,
        "Expansion.history","One undo/redo restores topology, all displaced nodes and group rectangles exactly");
    int nested=-1;for(int id:children){const auto* n=graph.FindNode(id);if(n && n->kind==NodeKind::Compound){nested=id;break;}}
    c.Check(nested>0 && editor.UnpackCompoundNode(nested,&error,measure),"Expansion.nested",error);
    boundary=graph.FindGroup(root);bool framesFit=boundary!=nullptr;
    if(boundary)for(const auto& child:graph.GetGroups())if(child.id!=root && child.compoundBoundary.is_object())
        framesFit &= child.position.x>=boundary->position.x && child.position.y>=boundary->position.y+28 &&
            child.position.x+child.size.x<=boundary->position.x+boundary->size.x && child.position.y+child.size.y<=boundary->position.y+boundary->size.y;
    c.Check(framesFit,"Expansion.ancestors","Nested decompile grows containing frames to include the full child frame");
    c.Check(editor.RepackCompoundGroup(root,&error),"Expansion.repack",error);
    const auto* repacked=graph.FindNode(graph.GetSelectedNodeId());const int repackedId=repacked?repacked->id:-1;
    c.Check(repackedId>0 && editor.UnpackCompoundNode(repackedId,&error,measure),"Expansion.reopen",error);
    c.Check(Difference(Render(f,f.graph),Render(f,graph))<2e-5,"Expansion.pixels",
        "Layout, nested decompile, repack and reopen retain the same image pixels");
    EditorModule limited;limited.SetDocumentPersistenceEnabled(false);limited.GetNodeGraph()=f.graph.CloneForAnalysis();
    limited.GetNodeGraph().FindNode(f.effect)->position={19990,0};
    const auto beforeFailure=SerializeGraphPayload(nlohmann::json::array(),limited.GetNodeGraph());
    c.Check(!limited.UnpackCompoundNode(f.effect,&error,measure) && !error.empty() &&
        SerializeGraphPayload(nlohmann::json::array(),limited.GetNodeGraph())==beforeFailure,
        "Expansion.atomic","Coordinate-limit failure preserves the original graph without a partial decompile");
}

void DescriptionCaching(Checks& c) {
    using namespace Stack::Editor::NodeGraphUIVisuals;
    auto f=Build("halation");
    EditorModule owner;owner.GetNodeGraph()=f.graph.CloneForAnalysis();
    const auto hardError=[](const GraphOutputDescription* output) {
        return output && std::any_of(output->diagnostics.begin(),output->diagnostics.end(),[](const auto& d) {
            return d.severity==DiagnosticSeverity::HardError;
        });
    };
    const auto* first=owner.GetGraphOutputDescription(f.effect,"image-out");
    c.Check(hardError(first),"Freeze.basis","Default Rec.2020 Halation keeps a bounded error for an sRGB input");
    const auto start=std::chrono::steady_clock::now();
    bool stable=true;
    for(int i=0;i<1000;++i) {
        stable &= owner.GetGraphOutputDescription(f.effect,"image-out")==first;
        stable &= !ResolveOutputVisualStyle(owner.GetNodeGraph(),f.effect,"image-out",&owner).scalarStream;
        stable &= ResolveOutputVisualStyle(owner.GetNodeGraph(),f.effect,"source-mask",&owner).scalarStream;
        stable &= !ResolveOutputVisualStyle(owner.GetNodeGraph(),f.effect,"missing-port",&owner).scalarStream;
    }
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    c.Check(stable,"Freeze.reuse","1000 sets of actual UI output-style/cache queries retain current descriptions; "+std::to_string(elapsed)+" ms (CPU query cost only)");
    owner.GetNodeGraph().FindNode(f.source)->image.sourceColorMetadata.descriptor.color=
        SemanticField<ColorIdentity>::Known({"rec2020-d65",{},ColorRelation::Standard});
    owner.MarkRenderDirty(f.source);
    c.Check(!hardError(owner.GetGraphOutputDescription(f.effect,"image-out")),"Freeze.edit","Source semantic edits invalidate the owner cache");
    EditorModule other;other.GetNodeGraph()=f.graph.CloneForAnalysis();
    c.Check(hardError(other.GetGraphOutputDescription(f.effect,"image-out")) &&
        !hardError(owner.GetGraphOutputDescription(f.effect,"image-out")),"Freeze.owner","Two project owners keep independent descriptions");
    auto snapshot=owner.BuildGraphSnapshot();
    const auto* published=owner.GetGraphOutputDescription(f.effect,"source-mask");
    c.Check(!snapshot.nodes.empty() && published && published->descriptor.logicalType==LogicalValueType::Mask &&
        !hardError(owner.GetGraphOutputDescription(f.effect,"image-out")),"Freeze.alias","One lowering publishes correct authored compound-port descriptions");
    owner.GetNodeGraph().RemoveLink(f.source,"imageOut",f.effect,"image-in");
    c.Check(hardError(owner.GetGraphOutputDescription(f.effect,"image-out")),"Freeze.structure","Disconnecting the source invalidates descriptions through the structural revision");
}

void SharedValuesAndHistory(Checks& c) {
    auto f=Build("colorize",false,1,1);Fill(f,{.2f,.2f,.2f,1});
    const int driver=f.graph.AddValueNode(MakeUniformScalar(2),{})->id;
    f.graph.FindNode(driver)->value.value.payload=2.;
    Wire(f.graph,driver,"valueOut",f.effect,"param:r");Wire(f.graph,driver,"valueOut",f.effect,"param:g");Wire(f.graph,driver,"valueOut",f.effect,"param:b");
    auto compact=Render(f,f.graph);c.Check(compact.size()==4,"G09.render","One uniform drives all three gains");if(compact.size()==4)for(int i=0;i<3;++i)c.Near(compact[i],.4,"G09.compact");
    {
        RenderPipeline cached;cached.Initialize();cached.LoadSourceFromPixels(f.pixels.data(),1,1,4);
        glBindTexture(GL_TEXTURE_2D,cached.GetSourceTexture());glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,1,1,0,GL_RGBA,GL_FLOAT,f.linearPixels.data());
        const auto run=[&](const Graph& graph) {
            EditorModule owner;owner.GetNodeGraph()=graph.CloneForAnalysis();auto snapshot=owner.BuildGraphSnapshot();
            for(auto& node:snapshot.nodes)if(node.nodeId==f.source)node.image.pixels={};
            cached.ExecuteGraph(snapshot);return Read(cached,1,1);
        };
        auto first=run(f.graph),again=run(f.graph);
        c.Check(Difference(first,again)<1e-6,"G14.cache-repeat","Unchanged snapshots retain identical output");
        auto changed=f.graph.CloneForAnalysis();changed.FindNode(driver)->value.value.payload=3.;auto next=run(changed);
        c.Check(next.size()==4,"G09.cache-render","Changed shared control renders through persistent caches");if(next.size()==4)c.Near(next[0],.6,"G09.cache-invalidation");
    }
    c.Save(f.graph,"standalone-constant-driven-colorize.json");
    auto expanded=f.graph.CloneForAnalysis();std::string error;std::vector<int> children;
    const bool opened=expanded.UnpackCompoundNode(f.effect,&children,&error);
    c.Check(opened,"G09.expand",error);
    if(!opened || expanded.GetGroups().empty())return;
    expanded.FindNode(driver)->value.value.payload=3.;auto edited=Render(f,expanded);
    c.Check(edited.size()==4,"G09.edit-render","Shared source remains executable after expansion");if(edited.size()==4)for(int i=0;i<3;++i)c.Near(edited[i],.6,"G09.edit");
    const int groupId=expanded.GetGroups().back().id;
    int internal=-1;for(int id:children)if(expanded.FindNode(id)->kind==NodeKind::ChannelCombine)internal=id;
    const int inspection=expanded.AddPreviewNode({})->id;Wire(expanded,internal,"imageOut",inspection,"previewIn");
    c.Check(expanded.RepackCompoundGroup(groupId,nullptr,&error),"G11.repack",error);
    c.Check(expanded.FindAnyInputLink(inspection,"previewIn")!=nullptr&&Difference(edited,Render(f,expanded))<1e-6,"G11.wires","A new external inspection link survives repacking without another final Output");
    c.Save(expanded,"colorize-shared-repacked.json");auto restored=c.Load("colorize-shared-repacked.json");
    c.Check(Difference(edited,Render(f,restored))<1e-6,"G08.shared","Shared driver and customized boundary survive reload");
    EditorModule editor;editor.GetNodeGraph()=f.graph.CloneForAnalysis();
    editor.BeginProjectGraphHistory();
    const auto before=SerializeGraphPayload(nlohmann::json::array(),editor.GetNodeGraph());
    c.Check(editor.UnpackCompoundNode(f.effect,&error),"G07.decompile",error);
    c.Check(editor.UndoProjectGraphEdit(),"G07.undo","Undo decompile transaction");
    auto restoredGraph=SerializeGraphPayload(nlohmann::json::array(),editor.GetNodeGraph());
    auto authoredBefore=before;
    // History may populate a rebuildable PNG cache from the retained source
    // pixels. Compare authored state and the actual source pixels separately.
    for(auto* payload:{&authoredBefore,&restoredGraph})
        for(auto& node:(*payload)["nodeGraph"]["nodes"])node.erase("pngBytes");
    const auto restoredDifference=nlohmann::json::diff(authoredBefore,restoredGraph);
    const auto* restoredSource=editor.GetNodeGraph().FindNode(f.source);
    const bool sameSource=restoredSource && restoredSource->image.pixels.size()==f.pixels.size() &&
        std::equal(f.pixels.begin(),f.pixels.end(),restoredSource->image.pixels.begin());
    c.Check(restoredDifference.empty() && sameSource,"G07.restore","Undo restores authored graph and exact source pixels; derived PNG cache excluded; differences="+restoredDifference.dump());
    c.Check(editor.RedoProjectGraphEdit(),"G07.redo","Redo decompile transaction");
    const int retained=editor.GetNodeGraph().GetGroups().empty()?-1:editor.GetNodeGraph().GetGroups().back().id;
    c.Check(editor.RepackCompoundGroup(retained,&error),"G07.repack",error);
    c.Check(editor.UndoProjectGraphEdit()&&editor.RedoProjectGraphEdit(),"G07.repack-history","Undo and redo repack transaction");
}

void Lifecycle(Checks& c) {
    auto fixture=Build("halation-srgb");c.Save(fixture.graph,"halation-default.json");
    Set(fixture.graph,fixture.effect,"radius",.05);Set(fixture.graph,fixture.effect,"aura-radius",.12);
    std::string error;const auto original=Render(fixture,fixture.graph);
    c.Check(!original.empty(),"G01","Compact Halation executes through the actual snapshot and GPU renderer");c.Save(fixture.graph,"halation-impulse-example.json");
    SavePixels(c.folder/"halation-after-linear-srgb.pfm",original,fixture.width,fixture.height);
    auto expanded=fixture.graph.CloneForAnalysis();std::vector<int> children;
    c.Check(expanded.UnpackCompoundNode(fixture.effect,&children,&error),"G02",error.empty()?"Decompile Halation once":error);
    const int parentGroup=expanded.GetGroups().empty()?-1:expanded.GetGroups().back().id;
    int luminance=-1;int nested=0;
    for(int id:children){const auto* n=expanded.FindNode(id);if(n&&n->kind==NodeKind::Compound){++nested;if(n->compound.instance.definition.id=="stack:compound/luminance-srgb")luminance=id;}}
    c.Check(nested>=6 && luminance>0,"G02.nesting","Immediate Luminance, Percent and curve children remain compounds");
    c.Check(Difference(original,Render(fixture,expanded))<2e-5,"G05.partial","One-level expansion preserves rendered values");c.Save(expanded,"halation-expanded.json");
    if(luminance>0){std::vector<int> grandchildren;c.Check(expanded.UnpackCompoundNode(luminance,&grandchildren,&error),"G03",error);
        int percent=-1;for(int id:grandchildren){const auto* n=expanded.FindNode(id);if(n&&n->kind==NodeKind::Compound&&n->compound.instance.definition.id=="stack:compound/percent")percent=id;}
        c.Check(percent>0,"G03.percent","Luminance's Percent children remain compounds");
        if(percent>0){std::vector<int> leaves;c.Check(expanded.UnpackCompoundNode(percent,&leaves,&error),"G04",error);
            c.Check(std::all_of(leaves.begin(),leaves.end(),[&](int id){return expanded.FindNode(id)->kind==NodeKind::DataMath;}),"G04.leaves","Percent exposes real Divide and Multiply");}
        c.Save(expanded,"halation-three-level.json");}
    c.Check(Difference(original,Render(fixture,expanded))<2e-5,"G05.deep","Nested partial expansion preserves pixels");
    Graph fully;CompoundExpansionResult expansion;c.Check(fixture.graph.ExpandAllCompoundNodes(fully,&expansion),"G05.lower",expansion.error);
    c.Check(Difference(original,Render(fixture,fully))<2e-5,"G05.full","Fully expanded graph preserves pixels");
    int edit=-1;for(int id:children){auto* n=expanded.FindNode(id);if(n&&n->kind==NodeKind::DataMath&&n->dataMathSettings.domain==ArithmeticDomain::Values&&std::abs(n->dataMathSettings.constantA-1.8f)<.0001f){edit=id;n->dataMathSettings.constantA=3;break;}}
    c.Check(edit>0,"G06.edit","Edit the actual internal red-gain value");const auto changed=Render(fixture,expanded);
    c.Check(Difference(changed,original)>1e-7 && !changed.empty() && !original.empty(),"G06.changed","The edited gain changes actual rendered RGB");
    int packed=-1;c.Check(expanded.RepackCompoundGroup(parentGroup,&packed,&error),"G06.repack",error.empty()?"Repack current internals":error);
    c.Check(Difference(changed,Render(fixture,expanded))<2e-5,"G06.pixels","Repacking preserves edited pixels");c.Save(expanded,"halation-custom-repacked.json");
    Graph loaded;try{loaded=c.Load("halation-custom-repacked.json");
        c.Check(Difference(changed,Render(fixture,loaded))<2e-5,"G08","Customized compound save/load preserves pixels");}
    catch(const std::exception& e){c.Check(false,"G08",e.what());}
    auto off=fixture.graph.CloneForAnalysis();Set(off,fixture.effect,"strength",0);
    const auto offPixels=Render(fixture,off);bool same=offPixels.size()==fixture.pixels.size();
    for(std::size_t i=0;same&&i<offPixels.size();++i)same=std::abs(offPixels[i]-fixture.pixels[i]/255.f)<1e-6;
    c.Check(same,"H02","Strength zero returns original RGBA");SavePixels(c.folder/"halation-before-linear-srgb.pfm",offPixels,fixture.width,fixture.height);
    const auto neighbor=(std::size_t(fixture.height/2)*fixture.width+fixture.width/2+1)*4;
    c.Check(original.size()>neighbor && original[neighbor]>0,"M01","RGB extraction before Gaussian produces neighboring light");
    auto halo=fixture.graph.CloneForAnalysis();for(auto& link:halo.EditLinks())if(link.toNodeId==fixture.output)link.fromSocketId="halo";
    SavePixels(c.folder/"halation-contribution-linear-srgb.pfm",Render(fixture,halo),fixture.width,fixture.height);
}
}

bool ValidateGraphModularity(const std::string& outputFolder) {
    Checks checks;checks.folder=outputFolder;std::filesystem::create_directories(checks.folder);
    std::ofstream(checks.folder/"checks.jsonl",std::ios::trunc);
    if(!glfwInit())return false;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
    auto* window=glfwCreateWindow(64,64,"Graph modularity validation",nullptr,nullptr);
    if(!window){glfwTerminate();return false;}glfwMakeContextCurrent(window);
    bool loaded=LoadGLFunctions();
    ImGui::CreateContext();
    if(loaded)try {
        for(const auto& result:CheckGraphWireRouteCache())checks.Check(result.at("passed").get<bool>(),result.at("id"),result.at("detail"));
    } catch(const std::exception& e){checks.Check(false,"Wire.exception",e.what());}
    if(loaded)for(const auto& [name,run]:std::array<std::pair<const char*,void(*)(Checks&)>,12>{{
        {"numerics",Numerics},{"color-spatial",ColorAndSpatial},{"native-decomposition",NativeDecomposition},{"lifecycle",Lifecycle},{"shared-history",SharedValuesAndHistory},{"contracts",ContractsAndPersistence},{"descriptions",DescriptionCaching},{"ports",CompactPorts},{"layout",LayoutTransactions},{"expansion",ExpansionPlacement},{"canvas",CanvasInteraction},{"raw-output",RawOutput}}})
        try {run(checks);}catch(const std::exception& e){checks.Check(false,std::string(name)+".exception",e.what());}
    std::ofstream(checks.folder/"results.json")<<nlohmann::json{{"checks",checks.results},{"failures",checks.failures},{"gpu",loaded},{"tolerance","absolute 1e-6 + relative 1e-5; compound max 2e-5"}}.dump(2);
    ImGui::DestroyContext();glfwMakeContextCurrent(nullptr);glfwDestroyWindow(window);glfwTerminate();return loaded&&checks.failures==0;
}
}
