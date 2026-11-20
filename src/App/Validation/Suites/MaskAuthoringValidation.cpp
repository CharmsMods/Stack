#include "Raw/MaskAuthoring.h"
#include "Project/AuthoredMask.h"
#include "App/Validation/ValidationImageUtils.h"
#include "ThirdParty/stb_image.h"
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
namespace {
using namespace MaskAuthoring;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::shared_ptr<const RawAssetCapture::PreviewField> Load(const std::filesystem::path& file){
    int w=0,h=0,n=0;std::unique_ptr<unsigned char,decltype(&stbi_image_free)> image(stbi_load(file.string().c_str(),&w,&h,&n,4),stbi_image_free);
    Check(bool(image),"Could not read the reference photo.");
    std::vector<float> rgba(size_t(w)*h*4);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const auto* src=image.get()+(size_t(y)*w+x)*4;auto* dst=&rgba[(size_t(h-1-y)*w+x)*4];
        for(int c=0;c<3;++c){float v=src[c]/255.f;dst[c]=v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);}dst[3]=src[3]/255.f;
    }
    return RawAssetCapture::PreparePreview(rgba,w,h,Raw::RawWorkingSpace::LinearSrgbD65,{});
}
void Save(const Assets::FloatData& mask,const std::filesystem::path& path){
    std::vector<unsigned char> rgba(mask.values.size()*4,255);
    for(size_t i=0;i<mask.values.size();++i)for(int c=0;c<3;++c)rgba[i*4+c]=static_cast<unsigned char>(std::round(mask.values[i]*255));
    Check(WriteValidationPng(path,rgba,mask.width,mask.height),"Could not save coverage preview.");
}
}
bool ValidateMaskAuthoring(const std::filesystem::path& sleeve,const std::filesystem::path& sky,const std::filesystem::path& output){
    using namespace Project;
    try {
        std::filesystem::create_directories(output);
        auto field=Load(sleeve);
        Recipe recipe;
        recipe.marks={{Tool::Brush,Role::Include,{{.03f,.86f},{.38f,.71f},{.65f,.55f},{.95f,.37f}},.19f,.36f},
            {Tool::Circle,Role::Protect,{{.35f,.32f}},.14f},
            {Tool::Edge,Role::Transition,{{.53f,.65f},{.47f,.51f}}}};
        CaptureEvidence(*field,recipe);
        Check(!recipe.marks[1].samples.empty() && recipe.marks[2].samples.size()==17,"Missing measured circle or edge evidence.");
        const auto mask=Evaluate(*field,recipe).data;Check(mask && mask->Valid(true),"Invalid sleeve coverage.");
        Save(*mask,output/"sleeve-mask.png");
        const auto coverage=[&](float x,float y){return mask->values[size_t(y*(mask->height-1))*mask->width+int(x*(mask->width-1))];};
        Check(coverage(.35f,.32f)==0 && coverage(.65f,.55f)>.8f,"Protected or painted side lost.");
        RawLayerStackState state;auto& graph=state.background.graph;
        Check(!FindRawOperation(state.background,RawRecipe::GraphOperationKind::Calibration),"New project spawned Calibration.");
        const auto* light=FindRawOperation(state.background,RawRecipe::GraphOperationKind::LocalEv);
        Check(light!=nullptr,"Missing Light.");
        const auto lightUuid=light->instanceUuid;
        AuthoredMaskEdit edit{{},{},"Sleeve",Serialize(recipe),mask};
        WriteAuthoredMask(graph,edit);RegisterRawMaskOutputs(state,kRawBackgroundId);
        const auto& outputs=state.background.maskOutputs;Check(outputs.size()==1,"Standalone texture was not published.");
        const auto uuid=outputs.front().nodeUuid;const auto id=FindRawPublishedNode(state.background,outputs.front())->id;
        Check(std::none_of(graph.GetLinks().begin(),graph.GetLinks().end(),[&](const auto& l){return l.fromNodeId==id || l.toNodeId==id;}),"Standalone mask was connected.");
        edit.destination=lightUuid;const auto local=WriteAuthoredMask(graph,edit);RegisterRawMaskOutputs(state,kRawBackgroundId);
        Check(!local.empty() && local!=lightUuid && outputs.size()==2,"Fresh local copy failed.");
        std::string error;
        Check(SetRawOperationMask(state,kRawBackgroundId,lightUuid,RawMaskReference{kRawBackgroundId,outputs.front().id},error),error.c_str());
        Check(SetRawOperationMask(state,kRawBackgroundId,local,RawMaskReference{kRawBackgroundId,outputs.front().id},error),error.c_str());
        // Exercise a deliberate revision and a normal save/reopen with an older Calibration node.
        edit.existing=uuid;WriteAuthoredMask(graph,edit);
        graph.AddRawOperationNode(RawRecipe::GraphOperationKind::Calibration,{0,600});
        RawLayerStackState restored;Check(DeserializeRawLayerStack(SerializeRawLayerStack(state),restored,error),error.c_str());
        Check(FindRawOperation(restored.background,RawRecipe::GraphOperationKind::Calibration)!=nullptr,"Existing Calibration was lost.");
        RawLayerStackDocument history;
        Check(history.Apply(restored,error) && history.Undo(&error) && history.State().background.maskOutputs.empty() &&
            history.Redo(&error) && history.State().background.maskOutputs.size()==2,"Mask transaction undo/redo failed.");
        const auto* reopened=restored.background.graph.FindNode(id);
        Check(reopened && Serialize(Deserialize(*reopened->customMask.authoring))==Serialize(recipe),"Editing recipe failed round trip.");
        Check(std::count_if(restored.background.graph.GetLinks().begin(),restored.background.graph.GetLinks().end(),[&](const auto& l){return l.fromNodeId==id;})==2,"Shared wires changed.");
        auto before=reopened->customMask.rasterLayer;
        FindRawOperation(restored.background,RawRecipe::GraphOperationKind::LocalEv)->rawOperation.enabled=false;
        Check(before==restored.background.graph.FindNode(id)->customMask.rasterLayer,"Upstream edit changed baked coverage.");
        bool canceled=false;try{Evaluate(*field,recipe,[]{return true;});}catch(const std::runtime_error&){canceled=true;}
        Check(canceled,"Canceled authoring produced a result.");
        field=Load(sky);recipe.marks={{Tool::Values,Role::Include,{{.02f,.13f},{.12f,.2f}}},
            {Tool::Circle,Role::Protect,{{.26f,.19f}},.04f}};
        const auto connected=Evaluate(*field,recipe).data;recipe.wholeImage=true;const auto whole=Evaluate(*field,recipe).data;
        size_t additional=0;for(size_t i=0;i<whole->values.size();++i){Check(whole->values[i]>=connected->values[i],"Connected coverage exceeded global match.");additional+=whole->values[i]>connected->values[i]+.01f;}
        Check(additional>0,"Global reach did not include disconnected sky samples.");
        Save(*connected,output/"sky-connected.png");Save(*whole,output/"sky-global.png");
        std::cout<<"PASS: real sleeve and sky, "<<additional<<" additional whole-image matches, standalone/shared mask, local adjustment, edit, reopen, fixed coverage and cancel."<<std::endl;
        return true;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return false;}
}
}

