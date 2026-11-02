#include "Persistence/BracketRgbMaster.h"
#include "Persistence/BracketDngMaster.h"
#include "Raw/DepthMaskManifest.h"
#include "Raw/ReconstructionDepth.h"
#include <chrono>
#include <iostream>
#include <cstring>

int main(int argc,char** argv) try {
    namespace fs=std::filesystem;
    auto root=argc>1 ? fs::u8path(argv[1]) : fs::temp_directory_path() /
        ("stack-master-roundtrip-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!fs::create_directory(root)) throw std::runtime_error("Choose an unused test directory.");
    struct Cleanup { fs::path path; ~Cleanup(){ if(!path.empty()){std::error_code ec;fs::remove_all(path,ec);} } } cleanup{argc>1?fs::path{}:root};
    nlohmann::json marker={{"schema","stack.bracket.master.v1"},{"resultId","test-result"},
        {"recipeId","test-recipe"},{"sources",{"capture-a","capture-b"}},{"orientation",6}};
    const std::vector<float> mosaic{-0.25f,0.0f,-0.0f,1.0f,8.125f,0.125f,2.0f,0.99999994f};
    Raw::RawImageData raw;
    raw.normalizedMosaicInputContract=Raw::NormalizedMosaicInputContract::BracketingPreGain;
    raw.normalizedMosaicBuffer=std::make_shared<const std::vector<float>>(mosaic);
    auto& m=raw.metadata; m.rawWidth=m.visibleWidth=4;m.rawHeight=m.visibleHeight=2;
    m.orientation=6;m.cameraMake="Stack";m.cameraModel="Fixture";m.cfaPattern=Raw::CfaPattern::RGGB;
    m.hasDngColorMatrix1=true; m.dngColorMatrix1={1,0,0,0,1,0,0,0,1};m.dngIlluminant1=21;
    Raw::DngGainMapOpcode map;
    map.right=4;map.bottom=2;map.mapPointsV=2;map.mapPointsH=2;
    map.mapSpacingV=1.;map.mapSpacingH=1.;map.gains={1.f,1.25f,1.5f,2.f};
    m.dngGainMaps.push_back(map);
    const auto require=[](bool v){ if(!v)throw std::runtime_error("Master round-trip mismatch."); };
    auto depth=std::make_shared<Raw::ReconstructionDepth>();
    Raw::DepthMask mask;mask.id=std::string(64,'a');mask.name="Near depth";mask.width=3;mask.height=2;
    mask.x=1;mask.w=2;mask.h=2;mask.pixels=std::make_shared<const Raw::DepthMask::Bytes>(Raw::DepthMask::Bytes{0,51,202,255});
    Raw::DepthMask far=mask;
    far.id=std::string(64,'b');far.name="Far depth";far.x=0;
    far.pixels=std::make_shared<const Raw::DepthMask::Bytes>(Raw::DepthMask::Bytes{255,114,63,0});
    depth->masks={mask,far};raw.reconstructionDepth=depth;marker["depthMasks"]=Raw::DepthMaskManifest(depth->masks);
    const auto checkMasks=[&](const std::vector<Raw::DepthMask>& restored) {
        require(Raw::DepthMaskManifest(restored)==marker["depthMasks"]);
        for(std::size_t i=0;i<restored.size();++i)
            require(*restored[i].Read()==*depth->masks[i].Read());
    };
    marker["contentHash"]=1;marker["mosaicHash"]=1;marker["decoderVersion"]="fixture";
    marker["cameraRgb"]=false;
    marker["metadata"]={{"cameraMake","Stack"},{"cameraModel","Fixture"},{"rawWidth",4},{"rawHeight",2},
        {"visibleWidth",4},{"visibleHeight",2},{"orientation",6},{"mosaiced",true},{"cfaPattern","RGGB"}};
    const auto dng=root/"bayer.dng";
    Stack::Project::DngMaster::Write(dng,raw,marker);
    require(Stack::Project::DngMaster::ReadMetadata(dng)==marker);
    auto fromDng=Stack::Project::DngMaster::ReadMasks(dng);
    checkMasks(fromDng);
    auto decoded=Stack::Project::DngMaster::Read(dng,4,2,"test-result");
    require(decoded.size()==mosaic.size() && std::memcmp(decoded.data(),mosaic.data(),mosaic.size()*4)==0);
    std::vector<float> rgb;
    for (const auto sample:mosaic) for (float scale:{1.f,0.5f,2.f}) rgb.push_back(sample*scale);
    marker["cameraRgb"]=true;
    marker["metadata"]["mosaiced"]=false;
    marker["metadata"]["linearChannels"]=3;
    auto exr=root/"camera.exr";
    Stack::Project::RgbMaster::Write(exr,4,2,rgb,marker,depth->masks);
    require(Stack::Project::RgbMaster::ReadMetadata(exr)==marker);
    auto fromExr=Stack::Project::RgbMaster::ReadMasks(exr,Stack::Project::RgbMaster::ReadMetadata(exr));
    checkMasks(fromExr);
    decoded=Stack::Project::RgbMaster::Read(exr,4,2,"test-result");
    require(decoded.size()==rgb.size() && std::memcmp(decoded.data(),rgb.data(),rgb.size()*4)==0);
    std::cout<<"DNG and ZIP EXR finite samples round-trip bit-for-bit, including signed zero, -0.25 and 16.25.\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
