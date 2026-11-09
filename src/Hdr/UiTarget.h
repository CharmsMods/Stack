#pragma once
#include <string>
#include "Hdr/GainMapPreview.h"
#include "Hdr/PhotoPairs.h"

namespace Stack::Hdr::Ui {
// Set only around actual window drawing. Offscreen export, capture, and
// processing contexts keep their own target and transfer contract.
inline thread_local bool linearTarget = false;
class TargetScope {
public:
    explicit TargetScope(bool linear) : previous(linearTarget) {linearTarget=linear;}
    ~TargetScope() {linearTarget=previous;}
    TargetScope(const TargetScope&)=delete;
    TargetScope& operator=(const TargetScope&)=delete;
private:
    bool previous;
};

inline constexpr int TargetUniform=48;
inline constexpr int HeadroomUniform=49;
inline constexpr char DecodeShader[]=R"(
vec3 stackUiLinear(vec3 c) {
    vec3 a=abs(c);
    return sign(c)*mix(a/12.92,pow((a+.055)/1.055,vec3(2.4)),greaterThan(a,vec3(.04045)));
}
)";

// Custom UI programs supply display-encoded straight RGB. The wrapper runs
// once at shader creation. A single output epilogue also handles early returns.
inline std::string Fragment(std::string source,const char* output) {
    const auto version=source.find("#version");
    const auto versionEnd=source.find('\n',version);
    // Explicit uniform locations are core in the application's GL 4.3
    // baseline, including for callbacks originally written with GLSL 3.30.
    source.replace(version,versionEnd-version,"#version 430 core");
    const auto insertAt=source.find('\n',version)+1;
    source.insert(insertAt,std::string("layout(location=48) uniform bool stackLinearTarget;\nlayout(location=49) uniform float stackPhotoHeadroom;\n")+
        DecodeShader+GainMapPreviewShader+"\n#define main stackEncodedUiMain\n");
    source+="\n#undef main\nvoid main(){stackEncodedUiMain();if(stackLinearTarget) ";
    source+=output;
    source+=".rgb=stackUiLinear(";
    source+=output;
    source+=".rgb);if(stackLinearTarget && stackPhotoHeadroom<1000.0/203.0 && max(";
    source+=output;source+=".r,max(";source+=output;source+=".g,";source+=output;
    source+=".b))>1.0) ";source+=output;source+=".rgb=stackUnpairedHdrPreview(";
    source+=output;source+=".rgb,stackPhotoHeadroom);}\n";
    return source;
}
}
