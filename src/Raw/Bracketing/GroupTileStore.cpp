#include "GroupTileStore.h"
#include "NodeMath/ContractTypes.h"
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <random>

namespace Raw::Bracketing {
namespace {
struct PackedObservation {
    float value,variance,headroom,support,fallback,exposure,measurement,uncertainty;
    std::uint32_t flags;
};
static_assert(sizeof(PackedObservation)==36);
struct Header {
    std::array<char,8> magic={'S','B','G','R','P','0','0','3'};
    std::uint32_t width=0,height=0,groups=0,reserved=0;
    std::uint64_t checksum=0;
};
std::filesystem::path TilePath(const ProcessingRequest& request,const PreparedDataset& data,
    unsigned tx,unsigned ty) {
    const auto& origin=data.sources[data.origin].frame;
    const auto identity=Stack::NodeMath::Sha256ContentIdentity(AnalysisIdentity(request)+
        "|motion="+data.motionIdentity+"|tile="+std::to_string(origin.tileRawPixels)+"|prepared="+origin.cacheKey);
    return data.directory/"group-measurements-v3"/identity.substr(7)/
        (std::to_string(tx)+"-"+std::to_string(ty)+"-temporal.bin");
}
bool Dimensions(const ProcessingRequest& request,const PreparedDataset& data,unsigned tx,unsigned ty,
    unsigned& width,unsigned& height,std::size_t& count) {
    const auto tile=data.sources[data.origin].frame.tileRawPixels;
    if(!tile||tx>=data.sources[data.origin].frame.tileColumns||ty>=data.sources[data.origin].frame.tileRows) return false;
    width=std::min<unsigned>(tile,data.width-tx*tile);height=std::min<unsigned>(tile,data.height-ty*tile);
    const std::uint64_t samples=static_cast<std::uint64_t>(width)*height*request.recipe.groups.size();
    if(!samples||samples>request.memoryBudgetBytes/sizeof(PackedObservation)) return false;
    count=static_cast<std::size_t>(samples);return true;
}
std::uint64_t Checksum(const std::vector<PackedObservation>& values) {
    std::uint64_t hash=14695981039346656037ull;
    const auto* bytes=reinterpret_cast<const unsigned char*>(values.data());
    for(std::size_t i=0;i<values.size()*sizeof(PackedObservation);++i) {hash^=bytes[i];hash*=1099511628211ull;}
    return hash;
}
}
void CanonicalizeGroupTile(std::vector<std::vector<Observation>>& groups) {
    for(auto& group:groups) for(auto& o:group) {
        o.value=float(o.value);o.variance=float(o.variance);o.headroom=float(o.headroom);
        o.support=float(o.support);o.fallback=float(o.fallback);o.exposure=float(o.exposure);
        o.measurementVariance=float(o.measurementVariance);o.uncertaintyVariance=float(o.uncertaintyVariance);
    }
}
bool ReadGroupTile(const ProcessingRequest& request,const PreparedDataset& data,
    unsigned tx,unsigned ty,std::vector<std::vector<Observation>>& groups) {
    unsigned width=0,height=0;std::size_t count=0;
    if(!Dimensions(request,data,tx,ty,width,height,count)) return false;
    const auto path=TilePath(request,data,tx,ty);
    std::error_code filesystemError;
    if(std::filesystem::file_size(path,filesystemError)!=sizeof(Header)+count*sizeof(PackedObservation)||filesystemError) return false;
    std::ifstream stream(path,std::ios::binary);Header header;
    const auto magic=header.magic;
    if(!stream.read(reinterpret_cast<char*>(&header),sizeof(header))||header.magic!=magic||
        header.width!=width||header.height!=height||header.groups!=request.recipe.groups.size()||header.reserved!=0) return false;
    std::vector<PackedObservation> packed(count);
    if(!stream.read(reinterpret_cast<char*>(packed.data()),packed.size()*sizeof(PackedObservation))||
        Checksum(packed)!=header.checksum) return false;
    const std::size_t pixels=static_cast<std::size_t>(width)*height;
    groups.assign(header.groups,std::vector<Observation>(pixels));
    for(std::size_t i=0;i<count;++i) {
        const auto& p=packed[i];
        if(!std::isfinite(p.value)||!std::isfinite(p.variance)||p.variance<0||
           !std::isfinite(p.support)||p.support<0||!std::isfinite(p.measurement)||
           (p.support>0&&p.measurement<0)||!std::isfinite(p.uncertainty)||p.uncertainty<0||
           !std::isfinite(p.headroom)||p.headroom<0||p.headroom>1||
           !std::isfinite(p.fallback)||!std::isfinite(p.exposure)||p.exposure<0||p.flags>31) return false;
        groups[i/pixels][i%pixels]={p.value,p.variance,p.headroom,p.support,p.fallback,p.exposure,
            bool(p.flags&1),bool(p.flags&2),bool(p.flags&4),bool(p.flags&8),p.measurement,p.uncertainty,bool(p.flags&16)};
    }
    return true;
}
bool WriteGroupTile(const ProcessingRequest& request,const PreparedDataset& data,
    unsigned tx,unsigned ty,const std::vector<std::vector<Observation>>& groups,std::string& error) {
    unsigned width=0,height=0;std::size_t count=0;
    if(!Dimensions(request,data,tx,ty,width,height,count)||groups.size()!=request.recipe.groups.size()) {
        error="Invalid group measurement tile.";return false;
    }
    const std::size_t pixels=static_cast<std::size_t>(width)*height;
    std::vector<PackedObservation> packed;packed.reserve(count);
    for(const auto& group:groups) {
        if(group.size()!=pixels) {error="Invalid group measurement tile size.";return false;}
        for(const auto& o:group) packed.push_back({float(o.value),float(o.variance),float(o.headroom),float(o.support),
            float(o.fallback),float(o.exposure),float(o.measurementVariance),float(o.uncertaintyVariance),
            unsigned(o.finite)|(unsigned(o.clipped)<<1)|(unsigned(o.localRejected)<<2)|(unsigned(o.fixedReference)<<3)|
            (unsigned(o.fallbackClipped)<<4)});
    }
    Header header;header.width=width;header.height=height;header.groups=static_cast<unsigned>(groups.size());
    header.checksum=Checksum(packed);
    const auto path=TilePath(request,data,tx,ty);
    static std::atomic<std::uint64_t> sequence{(static_cast<std::uint64_t>(std::random_device{}())<<32)^std::random_device{}()};
    const auto temporary=std::filesystem::path(path.string()+".tmp-"+std::to_string(sequence.fetch_add(1)));
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(),filesystemError);
    if(filesystemError) {error="The group cache directory could not be created.";return false;}
    {
        std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
        if(!stream.write(reinterpret_cast<const char*>(&header),sizeof(header))||
           !stream.write(reinterpret_cast<const char*>(packed.data()),packed.size()*sizeof(PackedObservation))) {
            stream.close();std::filesystem::remove(temporary,filesystemError);
            error="The group measurement cache could not be written.";return false;
        }
    }
    if(request.shouldCancel&&request.shouldCancel()) {
        std::filesystem::remove(temporary,filesystemError);error="Canceled";return false;
    }
    std::filesystem::rename(temporary,path,filesystemError);
    if(filesystemError) {
        // Another request may have published the same immutable analysis tile.
        // Accept only a complete verified file, otherwise rebuild the corrupt one.
        std::vector<std::vector<Observation>> existing;
        if(ReadGroupTile(request,data,tx,ty,existing)) {
            std::filesystem::remove(temporary,filesystemError);return true;
        }
        std::filesystem::remove(path,filesystemError);filesystemError.clear();
        std::filesystem::rename(temporary,path,filesystemError);
    }
    if(filesystemError) {
        std::filesystem::remove(temporary,filesystemError);
        error="The group measurement cache could not be published.";return false;
    }
    return true;
}
}
