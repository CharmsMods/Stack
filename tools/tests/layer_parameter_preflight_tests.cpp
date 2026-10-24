#include "Editor/LayerParameterPreflight.h"
#include "Editor/Layers/SplitAdjustmentsLayers.h"
#include "Renderer/FullscreenQuad.h"
#include "Renderer/GLHelpers.h"
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
int renderingCalls=0;
[[noreturn]] void UnexpectedRendering() { ++renderingCalls; throw std::runtime_error("CPU preflight attempted rendering or UI"); }
void Require(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
}
// Link guards for the actual Brightness implementation's unused virtual methods.
// No GL context, shader initialization, execution or UI is allowed in this test.
namespace GLHelpers { unsigned int CreateShaderProgram(const char*,const char*){UnexpectedRendering();} }
void FullscreenQuad::Draw(){UnexpectedRendering();}
#ifndef _WIN32
extern "C" void glBindTexture(GLenum,GLuint){UnexpectedRendering();}
#endif
namespace ImGuiExtras { bool NodeSliderFloat(const char*,const char*,float*,float,float,const char*,float){UnexpectedRendering();} }

int main() try {
    using nlohmann::json;
    using Stack::EditorPersistence::PreflightLayerParameters;
    int created=0;
    const auto factory=[&](const std::string& type)->std::shared_ptr<LayerBase> {
        if(type!="Brightness")return {};
        ++created;return std::make_shared<BrightnessLayer>();
    };
    const json records=json::array({{{"type","Brightness"},{"brightness",.25},{"visible",false}}});
    const auto original=records;
    PreflightLayerParameters(records,factory);
    Require(created==1 && records==original,"preflight changed authored records");
    PreflightLayerParameters(json::array({{{"type","Brightness"}}}),factory);
    PreflightLayerParameters(json::array(),factory);
    BrightnessLayer live;
    Stack::GraphModel::PrepareLayerParameterDraft(live,records[0]);
    Require(live.Serialize().at("brightness")==.25f && !live.IsVisible(),"valid decoder semantics changed");
    BrightnessLayer defaults;
    Stack::GraphModel::PrepareLayerParameterDraft(defaults,{{"type","Brightness"}});
    Require(defaults.Serialize().at("brightness")==0.f && defaults.IsVisible(),"missing fields changed defaults");
    const auto liveBefore=live.Serialize();
    for(const char* field:{"brightness","visible"}) {
        auto bad=records;bad[0][field]=json::object();bool rejected=false;
        try { PreflightLayerParameters(bad,factory); }
        catch(const json::exception&) { rejected=true; }
        Require(rejected,"actual decoder accepted malformed field type");
        Require(live.Serialize()==liveBefore && !live.IsVisible(),"rejected draft mutated live adapter");
    }
    bool unavailable=false;
    try {PreflightLayerParameters(json::array({{{"type","Unavailable"}}}),factory);}
    catch(const std::runtime_error&) {unavailable=true;}
    Require(unavailable,"unavailable canonical effect was skipped");
    Require(renderingCalls==0,"preflight accessed rendering");
    std::cout<<"Layer preflight: real Brightness valid/default/malformed fields, live preservation, zero rendering calls passed.\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
