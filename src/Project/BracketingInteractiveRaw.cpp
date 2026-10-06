#include "BracketingState.h"

namespace Stack::Project {
std::shared_ptr<const Raw::RawImageData> MakeBracketingInteractiveRaw(const BracketingState& ui) {
    const auto& p=ui.preview;
    if(!ui.result||!ui.result->raw||!p.width||!p.height||p.resultRgb.size()!=std::size_t(p.width)*p.height*3)return {};
    auto raw=std::make_shared<Raw::RawImageData>();
    raw->metadata=ui.result->raw->metadata;
    auto& m=raw->metadata;
    m.rawWidth=m.visibleWidth=p.width;m.rawHeight=m.visibleHeight=p.height;
    m.leftMargin=m.topMargin=0;
    m.pixelLayout=Raw::RawPixelLayout::LinearRgb;m.mosaiced=false;m.cfaPattern=Raw::CfaPattern::Unknown;
    m.linearChannels=3;m.linearSampleFormat=Raw::RawSampleFormat::Float32;
    m.dngActiveArea={0,0,int(p.height),int(p.width)};
    m.dngGainMaps.clear();m.dngGainMapCount=0;
    m.dngLinearizationTable.clear();m.dngBlackLevelValues.clear();
    m.dngBlackLevelDeltaH.clear();m.dngBlackLevelDeltaV.clear();
    m.blackLevel=0;m.perChannelBlack={0,0,0,0};m.whiteLevel=1;
    raw->reconstructedCameraRgb=true;
    raw->linearFloatBuffer=p.resultRgb;
    // This identity belongs only to an editor preview. It is never adopted as
    // the project's result and BuildGraphSnapshot/export retain the full input.
    raw->contentIdentity="bracket-interactive:"+ui.editedRecipe;
    std::uint64_t hash=14695981039346656037ull;
    for(unsigned char c:raw->contentIdentity){hash^=c;hash*=1099511628211ull;}
    raw->contentIdentityHash=hash;
    m.sourceContentSha256=std::to_string(hash);m.sourceByteSize=raw->linearFloatBuffer.size()*sizeof(float);
    return raw;
}
}
