#include "Editor/NodeGraph/ChannelOutputs.h"
#include "Editor/NodeGraph/CompoundGroupMembership.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Project/RawProjectGraph.h"
#include "NodeMath/ChannelImageSemantics.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace EditorNodeGraph;
void Check(bool pass,const std::string& message) {
    if(!pass)throw std::runtime_error(message.empty()?"Graph operation rejected.":message);
    std::cout<<"PASS "<<(message.empty()?"Graph operation accepted":message)<<'\n';
}
void Wire(Graph& g,int from,const std::string& output,int to,const std::string& input) {
    std::string error;if(!g.TryConnectSockets(from,output,to,input,&error))throw std::runtime_error(
        std::to_string(from)+"/"+output+" -> "+std::to_string(to)+"/"+input+": "+error);
}
std::string Color(const Graph& g,int id,const std::string& port="imageOut") {
    return OutputChannelColor(DescribeGraphOutput(g,id,port));
}
}
void RunGraphChannelOutputsTests() {
    using namespace Stack::NodeMath;
    {
        Graph membership; membership.Clear(); membership.SetAllowNoOutput(true);
        const int a=membership.AddDataMathNode(DataMathMode::Multiply,{100,100})->id;
        const int b=membership.AddDataMathNode(DataMathMode::Multiply,{200,100})->id;
        auto* group=membership.AddGroup("Owned",{0,0},{400,300});
        const int groupId=group->id;
        group->compoundBoundary=nlohmann::json::object();group->compoundMembers={a};
        const std::unordered_map<int,GraphRect> frames{{groupId,{{0,0},{400,300}}}};
        const auto measure=[](const Graph&,const Node&){return GraphRect{{0,0},{50,40}};};
        Check(UpdateCompoundGroupMembership(membership,{b},frames,measure),"Dropped node joins container.");
        Wire(membership,a,"imageOut",b,"imageA");
        membership.FindNode(a)->position={700,100};
        UpdateCompoundGroupMembership(membership,{a},frames,measure);
        Check(membership.FindGroup(groupId)->compoundMembers.size()==2,"Right-connected member retains ownership.");
        membership.EditLinks().clear();
        Check(UpdateCompoundGroupMembership(membership,{a},frames,measure),"Right-disconnected member can leave container.");
        Check(membership.FindGroup(groupId)->compoundMembers==std::vector<int>{b},"Detach leaves remaining member intact.");
        Check(membership.FindGroup(groupId)->position.x==-20.f && membership.FindGroup(groupId)->size.x==302.f,
            "Container refits owned node bounds with entry allowance.");
        const int childId=membership.AddGroup("Child",{150,50},{150,150})->id;
        membership.FindGroup(childId)->compoundBoundary=nlohmann::json::object();
        membership.FindGroup(childId)->compoundMembers={b};
        const int added=membership.AddDataMathNode(DataMathMode::Add,{210,100})->id;
        const std::unordered_map<int,GraphRect> nestedFrames{{groupId,{{0,0},{400,300}}},{childId,{{150,50},{300,200}}}};
        const auto beforeRevision=membership.GetStructureRevision();
        Check(UpdateCompoundGroupMembership(membership,{added},nestedFrames,measure),"Nested admission updates ownership.");
        Check(membership.GetStructureRevision()!=beforeRevision,"Membership invalidates structural presentation caches.");
        for(int owner:{groupId,childId}) {
            const auto& members=membership.FindGroup(owner)->compoundMembers;
            Check(std::find(members.begin(),members.end(),added)!=members.end(),"Nested admission retains both child and ancestor.");
        }
        const auto fittedNested=MeasureCompoundGroupFrames(membership,measure);
        Check(fittedNested.at(groupId).min.x<fittedNested.at(childId).min.x &&
            fittedNested.at(groupId).max.x>fittedNested.at(childId).max.x,
            "Ancestor frame encloses nested child frame and its entry area.");
        const auto authoredFrame=membership.FindGroup(groupId)->position;
        const int admissionOnly=membership.AddDataMathNode(DataMathMode::Add,{210,100})->id;
        UpdateCompoundGroupMembership(membership,{admissionOnly},nestedFrames,measure,false);
        Check(membership.FindGroup(groupId)->position.x==authoredFrame.x && membership.FindGroup(groupId)->position.y==authoredFrame.y,
            "Command admission preserves measured authored layout.");
        const int protectedId=membership.AddDataMathNode(DataMathMode::Add,{210,100})->id;
        membership.FindNode(protectedId)->role=Stack::GraphModel::NodeRole::OriginalImage;
        Check(!UpdateCompoundGroupMembership(membership,{protectedId},nestedFrames,measure),"Protected source cannot join a retained container.");
        const int effect=membership.AddLayerNode(LayerType::GaussianBlur,0,{210,100})->id;
        membership.FindGroup(groupId)->compoundMembers.push_back(effect);
        membership.FindGroup(childId)->compoundMembers.push_back(effect);
        membership.RemoveLayerNode(0);
        for(int owner:{groupId,childId}) {
            const auto& members=membership.FindGroup(owner)->compoundMembers;
            Check(std::find(members.begin(),members.end(),effect)==members.end(),"Effect deletion removes nested ownership references.");
        }


    }
    Graph graph;graph.Clear();graph.SetAllowNoOutput(true);
    const int image=graph.AddImageGeneratorNode(ImageGeneratorKind::SolidColor,{})->id;
    const int split=graph.AddChannelSplitNode({300,0})->id;
    const int gain=graph.AddDataMathNode(DataMathMode::Multiply,{600,0})->id;
    graph.FindNode(gain)->dataMathSettings.domain=ArithmeticDomain::Channel;
    graph.FindNode(gain)->dataMathSettings.constantB=.7f;
    Wire(graph,image,"imageOut",split,"imageIn");Wire(graph,split,"r",gain,"imageA");
    Check(Color(graph,gain)=="r","Channel Math discarded a modified red channel's identity.");
    const int control=graph.AddValueNode(MakeUniformScalar(.5),{})->id;
    Wire(graph,control,"valueOut",gain,"imageB");
    Check(Color(graph,gain)=="r","A uniform numeric control discarded channel identity.");
    const int constant=graph.AddConstantChannelNode({})->id;
    graph.FindNode(constant)->constantChannelSettings.value=.5;
    Wire(graph,split,"r",constant,kMatchExtentInputSocketId);
    Wire(graph,constant,graph.DefaultOutputSocket(*graph.FindNode(constant)),gain,"imageB");
    Check(Color(graph,gain)=="r","A uniform raster coefficient discarded channel identity.");
    Wire(graph,control,"valueOut",gain,"imageB");
    const int threshold=graph.AddMaskUtilityNode(MaskUtilityKind::Threshold,{})->id;
    Wire(graph,gain,"imageOut",threshold,graph.DefaultInputSocket(*graph.FindNode(threshold)));
    Check(Color(graph,threshold,graph.DefaultOutputSocket(*graph.FindNode(threshold)))=="r","A unary channel threshold discarded identity.");
    const int mixed=graph.AddDataMathNode(DataMathMode::Add,{})->id;
    graph.FindNode(mixed)->dataMathSettings.domain=ArithmeticDomain::Channel;
    Wire(graph,gain,"imageOut",mixed,"imageA");Wire(graph,split,"g",mixed,"imageB");
    Check(Color(graph,mixed).empty(),"Mixed red/green fields retained a false channel color.");
    const auto folded=FoldChannelSplits(graph,{split});
    Check(folded.size()==1 && !graph.FindNode(split),"A used channel split did not fold.");
    Check(graph.FindNode(image)->channelOutputs.size()==2,"Fold created unused component outputs.");
    const auto red=ChannelOutputId("imageOut","r"),green=ChannelOutputId("imageOut","g");
    Check(graph.FindInputLink(gain,"imageA")->fromNodeId==image && graph.FindInputLink(gain,"imageA")->fromSocketId==red,
        "Fold did not reconnect the channel to its producer.");
    Check(Color(graph,image,red)=="r" && Color(graph,image,green)=="g" && Color(graph,gain)=="r" && Color(graph,mixed).empty(),
        "Direct ports changed channel identity propagation.");
    Check(graph.Validate().valid,"Folded graph failed validation.");
    auto independent=graph.CloneForAnalysis();
    const int other=independent.AddImageGeneratorNode(ImageGeneratorKind::SolidColor,{})->id;
    const int otherSplit=independent.AddChannelSplitNode({})->id;
    Wire(independent,other,"imageOut",otherSplit,"imageIn");Wire(independent,otherSplit,"r",mixed,"imageB");
    Check(Color(independent,mixed).empty(),"Mixing red channels from independent images retained false provenance.");
    Wire(independent,gain,"imageOut",mixed,"imageB");
    Check(Color(independent,mixed)=="r","Recombining modified branches of the same original channel lost identity.");
    Graph lowered;CompoundExpansionResult expansion;
    Check(graph.ExpandAllCompoundNodes(lowered,&expansion),expansion.error);
    int extractors=0;
    for(const auto& node:lowered.GetNodes()) {extractors+=node.kind==NodeKind::ChannelSplit;Check(node.channelOutputs.empty(),"Execution retained an authored selector.");}
    Check(extractors==1 && expansion.outputBindings.size()==2,"Components of one output did not share an extractor.");
    Graph saved;DeserializeGraphPayload(SerializeGraphPayload(nlohmann::json::array(),graph),saved,0);
    Check(saved.FindNode(image)->channelOutputs.size()==2 && Color(saved,gain)=="r","Channel outputs did not survive graph persistence.");
    Check(saved.CloneForAnalysis().FindNode(image)->channelOutputs.size()==2,"Analysis clone lost component outputs.");
    auto malformed=SerializeGraphPayload(nlohmann::json::array(),graph);
    malformed["nodeGraph"]["nodes"][0]["channelOutputs"][0]["component"]="";
    bool rejected=false;try {DeserializeGraphPayload(malformed,saved,0);}catch(...){rejected=true;}
    Check(rejected && saved.FindNode(image)->channelOutputs.size()==2,"Malformed selector replaced the current graph.");
    auto obsolete=SerializeGraphPayload(nlohmann::json::array(),graph);obsolete["nodeGraph"]["version"]=15;
    rejected=false;try {DeserializeGraphPayload(obsolete,saved,0);}catch(...){rejected=true;}
    Check(rejected,"The previous project graph format was silently accepted.");

    Graph rgb;rgb.Clear();rgb.SetAllowNoOutput(true);
    ImagePayload payload;payload.width=payload.height=1;payload.channels=payload.originalChannels=3;
    const int source=rgb.AddImageNode(std::move(payload),{})->id;
    const int alphaSplit=rgb.AddChannelSplitNode({})->id;
    rgb.FindNode(alphaSplit)->splitMissingAlphaIsOpaque=true;
    const int alphaUse=rgb.AddDataMathNode(DataMathMode::Multiply,{})->id;
    Wire(rgb,source,"imageOut",alphaSplit,"imageIn");Wire(rgb,alphaSplit,"a",alphaUse,"imageA");
    FoldChannelSplits(rgb,{alphaSplit});
    const auto alpha=DescribeGraphOutput(rgb,source,ChannelOutputId("imageOut","a",true));
    Check(alpha.descriptor.range.state==KnowledgeState::Known && alpha.descriptor.range.value.nominalMinimum==1 &&
        OutputChannelColor(alpha).empty(),"Folding lost the opaque missing-alpha policy or invented image provenance.");

    Graph compound;compound.Clear();compound.SetAllowNoOutput(true);
    std::string error;
    const auto& templates=EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
    const auto definition=std::find_if(templates.begin(),templates.end(),[](const auto& d){return d.identity.id=="stack:compound/add-then-multiply";});
    Check(definition!=templates.end(),"The test compound template is unavailable.");
    Check(compound.AddCompoundDefinition(*definition,&error),error);
    const int input=compound.AddImageGeneratorNode(ImageGeneratorKind::SolidColor,{})->id;
    const int packed=compound.AddCompoundNode(definition->identity,{300,0})->id;
    const int use=compound.AddDataMathNode(DataMathMode::Multiply,{700,0})->id;
    Wire(compound,input,"imageOut",packed,"image-in");
    const auto port=ExposeChannelOutput(compound,packed,"image-out","r",false,&error);
    Check(!port.empty(),error);Wire(compound,packed,port,use,"imageA");
    Check(Color(compound,use)=="r","A compound component output lost identity.");
    std::vector<int> children;
    Check(compound.UnpackCompoundNode(packed,&children,&error),error);
    Check(Color(compound,use)=="r","Decompile lost a direct compound channel output.");
    const int group=compound.GetGroups().back().id;
    int restored=0;Check(compound.RepackCompoundGroup(group,&restored,&error),error);
    Check(Color(compound,use)=="r" && compound.Validate().valid,"Repack lost a direct compound channel output.");

    // Ordinary effect nodes are members too: public RemoveNode protects these,
    // but packing must transfer every member, including both Gaussian lobes.
    Graph effects;effects.Clear();effects.SetAllowNoOutput(true);
    const int effectSource=effects.AddImageGeneratorNode(ImageGeneratorKind::SolidColor,{})->id;
    const int blurA=effects.AddLayerNode(LayerType::GaussianBlur,0,{200,0})->id;
    const int blurB=effects.AddLayerNode(LayerType::GaussianBlur,1,{400,0})->id;
    const int effectUse=effects.AddDataMathNode(DataMathMode::Multiply,{800,0})->id;
    Wire(effects,effectSource,"imageOut",blurA,"imageIn");
    Wire(effects,blurA,"imageOut",blurB,"imageIn");
    Wire(effects,blurB,"imageOut",effectUse,"imageA");
    int effectPacked=0;
    Check(effects.CreateCompoundFromSelection({blurA,blurB},"Two blurs",&effectPacked,&error),error);
    Check(!effects.FindNode(blurA) && !effects.FindNode(blurB),"Packing left protected effect nodes outside the compound.");
    children.clear();Check(effects.UnpackCompoundNode(effectPacked,&children,&error),error);
    Check(effects.GetSelectedNodeIds().empty(),"Decompile selected every member.");
    const auto effectChildren=children;
    const int effectGroup=effects.GetGroups().back().id;
    const int intermediateUse=effects.AddDataMathNode(DataMathMode::Multiply,{800,300})->id;
    Wire(effects,effectChildren.front(),"imageOut",intermediateUse,"imageA");
    Check(effects.RepackCompoundGroup(effectGroup,&effectPacked,&error),error);
    for(int id:effectChildren)Check(!effects.FindNode(id),"Repack left a Gaussian member on the canvas.");
    const auto* intermediateLink=effects.FindAnyInputLink(intermediateUse,"imageA");
    Check(intermediateLink && intermediateLink->fromNodeId==effectPacked,
        "Repack did not preserve an intermediate branch through an extra output.");

    const auto halation=std::find_if(templates.begin(),templates.end(),[](const auto& d){return d.identity.id=="stack:compound/halation";});
    Check(halation!=templates.end(),"Halation template missing.");
    for(bool editedHalation:{false,true}) {
        Graph h;h.Clear();h.SetAllowNoOutput(true);
        const auto closure=Stack::NodeMath::CollectCompoundDependencyClosure(templates,{halation->identity},&error);
        Check(!closure.empty(),error);
        for(const auto& reference:closure) {
            const auto* dependency=Stack::NodeMath::FindExactCompoundDefinition(templates,reference);
            Check(dependency && h.AddCompoundDefinition(*dependency,&error),error);
        }
        int id=h.AddCompoundNode(halation->identity,{})->id;
        std::vector<int> body;Check(h.UnpackCompoundNode(id,&body,&error),error);
        const auto countBlurs=[&]() {return std::count_if(h.GetNodes().begin(),h.GetNodes().end(),[](const auto& n){return n.kind==NodeKind::Layer && n.layerType==LayerType::GaussianBlur;});};
        Check(countBlurs()==2,"Halation must decompile to its two Gaussian lobes.");
        if(editedHalation)h.FindNode(body.front())->notes="Edited Halation child";
        const int boundary=h.GetGroups().back().id;
        Check(h.RepackCompoundGroup(boundary,&id,&error),error);
        Check(countBlurs()==0 && h.GetNodes().size()==1,"Halation repack left nodes outside its container.");
        body.clear();Check(h.UnpackCompoundNode(id,&body,&error),error);
        Check(countBlurs()==2,"Reopened Halation lost its Gaussian operations.");
        if(editedHalation)Check(std::any_of(h.GetNodes().begin(),h.GetNodes().end(),[](const auto& n){return n.notes=="Edited Halation child";}),"Halation repack lost its edit.");
    }

    using namespace Stack::Project;
    RawLayerStackDocument raw;
    auto projection=BuildRawProjectGraph(raw.State());auto edited=projection.graph;
    const int rawSplit=edited.AddChannelSplitNode({})->id;
    const int rawMath=edited.AddDataMathNode(DataMathMode::Multiply,{})->id;
    edited.FindNode(rawSplit)->rawLayerOwnerId=kRawBackgroundId;edited.FindNode(rawMath)->rawLayerOwnerId=kRawBackgroundId;
    Wire(edited,projection.preparedSource,"imageOut",rawSplit,"imageIn");Wire(edited,rawSplit,"r",rawMath,"imageA");
    Check(FoldChannelSplits(edited,{rawSplit}).size()==1,"RAW technical-source split did not fold.");
    RawLayerStackState next;
    Check(ApplyRawProjectGraph(raw.State(),projection,edited,nlohmann::json::array(),kRawBackgroundId,next,error),error);
    Check(next.sourceChannelOutputs.size()==1,"RAW source component outputs have no document owner.");
    Check(raw.Apply(next,error),error);
    RawLayerStackState reopened;
    Check(DeserializeRawLayerStack(SerializeRawLayerStack(raw.State()),reopened,error),error);
    Check(reopened.sourceChannelOutputs.size()==1 && BuildRawProjectGraph(reopened).graph.Validate().valid,"RAW component outputs did not reopen.");
    Check(raw.Undo(&error) && raw.State().sourceChannelOutputs.empty() && raw.Redo(&error) && raw.State().sourceChannelOutputs.size()==1,
        "RAW undo/redo lost component-output ownership.");
    std::cout<<"Channel outputs: identity, modification/mixing, folding, shared lowering, persistence/rejection, opaque alpha, compound repack and RAW ownership/history passed.\n";
}
