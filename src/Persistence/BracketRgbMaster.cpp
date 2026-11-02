#include "Persistence/BracketRgbMaster.h"
#include "Raw/DepthMaskManifest.h"
#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfInputFile.h>
#include <ImfOutputFile.h>
#include <ImfStdIO.h>
#include <ImfStringAttribute.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace Stack::Project::RgbMaster {
namespace {
constexpr char markerName[] = "stackBracket";
constexpr char colorDomain[] = "linear camera RGB before white balance";
std::size_t Pixels(int width, int height) {
    if (width <= 0 || height <= 0 ||
        static_cast<std::uint64_t>(width) * height > std::numeric_limits<std::size_t>::max() / (3 * sizeof(float)))
        throw std::runtime_error("Invalid bracket master dimensions.");
    return static_cast<std::size_t>(width) * height;
}
Imf::FrameBuffer Buffer(float* rgb, int width) {
    Imf::FrameBuffer frame;
    constexpr const char* names[] = {"R", "G", "B"};
    for (int i = 0; i < 3; ++i)
        frame.insert(names[i], Imf::Slice(Imf::FLOAT, reinterpret_cast<char*>(rgb + i),
            sizeof(float) * 3, sizeof(float) * 3 * static_cast<std::size_t>(width)));
    return frame;
}
std::string MaskChannel(const Raw::DepthMask& mask) {
    auto name=mask.name;for(auto& c:name)if(c==' ' || c=='/')c='_';
    return "mask."+name+"."+mask.id+".Y";
}
int Anchor(int index,int grid,int extent) {
    return std::min(extent-1,int((std::int64_t(2*index+1)*extent)/(2*grid)));
}
struct Axis {int lo,hi;float mix;};
Axis SampleAxis(int p,int grid,int extent) {
    int lo=std::clamp(int((p+.5)*grid/extent-.5),0,grid-1);
    if(lo>0 && Anchor(lo,grid,extent)>p)--lo;
    const int hi=std::min(grid-1,lo+1),a=Anchor(lo,grid,extent),b=Anchor(hi,grid,extent);
    return {lo,hi,a==b?0.f:std::clamp(float(p-a)/(b-a),0.f,1.f)};
}
nlohmann::json Marker(const Imf::Header& header) {
    const auto* attr = header.findTypedAttribute<Imf::StringAttribute>(markerName);
    if (!attr || attr->value().size() > 8 * 1024 * 1024) return {};
    auto marker = nlohmann::json::parse(attr->value(), nullptr, false);
    if (!marker.is_object() || !marker.contains("schema") || marker["schema"] != "stack.bracket.master.v1" ||
        !marker.contains("resultId") || !marker["resultId"].is_string()) return {};
    return marker;
}
}

void Write(const std::filesystem::path& path, int width, int height,
    const std::vector<float>& rgb, const nlohmann::json& metadata,const std::vector<Raw::DepthMask>& masks) {
    if (Pixels(width, height) * 3 != rgb.size() ||
        !std::all_of(rgb.begin(), rgb.end(), [](float v){ return std::isfinite(v); }))
        throw std::runtime_error("Invalid reconstructed RGB measurement.");
    Imf::Header header(width, height);
    header.compression() = Imf::ZIP_COMPRESSION;
    for (const auto* name : {"R", "G", "B"}) header.channels().insert(name, Imf::Channel(Imf::FLOAT));
    auto marker=metadata;
    if(!masks.empty())marker["depthMasks"]=Raw::DepthMaskManifest(masks);
    for(const auto& m:masks)header.channels().insert(MaskChannel(m),Imf::Channel(Imf::FLOAT));
    header.insert(markerName, Imf::StringAttribute(marker.dump()));
    header.insert("stackColorInterpretation", Imf::StringAttribute(colorDomain));
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) throw std::runtime_error("Could not create EXR master.");
    {
        Imf::StdOFStream stream(file, "Stack RGB bracket");
        Imf::OutputFile out(stream, header);
        // OpenEXR's Slice API is mutable even on a write-only OutputFile.
        auto frame=Buffer(const_cast<float*>(rgb.data()),width);
        std::vector<std::vector<float>> rows(masks.size(),std::vector<float>(width));
        std::vector<std::shared_ptr<const Raw::DepthMask::Bytes>> coverage;
        std::vector<Axis> columns;
        if(!masks.empty())for(int x=0;x<width;++x)columns.push_back(SampleAxis(x,masks.front().width,width));
        for(std::size_t i=0;i<masks.size();++i) {
            if(masks[i].width!=masks.front().width || masks[i].height!=masks.front().height ||
                masks[i].width>width || masks[i].height>height)throw std::runtime_error("Invalid EXR mask grid.");
            coverage.push_back(masks[i].Read());
            frame.insert(MaskChannel(masks[i]),Imf::Slice(Imf::FLOAT,reinterpret_cast<char*>(rows[i].data()),sizeof(float),0));
        }
        out.setFrameBuffer(frame);
        if(masks.empty())out.writePixels(height);
        else for(int y=0;y<height;++y) {
            const auto sy=SampleAxis(y,masks.front().height,height);
            for(std::size_t i=0;i<masks.size();++i){
                const auto& m=masks[i];const auto& bytes=*coverage[i];
                for(int x=0;x<width;++x) {
                    const auto sx=columns[x];
                    const float a=m.At(bytes,sx.lo,sy.lo)*(1-sx.mix)+m.At(bytes,sx.hi,sy.lo)*sx.mix;
                    const float b=m.At(bytes,sx.lo,sy.hi)*(1-sx.mix)+m.At(bytes,sx.hi,sy.hi)*sx.mix;
                    rows[i][x]=(a*(1-sy.mix)+b*sy.mix)/255.f;
                }
            }
            out.writePixels(1);
        }
    }
    file.close();
    if (!file) throw std::runtime_error("Could not finish writing EXR master.");
}

nlohmann::json ReadMetadata(const std::filesystem::path& path) try {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    Imf::StdIFStream stream(file, "Stack RGB bracket");
    Imf::InputFile in(stream);
    return Marker(in.header());
} catch (const std::exception&) { return {}; }

std::vector<Raw::DepthMask> ReadMasks(const std::filesystem::path& path,const nlohmann::json& metadata) {
    if(!metadata.contains("depthMasks"))return {};
    auto list=Raw::ReadDepthMaskManifest(metadata.at("depthMasks"));
    for(auto& m:list) {
        m.readPixels=[path,desc=m,id=metadata.at("resultId").get<std::string>()]{
            std::ifstream file(path,std::ios::binary);Imf::StdIFStream stream(file,"Stack depth mask");Imf::InputFile in(stream);
            auto& header=in.header();const auto bounds=header.dataWindow();const auto channel=MaskChannel(desc);
            const auto* format=header.channels().findChannel(channel);
            if(Marker(header).value("resultId",std::string{})!=id || !format || format->type!=Imf::FLOAT ||
                format->xSampling!=1 || format->ySampling!=1 || bounds.min.x!=0 || bounds.min.y!=0 ||
                bounds.max.x<desc.width-1 || bounds.max.y<desc.height-1 || bounds.max.x>=65536 || bounds.max.y>=65536)
                throw std::runtime_error("EXR depth mask source is unavailable.");
            const int w=bounds.max.x+1,h=bounds.max.y+1;
            std::vector<float> row(w);Raw::DepthMask::Bytes bytes(std::size_t(desc.w)*desc.h);
            Imf::FrameBuffer frame;frame.insert(channel,Imf::Slice(Imf::FLOAT,reinterpret_cast<char*>(row.data()),sizeof(float),0));
            in.setFrameBuffer(frame);
            for(int y=0;y<desc.h;++y) {
                in.readPixels(Anchor(y+desc.y,desc.height,h));
                for(int x=0;x<desc.w;++x) {
                    const auto v=row[Anchor(x+desc.x,desc.width,w)];
                    if(!std::isfinite(v)||v<0 || v>1)throw std::runtime_error("Invalid EXR mask coverage.");
                    bytes[std::size_t(y)*desc.w+x]=static_cast<std::uint8_t>(std::lround(255*v));
                }
            }
            return bytes;
        };
    }
    return list;
}

std::vector<float> Read(const std::filesystem::path& path, int width, int height,
    const std::string& expectedResultId) {
    std::ifstream file(path, std::ios::binary);
    Imf::StdIFStream stream(file, "Stack RGB bracket");
    Imf::InputFile input(stream);
    const auto& header = input.header();
    const auto& bounds = header.dataWindow();
    const auto marker = Marker(header);
    if (!marker.is_object() || marker["resultId"] != expectedResultId ||
        bounds.min.x != 0 || bounds.min.y != 0 || bounds.max.x != width-1 || bounds.max.y != height-1)
        throw std::runtime_error("EXR master identity or dimensions do not match the project.");
    for (const auto* name : {"R", "G", "B"}) {
        const auto* channel = header.channels().findChannel(name);
        if (!channel || channel->type != Imf::FLOAT || channel->xSampling != 1 || channel->ySampling != 1)
            throw std::runtime_error("Incomplete float RGB channels in bracket master.");
    }
    std::vector<float> rgb(Pixels(width,height)*3);
    input.setFrameBuffer(Buffer(rgb.data(), width));
    input.readPixels(0,height-1);
    return rgb;
}
}
