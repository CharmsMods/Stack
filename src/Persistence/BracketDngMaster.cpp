#include "Persistence/BracketDngMaster.h"
#include <array>
#include "Raw/Internal/DngTiffReader.h"
#include <set>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace Stack::Project::DngMaster {
namespace {
// Inspect only the bounded first IFD of Stack's single-strip, little-endian
// float DNG. General photo decoding remains owned by the normal RAW loader.
struct Fields {
    nlohmann::json marker;
    unsigned width=0, height=0, strip=0, byteCount=0;
    bool floatSamples=false, uncompressed=false, bayer=false, float32=false;
};
template<typename T> T Value(std::istream& file) {
    T value{};
    if (!file.read(reinterpret_cast<char*>(&value), sizeof(value))) throw std::runtime_error("Truncated DNG header.");
    return value;
}
Fields Inspect(std::ifstream& file) {
    Fields f;
    if (Value<std::uint32_t>(file) != 0x002a4949u) return f;
    auto offset=Value<std::uint32_t>(file);
    file.seekg(offset);
    const unsigned count=Value<std::uint16_t>(file);
    if (count>512) return f;
    std::uint32_t textAt=0,textSize=0;
    for (unsigned i=0;i<count;++i) {
        const auto tag=Value<std::uint16_t>(file), type=Value<std::uint16_t>(file);
        const auto n=Value<std::uint32_t>(file), v=Value<std::uint32_t>(file);
        if (tag==270 && type==2 && n>4 && n<8*1024*1024) { textAt=v; textSize=n; }
        if (n!=1) continue;
        switch (tag) {
        case 256: f.width=v; break;
        case 257: f.height=v; break;
        case 258: f.float32=v==32; break;
        case 259: f.uncompressed=v==1; break;
        case 262: f.bayer=v==32803; break;
        case 273: f.strip=v; break;
        case 279: f.byteCount=v; break;
        case 339: f.floatSamples=v==3; break;
        }
    }
    if (!textAt || !textSize || !f.float32 || !f.uncompressed || !f.floatSamples || !f.bayer) return f;
    std::string text(textSize, 0); file.seekg(textAt);
    if (!file.read(text.data(), textSize) || text.back()!=0) return f;
    text.pop_back();
    auto m=nlohmann::json::parse(text,nullptr,false);
    if (m.is_object() && m.contains("schema") && m["schema"]=="stack.bracket.master.v1") f.marker=std::move(m);
    return f;
}
}
std::vector<Raw::DepthMask> ReadMasks(const std::filesystem::path& path, int* orientation) {
    Raw::Internal::DngTiffReader reader(path);
    std::vector<Raw::DepthMask> masks;std::set<std::string> ids;
    if(!reader.Valid())return masks;
    bool first = true;
    for(auto ifd:reader.ReadIfdTree(true)) {
        const auto entries=reader.ReadEntries(ifd);
        const auto numbers=[&](unsigned tag,std::size_t limit=1) {
            for(const auto& e:entries)if(e.tag==tag)return reader.NumberValues(e,limit);
            return std::vector<double>{};
        };
        const auto number=[&](unsigned tag){const auto v=numbers(tag);return v.empty()?0.:v.front();};
        if (first && orientation) *orientation = std::clamp(int(number(274)), 1, 8);
        first = false;
        if(number(254)!=65540 || number(262)!=52527 || number(259)!=1 || number(258)!=8)continue;
        Raw::DepthMask m;
        for(const auto& e:entries) {
            if(e.tag==52526)m.name=reader.StringValue(e);
            if(e.tag==52528)m.id=reader.StringValue(e);
        }
        const auto area=numbers(52536,4);
        if(area.size()!=4)continue;
        m.y=int(area[0]);m.x=int(area[1]);m.width=int(area[2]);m.height=int(area[3]);
        m.w=int(number(256));m.h=int(number(257));
        if(!m.Valid() || !ids.insert(m.id).second)continue;
        const auto offset=std::uint64_t(number(273)),count=std::uint64_t(number(279));
        if(count!=std::uint64_t(m.w)*m.h || offset+count>std::filesystem::file_size(path))
            throw std::runtime_error("Invalid DNG mask strip.");
        m.readPixels=[path,offset,count]{
            std::ifstream stream(path,std::ios::binary);stream.seekg(std::streamoff(offset));
            Raw::DepthMask::Bytes data(static_cast<std::size_t>(count));
            if(!stream.read(reinterpret_cast<char*>(data.data()),count))throw std::runtime_error("Could not read DNG mask.");
            return data;
        };
        masks.push_back(std::move(m));
        if(masks.size()>36)throw std::runtime_error("Too many Stack DNG masks.");
    }
    return masks;
}

nlohmann::json ReadMetadata(const std::filesystem::path& path) try {
    std::ifstream file(path,std::ios::binary);
    return Inspect(file).marker;
} catch (const std::exception&) { return {}; }

std::vector<float> Read(const std::filesystem::path& path,int width,int height,const std::string& id) {
    std::ifstream file(path,std::ios::binary);
    auto f=Inspect(file);
    if (!f.marker.is_object() || f.marker.value("resultId",std::string{})!=id ||
        width<=0 || height<=0 || f.width!=unsigned(width) || f.height!=unsigned(height) ||
        std::uint64_t(width)*height*sizeof(float)!=f.byteCount)
        throw std::runtime_error("DNG master identity or dimensions do not match.");
    file.seekg(0,std::ios::end);
    if (file.tellg()<std::streamoff(std::uint64_t(f.strip)+f.byteCount)) throw std::runtime_error("Truncated float DNG.");
    std::vector<float> samples(std::size_t(width)*height);
    file.seekg(f.strip);
    if (!file.read(reinterpret_cast<char*>(samples.data()),f.byteCount)) throw std::runtime_error("Could not read DNG samples.");
    return samples;
}
}
