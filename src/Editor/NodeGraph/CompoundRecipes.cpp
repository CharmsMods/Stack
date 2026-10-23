#include "Editor/NodeGraph/CompoundRecipes.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Graph/LayerOperationParameters.h"
#include <array>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace EditorNodeGraphDefinitions {
namespace {
using namespace EditorNodeGraph;
using namespace Stack::NodeMath;

struct Recipe {
    Graph graph;
    CompoundDefinition definition;
    int identity;
    explicit Recipe(int identity, const char* id, const char* label) : identity(identity) {
        graph.Clear(); graph.SetAllowNoOutput(true);
        definition.definitionUuid = Uuid(0);
        definition.identity.id = std::string("stack:compound/") + id;
        definition.identity.version = {1,0,0};
        definition.label = label;
        definition.description = "Editable canonical graph of standalone operations.";
        definition.source = CompoundDefinitionSource::ShippedTemplate;
        definition.lifecycleNotes = "Decompile one level; repack the edited graph. No native specialization.";
    }
    std::string Uuid(int n) const {
        char text[64]; std::snprintf(text,sizeof(text),"%08d-1019-4000-8000-%012d",identity,n); return text;
    }
    int Keep(Node* node) {
        if (!node) throw std::runtime_error("Could not construct compound recipe node.");
        node->instanceUuid=Uuid(node->id);
        node->position={float((node->id-1)%6)*280.f,float((node->id-1)/6)*240.f};
        if (node->kind==NodeKind::Compound) node->compound.instance.instanceUuid=node->instanceUuid;
        return node->id;
    }
    Node& At(int id) { return *graph.FindNode(id); }
    // Purpose notes are authored template data, copied with the node on decompile.
    void Note(int id, const std::string& text) { At(id).notes = text; }
    int Math(DataMathMode mode, float b=1.f, bool uniform=false) {
        const int id=Keep(graph.AddDataMathNode(mode,{}));
        At(id).dataMathSettings.constantB=b;
        At(id).dataMathSettings.domain=uniform ? ArithmeticDomain::Values : ArithmeticDomain::Spatial;
        return id;
    }
    int Identity() {return Math(DataMathMode::Multiply);}
    int Split(int source) {const int id=Keep(graph.AddChannelSplitNode({})); Wire(source,"imageOut",id,"imageIn");return id;}
    int Combine() {return Keep(graph.AddChannelCombineNode({}));}
    void Wire(int a,const char* out,int b,const char* in) {
        std::string error;
        if (!graph.TryConnectSockets(a,out,b,in,&error))
            throw std::runtime_error(definition.label+": "+error+" ("+std::to_string(a)+"."+out+" -> "+std::to_string(b)+"."+in+")");
    }
    void Wire(int a,int b,const char* in="imageA") {Wire(a,"imageOut",b,in);}
    void Port(const char* id,const char* label,int node,const char* socket,LogicalValueType type,
        bool output=false,bool optional=false,bool numeric=false) {
        definition.ports.push_back({id,label,output?PortDirection::Output:PortDirection::Input,type,optional,At(node).instanceUuid,socket,numeric});
    }
    void Input(int node,const char* socket="imageA",bool numeric=true) {
        Port("image-in","Image / data",node,socket,LogicalValueType::ColorImage,false,false,numeric);
    }
    void Output(int node,LogicalValueType type=LogicalValueType::ColorImage,const char* socket="imageOut",bool numeric=true) {
        Port("image-out","Result",node,socket,type,true,false,numeric);
    }
    void Param(const char* id,const char* label,int node,const char* path,double value,
        double minimum,double maximum,const char* units="unitless") {
        CompoundParameterDefinition p;
        p.id=id;p.label=label;p.defaultValue=value;p.hardDomain={true,minimum,maximum,true,true};
        p.units=units;p.internalInstanceUuid=At(node).instanceUuid;p.internalParameterId=path;
        definition.parameters.push_back(std::move(p));
    }
    int Control(const char* id,const char* label,double value,double low,double high,const char* units="unitless") {
        const int n=Math(DataMathMode::Add,0,true);
        At(n).dataMathSettings.constantA=float(value);
        Param(id,label,n,"dataMathSettings.constantA",value,low,high,units);
        const std::string port=ParameterInputSocketId(id);
        Port(port.c_str(),label,n,"imageA",LogicalValueType::Scalar,false,true);
        return n;
    }
    int Child(const CompoundDefinition& child) {
        Graph embedded;
        DeserializeGraphPayload(child.canonicalGraph,embedded,0);
        for (const auto& d:embedded.GetCompoundDefinitions()) AddDependency(d);
        AddDependency(child);
        return Keep(graph.AddCompoundNode(child.identity,{}));
    }
    void AddDependency(const CompoundDefinition& child) {
        std::string error;
        if (!graph.AddCompoundDefinition(child,&error)) throw std::runtime_error(definition.label+": "+error);
        if (std::none_of(definition.dependencies.begin(),definition.dependencies.end(),[&](const auto& r){return SameDefinitionReference(r,child.identity);}))
            definition.dependencies.push_back(child.identity);
    }
    void Override(int n,const char* id,double value) {SetCompoundParameterOverride(At(n).compound.instance,id,value);}
    CompoundDefinition Finish() {
        Stack::GraphModel::ReindexLayerParameterRecords(graph);
        definition.canonicalGraph=SerializeGraphPayload(nlohmann::json::array(),graph);
        RefreshCompoundDefinitionContentHash(definition);
        return definition;
    }
};

CompoundDefinition Percent() {
    Recipe r(101,"percent","Percent");
    const int divide=r.Math(DataMathMode::Divide,100,true);
    r.At(divide).dataMathSettings.constantA=100;
    r.At(divide).dataMathSettings.percentageInput=true;
    r.Param("percent","Percent (%)",divide,"dataMathSettings.constantA",100,-100000,100000,"percent");
    r.Port("param:percent","Percentage",divide,"imageA",LogicalValueType::Scalar,false,true);
    const int multiply=r.Math(DataMathMode::Multiply);
    r.Wire(divide,multiply,"imageB");r.Input(multiply);r.Output(multiply);
    r.Port("mask-in","Effect strength mask",multiply,"maskIn",LogicalValueType::Channel,false,true);
    r.Note(divide,"Convert a percentage to a multiplier: 10% becomes 0.1.");
    r.Note(multiply,"Scale the input by that multiplier. A black effect mask keeps the original input.");
    r.definition.description="Take P percent of the input: A × (P / 100). A black effect mask bypasses the gain.";
    return r.Finish();
}

CompoundDefinition Luminance(int id,const char* name,const char* label,const char* basis,
    std::array<double,3> weights,const CompoundDefinition& percent) {
    Recipe r(id,name,label);
    const int entry=r.Identity();r.At(entry).dataMathSettings.requiredLinearBasis=basis;r.Input(entry,"imageA",false);
    const int split=r.Split(entry);
    r.Note(entry,"Pass the input through unchanged in the expected linear color space.");
    r.Note(split,"Separate red, green and blue so each can contribute to brightness.");
    const char* channels[]={"r","g","b"};const char* labels[]={"Red coefficient (%)","Green coefficient (%)","Blue coefficient (%)"};
    const char* notes[]={"Weight red's contribution to brightness.","Weight green's contribution to brightness.","Weight blue's contribution to brightness."};
    int terms[3];
    for (int i=0;i<3;++i) {
        terms[i]=r.Child(percent);r.Override(terms[i],"percent",weights[i]*100);
        r.Note(terms[i],notes[i]);
        r.Wire(split,channels[i],terms[i],"image-in");
        r.Param(channels[i],labels[i],terms[i],"compound:percent",weights[i]*100,-10000,10000,"percent");
        const auto port=ParameterInputSocketId(channels[i]);
        r.Port(port.c_str(),labels[i],terms[i],"param:percent",LogicalValueType::Scalar,false,true);
    }
    const int rg=r.Math(DataMathMode::Add), y=r.Math(DataMathMode::Add);
    r.Wire(terms[0],"image-out",rg,"imageA");r.Wire(terms[1],"image-out",rg,"imageB");
    r.Wire(rg,y);r.Wire(terms[2],"image-out",y,"imageB");r.Output(y,LogicalValueType::Channel);
    r.Note(rg,"Add the weighted red and green brightness contributions.");
    r.Note(y,"Add the blue contribution to finish the brightness estimate.");
    r.definition.description=std::string("Relative Y in linear ")+basis+". Editable coefficients are not normalized; edited weights define custom weighted brightness.";
    return r.Finish();
}

CompoundDefinition SmoothCurve() {
    Recipe r(105,"smoothstep-curve","Smoothstep Tone Curve");
    const int t=r.Math(DataMathMode::Remap);
    r.Input(t);r.At(t).dataMathSettings.minValue=.5f;r.At(t).dataMathSettings.maxValue=1;
    r.Param("low","Black endpoint (linear HDR)",t,"dataMathSettings.minValue",.5,-10000,10000);
    r.Param("high","White endpoint (linear HDR)",t,"dataMathSettings.maxValue",1,-10000,10000);
    const int squared=r.Math(DataMathMode::Multiply),twice=r.Math(DataMathMode::Multiply,2),complement=r.Math(DataMathMode::Subtract),product=r.Math(DataMathMode::Multiply),bounded=r.Math(DataMathMode::Clamp);
    r.At(complement).dataMathSettings.constantA=3;
    r.At(bounded).dataMathSettings.boundedMask=true;
    r.Wire(t,squared);r.Wire(t,squared,"imageB");r.Wire(t,twice);r.Wire(twice,complement,"imageB");
    r.Wire(squared,product);r.Wire(complement,product,"imageB");r.Wire(product,bounded);r.Output(bounded,LogicalValueType::Mask);
    r.Note(t,"Map the chosen black and white endpoints onto a 0-to-1 ramp.");
    r.Note(squared,"Square the ramp to help the selection begin gently.");
    r.Note(twice,"Double the ramp for the next curve-shaping step.");
    r.Note(complement,"Calculate 3 minus twice the ramp; this helps soften the bright end.");
    r.Note(product,"Combine the two curve terms to create a smooth S-shaped transition.");
    r.Note(bounded,"Keep the result between 0 and 1 so it can act as a mask.");
    r.definition.description="Scalar curve t²(3−2t), with editable HDR input endpoints and constant 0/1 endpoint extension. Clamp explicitly converts the result to coverage.";
    return r.Finish();
}

CompoundDefinition Colorize() {
    Recipe r(106,"colorize","Colorize");
    const int entry=r.Identity();r.At(entry).dataMathSettings.domain=ArithmeticDomain::Channel;
    r.Port("image-in","Scalar signal",entry,"imageA",LogicalValueType::Channel);
    const int combine=r.Combine();
    r.Note(entry,"Pass the scalar signal through unchanged before giving it an RGB color.");
    r.Note(combine,"Assemble the three scaled signals into one RGB image.");
    const char* channels[]={"r","g","b"};const char* labels[]={"Red coefficient","Green coefficient","Blue coefficient"};
    const char* names[]={"red","green","blue"};
    for(int i=0;i<3;++i) {
        const int gain=r.Control(channels[i],labels[i],1,-100,100);
        const int multiply=r.Math(DataMathMode::Multiply);r.Wire(entry,multiply);r.Wire(gain,multiply,"imageB");r.Wire(multiply,"imageOut",combine,channels[i]);
        r.Note(gain,std::string("Set how much ")+names[i]+" the signal contributes. 0 removes it.");
        r.Note(multiply,std::string("Scale the signal to create its ")+names[i]+" channel.");
    }
    r.Output(combine,LogicalValueType::ColorImage,"imageOut",false);
    r.definition.description="A scalar signal times three numerical RGB coefficients. Coefficients are in the declared working basis, not an encoded color picker.";
    return r.Finish();
}

CompoundDefinition Mixer() {
    Recipe r(107,"channel-mixer","Channel Mixer");
    const int entry=r.Identity();r.Input(entry,"imageA",false);const int split=r.Split(entry),combine=r.Combine();
    const char* channels[]={"r","g","b"};
    const char* names[]={"red","green","blue"};
    r.Note(entry,"Pass the original image through unchanged into the channel mixer.");
    r.Note(split,"Separate the input colors so each output can mix them differently.");
    r.Note(combine,"Assemble the mixed colors and preserve the original transparency.");
    for(int row=0;row<3;++row) {
        int terms[3];
        for(int col=0;col<3;++col) {
            const std::string id=std::string(channels[row])+channels[col];
            const std::string label=std::string("Output ")+channels[row]+" from "+channels[col];
            const int gain=r.Control(id.c_str(),label.c_str(),row==col?1:0,-100,100);
            terms[col]=r.Math(DataMathMode::Multiply);r.Wire(split,channels[col],terms[col],"imageA");r.Wire(gain,terms[col],"imageB");
            r.Note(gain,std::string("Set how much input ")+names[col]+" feeds output "+names[row]+".");
            r.Note(terms[col],std::string("Scale input ")+names[col]+" for the new "+names[row]+" channel.");
        }
        const int sum=r.Math(DataMathMode::Add),all=r.Math(DataMathMode::Add);
        r.Wire(terms[0],sum);r.Wire(terms[1],sum,"imageB");r.Wire(sum,all);r.Wire(terms[2],all,"imageB");r.Wire(all,"imageOut",combine,channels[row]);
        r.Note(sum,std::string("Add the red and green contributions for output ")+names[row]+".");
        r.Note(all,std::string("Add the blue contribution to finish output ")+names[row]+".");
    }
    r.Wire(split,"a",combine,"a");r.Output(combine,LogicalValueType::ColorImage,"imageOut",false);
    r.definition.description="3×3 RGB matrix: each output row is the sum of three weighted input channels. Alpha passes through separately.";
    return r.Finish();
}

CompoundDefinition LinearMix() {
    Recipe r(108,"linear-mix","Linear Mix");
    const int a=r.Identity(),b=r.Identity(),delta=r.Math(DataMathMode::Subtract),scaled=r.Math(DataMathMode::Multiply),out=r.Math(DataMathMode::Add);
    r.Port("a","A / base",a,"imageA",LogicalValueType::ColorImage,false,false,true);
    r.Port("b","B / processed",b,"imageA",LogicalValueType::ColorImage,false,false,true);
    const int amount=r.Control("factor","Factor",.5,0,1);
    r.Wire(b,delta);r.Wire(a,delta,"imageB");r.Wire(delta,scaled);r.Wire(amount,scaled,"imageB");r.Wire(a,out);r.Wire(scaled,out,"imageB");r.Output(out);
    r.Note(a,"Pass the base image through unchanged. This is the result at factor 0.");
    r.Note(b,"Pass the second image through unchanged. This is the result at factor 1.");
    r.Note(amount,"Choose the blend: 0 uses the base; 1 uses the second image.");
    r.Note(delta,"Find the difference between the second image and the base.");
    r.Note(scaled,"Keep the chosen fraction of that difference.");
    r.Note(out,"Add the scaled difference to the base to finish the blend.");
    return r.Finish();
}

CompoundDefinition Invert() {
    Recipe r(109,"invert-mask","Invert Mask (compound)");
    const int bounded=r.Math(DataMathMode::Clamp),invert=r.Math(DataMathMode::Subtract);
    r.At(bounded).dataMathSettings.boundedMask=true;r.Input(bounded);
    r.At(invert).dataMathSettings.constantA=1;r.Wire(bounded,invert,"imageB");r.Output(invert,LogicalValueType::Channel);
    r.Note(bounded,"Keep the input within the mask range, from 0 to 1.");
    r.Note(invert,"Subtract the mask from 1: black becomes white, and white becomes black.");
    return r.Finish();
}

CompoundDefinition AverageThree() {
    Recipe r(112,"average-three","Average (3 inputs)");
    const int a=r.Identity(),b=r.Identity(),c=r.Identity();
    r.Port("a","A",a,"imageA",LogicalValueType::ColorImage,false,false,true);
    r.Port("b","B",b,"imageA",LogicalValueType::ColorImage,false,false,true);
    r.Port("c","C",c,"imageA",LogicalValueType::ColorImage,false,false,true);
    const int ab=r.Math(DataMathMode::Add),abc=r.Math(DataMathMode::Add),mean=r.Math(DataMathMode::Divide,3);
    r.Wire(a,ab);r.Wire(b,ab,"imageB");r.Wire(ab,abc);r.Wire(c,abc,"imageB");r.Wire(abc,mean);r.Output(mean);
    r.Note(a,"Pass the first input through unchanged into the average.");
    r.Note(b,"Pass the second input through unchanged into the average.");
    r.Note(c,"Pass the third input through unchanged into the average.");
    r.Note(ab,"Add the first two inputs.");
    r.Note(abc,"Add the third input to the running total.");
    r.Note(mean,"Divide the total by 3 so all three inputs contribute equally.");
    r.definition.description="Equal-weight mean of exactly three connected operands: (A+B+C)/3. Uniform numbers broadcast; the variable-count Average node remains available.";
    return r.Finish();
}

CompoundDefinition Halation(int id,const char* name,const char* label,const char* basis,
    const CompoundDefinition& luminance,const CompoundDefinition& percent,const CompoundDefinition& curve) {
    Recipe r(id,name,label);
    const int original=r.Identity();r.Input(original,"imageA",false);r.At(original).dataMathSettings.requiredLinearBasis=basis;
    const int y=r.Child(luminance);r.Wire(original,"imageOut",y,"image-in");
    const int source=r.Child(curve);r.Wire(y,"image-out",source,"image-in");
    r.Note(original,"Keep the original photo unchanged for the final addition and brightness checks.");
    r.Note(y,"Estimate the original photo's brightness for light selection and protection.");
    r.Note(source,"Select bright areas that will emit the glow, with a soft transition.");
    r.Param("source-low","Source black endpoint",source,"compound:low",.5,-100,100);
    r.Param("source-high","Source white endpoint",source,"compound:high",1,-100,100);
    const int extracted=r.Math(DataMathMode::Multiply),positive=r.Math(DataMathMode::Max,0),covered=r.Math(DataMathMode::Multiply);
    const int originalSplit=r.Split(original);
    r.At(originalSplit).splitMissingAlphaIsOpaque=true;
    r.Wire(original,extracted);r.Wire(source,"image-out",extracted,"imageB");r.Wire(extracted,positive);r.Wire(positive,covered);r.Wire(originalSplit,"a",covered,"imageB");
    r.Note(extracted,"Keep RGB light from the selected bright areas; suppress the rest.");
    r.Note(positive,"Discard negative light values before creating the glow.");
    r.Note(originalSplit,"Read the original transparency so invisible areas cannot emit light.");
    r.Note(covered,"Scale the extracted light by its visible coverage.");
    // Coverage is consumed once as emitted light, then the convolution image
    // gets opaque alpha so the generic Gaussian does not divide it away.
    const int extractedSplit=r.Split(covered),opaque=r.Combine();
    for(const char* channel:{"r","g","b"})r.Wire(extractedSplit,channel,opaque,channel);
    const int alpha=r.Keep(r.graph.AddConstantChannelNode({}));r.At(alpha).constantChannelSettings.value=1;
    r.Wire(extractedSplit,"r",alpha,"matchExtent");r.Wire(alpha,"channelOut",opaque,"a");
    r.Note(extractedSplit,"Separate the covered RGB light from its old transparency channel.");
    r.Note(alpha,"Supply opaque alpha (1) for the glow image, at the matching image size.");
    r.Note(opaque,"Rebuild extracted RGB with opaque alpha so the blur preserves its light energy.");
    int lobes[2];
    for(int i=0;i<2;++i) {
        const int blur=r.Keep(r.graph.AddLayerNode(LayerType::GaussianBlur,-1,{}));
        r.At(blur).layerParameters={{"type","GaussianBlur"},{"sigmaMode",true},{"sigmaFraction",i?.006:.0015},{"amount",2.0}};
        r.Wire(opaque,"imageOut",blur,"imageIn");
        r.Param(i?"aura-radius":"radius",i?"Aura sigma (fraction of shorter side)":"Local sigma (fraction of shorter side)",blur,"layer:sigmaFraction",i?.006:.0015,0,.25,"fraction-of-shorter-dimension");
        lobes[i]=r.Child(percent);r.Override(lobes[i],"percent",i?0:100);r.Wire(blur,"imageOut",lobes[i],"image-in");
        r.Param(i?"aura":"local",i?"Aura amount (%)":"Local amount (%)",lobes[i],"compound:percent",i?0:100,0,1000,"percent");
        r.Note(blur,i ? "Spread light over a wider area to create the broad aura."
                     : "Spread light a short distance to create the tight local glow.");
        r.Note(lobes[i],i ? "Set how much the broad aura contributes. 0% disables this branch."
                         : "Set how much the tight local glow contributes.");
    }
    const int blurred=r.Math(DataMathMode::Add);r.Wire(lobes[0],"image-out",blurred,"imageA");r.Wire(lobes[1],"image-out",blurred,"imageB");
    const int by=r.Child(luminance),redCurve=r.Child(curve);r.Wire(blurred,"imageOut",by,"image-in");r.Wire(by,"image-out",redCurve,"image-in");r.Override(redCurve,"low",0);
    r.Note(blurred,"Add the local glow and broad aura before adjusting their color.");
    r.Note(by,"Measure the blurred light's brightness to guide its red gain.");
    r.Note(redCurve,"Choose where brightness allows the red gain to take effect.");
    r.Param("red-low","Red gain mask black endpoint",redCurve,"compound:low",0,-100,100);r.Param("red-high","Red gain mask white endpoint",redCurve,"compound:high",1,-100,100);
    // Mr = 1 + enabled * (curve - 1). Off retains the authored curve.
    const int redEnabled=r.Control("red-mask","Red gain follows brightness (0 off / 1 on)",1,0,1);
    const int redMinus=r.Math(DataMathMode::Subtract,1),redSwitch=r.Math(DataMathMode::Multiply),mr=r.Math(DataMathMode::Add,1);
    r.Wire(redCurve,"image-out",redMinus,"imageA");r.Wire(redMinus,redSwitch);r.Wire(redEnabled,redSwitch,"imageB");r.Wire(redSwitch,mr);
    r.Note(redEnabled,"Choose red gain behavior: 0 applies it everywhere; 1 follows brightness.");
    r.Note(redMinus,"Find how the brightness mask differs from full effect (1).");
    r.Note(redSwitch,"Apply the brightness-follow control to that difference.");
    r.Note(mr,"Add back 1: the control now chooses full red gain or the brightness mask.");
    const int split=r.Split(blurred),tinted=r.Combine();const char* channels[]={"r","g","b"};const char* gainLabels[]={"Red gain","Green gain","Blue gain"};const double defaults[]={1.8,.5,.1};
    const char* gainNotes[]={"Set red gain in the glow. 1 leaves it unchanged.","Set green gain in the glow. 1 leaves it unchanged.","Set blue gain in the glow. 1 leaves it unchanged."};
    const char* tintNotes[]={"Adjust the glow's red; the brightness mask limits where this gain applies.","Scale the glow's green channel to shape its color.","Scale the glow's blue channel to shape its color."};
    r.Note(split,"Separate the blurred red, green and blue so their gains can differ.");
    r.Note(tinted,"Reassemble the adjusted channels into the colored glow.");
    for(int i=0;i<3;++i){
        const int gain=r.Control(channels[i],gainLabels[i],defaults[i],-100,100),multiply=r.Math(DataMathMode::Multiply);
        r.Wire(split,channels[i],multiply,"imageA");r.Wire(gain,multiply,"imageB");
        if(i==0)r.Wire(mr,"imageOut",multiply,"maskIn");
        r.Wire(multiply,"imageOut",tinted,channels[i]);
        r.Note(gain,gainNotes[i]);
        r.Note(multiply,tintNotes[i]);
    }
    r.Wire(alpha,"channelOut",tinted,"a");
    const int protectionCurve=r.Child(curve);r.Wire(y,"image-out",protectionCurve,"image-in");
    r.Note(protectionCurve,"Select bright areas of the original photo to protect from extra glow.");
    r.Param("protection-low","Protection black endpoint",protectionCurve,"compound:low",.5,-100,100);r.Param("protection-high","Protection white endpoint",protectionCurve,"compound:high",1,-100,100);
    // Q = independent + linked * (source - independent).
    const int linked=r.Control("linked-protection","Link protection to source (0 independent / 1 linked)",0,0,1);
    const int qDelta=r.Math(DataMathMode::Subtract),qScale=r.Math(DataMathMode::Multiply),q=r.Math(DataMathMode::Add);
    r.Wire(source,"image-out",qDelta,"imageA");r.Wire(protectionCurve,"image-out",qDelta,"imageB");r.Wire(qDelta,qScale);r.Wire(linked,qScale,"imageB");r.Wire(protectionCurve,"image-out",q,"imageA");r.Wire(qScale,q,"imageB");
    r.Note(linked,"Choose protection: 0 uses its own curve; 1 uses the source selection.");
    r.Note(qDelta,"Find the difference between source selection and independent protection.");
    r.Note(qScale,"Scale that difference by the protection-link control.");
    r.Note(q,"Build the chosen protection selection by adding the scaled difference.");
    const int protectionAmount=r.Control("protection","Protection amount",1,0,1),pScale=r.Math(DataMathMode::Multiply),p=r.Math(DataMathMode::Subtract);
    r.Wire(q,pScale);r.Wire(protectionAmount,pScale,"imageB");r.At(p).dataMathSettings.constantA=1;r.Wire(pScale,p,"imageB");
    r.Note(protectionAmount,"Set protection strength: 0 gives none; 1 gives full protection.");
    r.Note(pScale,"Apply protection strength to the selected areas.");
    r.Note(p,"Turn protection into permission: protected areas receive less glow.");
    const int protectedHalo=r.Math(DataMathMode::Multiply),strength=r.Child(percent),overall=r.Math(DataMathMode::Multiply),out=r.Math(DataMathMode::Add);
    r.Wire(tinted,protectedHalo);r.Wire(p,protectedHalo,"imageB");r.Wire(protectedHalo,"imageOut",strength,"image-in");r.Override(strength,"percent",10);
    r.Param("strength","Strength (%)",strength,"compound:percent",10,0,1000,"percent");
    r.Port("param:strength","Strength (%)",strength,"param:percent",LogicalValueType::Scalar,false,true);
    r.Wire(strength,"image-out",overall,"imageA");r.Port("mask-in","Overall destination mask",overall,"imageB",LogicalValueType::Channel,false,true,true);
    r.Wire(original,out);r.Wire(overall,out,"imageB");r.Output(out,LogicalValueType::ColorImage,"imageOut",false);
    r.Note(protectedHalo,"Apply destination protection to the colored glow.");
    r.Note(strength,"Set the overall glow strength as a percentage.");
    r.Note(overall,"Apply the external mask: black hides the glow; white keeps it.");
    r.Note(out,"Add the finished glow to the photo, retaining its original transparency.");
    r.Port("source-mask","Source selection",source,"image-out",LogicalValueType::Mask,true);
    r.Port("extracted","Extracted RGB light",opaque,"imageOut",LogicalValueType::ColorImage,true);
    r.Port("blurred","Pre-gain blurred RGB",blurred,"imageOut",LogicalValueType::ColorImage,true);
    r.Port("red-mask","Red gain mask",mr,"imageOut",LogicalValueType::Channel,true,false,true);
    r.Port("protection-mask","Destination protection",p,"imageOut",LogicalValueType::Channel,true,false,true);
    r.Port("halo","Isolated RGB contribution",overall,"imageOut",LogicalValueType::ColorImage,true);
    for (auto& port : r.definition.ports) {
        port.advanced = port.id != "image-in" && port.id != "image-out" && port.id != "mask-in";
        if (port.id == "image-in") port.label = "Image";
        if (port.id == "mask-in") port.label = "Mask";
    }
    r.definition.description=std::string("Artistic RGB halation in linear ")+basis+". Spread bright light, tint it, then add it back with highlight protection.";
    return r.Finish();
}
}

void AppendModularCompoundRecipes(std::vector<CompoundDefinition>& catalog) {
    const auto percent=Percent();catalog.push_back(percent);
    // Coefficients use the RGB-to-XYZ Y rows; each instance keeps its explicit
    // basis requirement inside the executable graph even after decompilation.
    const auto srgb=Luminance(102,"luminance-srgb","Luminance (linear sRGB)","srgb-d65",{.21263900587151036,.7151686787677559,.07219231536073371},percent);
    const auto rec2020=Luminance(103,"luminance-rec2020","Luminance","rec2020-d65",{.2627002120112671,.6779980715188708,.05930171646986196},percent);
    const auto p3=Luminance(104,"luminance-display-p3","Luminance (linear Display P3)","display-p3-d65",{.2289745640697488,.6917385218365064,.079286914093745},percent);
    catalog.push_back(srgb);catalog.push_back(rec2020);catalog.push_back(p3);
    const auto curve=SmoothCurve();catalog.push_back(curve);
    catalog.push_back(Colorize());catalog.push_back(Mixer());catalog.push_back(LinearMix());catalog.push_back(Invert());catalog.push_back(AverageThree());
    catalog.push_back(Halation(110,"halation","Halation","rec2020-d65",rec2020,percent,curve));
    catalog.push_back(Halation(111,"halation-srgb","Halation (linear sRGB)","srgb-d65",srgb,percent,curve));
}
}
