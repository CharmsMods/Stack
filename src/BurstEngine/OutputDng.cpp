// This product includes DNG technology under license by Adobe.
// DNG 1.4 float CFA, TIFF 6.0 little-endian uncompressed strips. See Adobe's
// DNG 1.7.1 specification, sections 3, 4 and 5. No source opcode is copied:
// the prepared pointwise calibration has already been applied to the samples.
#include "OutputInternal.h"
#include "Raw/ReconstructionDepth.h"
#include "Persistence/BracketDngMaster.h"
#include "Raw/RawColorTransform.h"
#include "App/AppVersion.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace Stack::BurstEngine::Output::Internal {
namespace {
using Bytes=std::vector<std::uint8_t>;
void U16(Bytes& out,std::uint16_t value){out.push_back(value&255);out.push_back(value>>8);}
void U32(Bytes& out,std::uint32_t value){U16(out,value&65535);U16(out,value>>16);}
struct Entry {std::uint16_t tag,type;std::uint32_t count;Bytes bytes;};
class Ifd {
public:
    std::vector<Entry> entries;
    void BytesTag(std::uint16_t tag,std::uint16_t type,const Bytes& bytes){entries.push_back({tag,type,std::uint32_t(bytes.size()),bytes});}
    void Text(std::uint16_t tag,const std::string& text){Bytes value(text.begin(),text.end());value.push_back(0);BytesTag(tag,2,value);}
    void Shorts(std::uint16_t tag,std::initializer_list<unsigned> values){Entry e{tag,3,std::uint32_t(values.size()),{}};for(auto v:values)U16(e.bytes,std::uint16_t(v));entries.push_back(std::move(e));}
    void Longs(std::uint16_t tag,std::initializer_list<std::uint32_t> values){Entry e{tag,4,std::uint32_t(values.size()),{}};for(auto v:values)U32(e.bytes,v);entries.push_back(std::move(e));}
    void Rational(std::uint16_t tag,const std::vector<double>& values,bool signedValue=false) {
        Entry e{tag,std::uint16_t(signedValue?10:5),std::uint32_t(values.size()),{}};
        for(double value:values) {
            const double limit=signedValue?double(std::numeric_limits<std::int32_t>::max()):double(std::numeric_limits<std::uint32_t>::max());
            if(!std::isfinite(value)||std::abs(value)>limit||(!signedValue&&value<0))throw std::runtime_error("DNG calibration metadata is invalid.");
            const auto denominator=std::uint32_t(std::max(1.,std::min(1000000000.,std::floor(limit/std::max(1.,std::abs(value))))));
            const auto numerator=std::int64_t(std::llround(value*denominator));
            U32(e.bytes,std::uint32_t(numerator));U32(e.bytes,denominator);
        }
        entries.push_back(std::move(e));
    }
    template<std::size_t N> void Array(std::uint16_t tag,const std::array<float,N>& values,bool signedValue=false){Rational(tag,std::vector<double>(values.begin(),values.end()),signedValue);}
    std::uint32_t Size() const {std::uint32_t size=2+std::uint32_t(entries.size())*12+4;for(const auto& e:entries)if(e.bytes.size()>4)size+=std::uint32_t((e.bytes.size()+1)&~std::size_t(1));return size;}
    Bytes Encode(std::uint32_t offset) {
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.tag<b.tag;});
        Bytes main,extra;U16(main,std::uint16_t(entries.size()));
        const auto extraOffset=offset+2+std::uint32_t(entries.size())*12+4;
        for(const auto& e:entries) {
            U16(main,e.tag);U16(main,e.type);U32(main,e.count);
            if(e.bytes.size()<=4){main.insert(main.end(),e.bytes.begin(),e.bytes.end());main.resize(main.size()+4-e.bytes.size());}
            else {U32(main,extraOffset+std::uint32_t(extra.size()));extra.insert(extra.end(),e.bytes.begin(),e.bytes.end());if(extra.size()&1)extra.push_back(0);}
        }
        U32(main,0);main.insert(main.end(),extra.begin(),extra.end());return main;
    }
    void SetLong(std::uint16_t tag,std::uint32_t value){for(auto& e:entries)if(e.tag==tag){e.bytes.clear();U32(e.bytes,value);return;}throw std::runtime_error("Missing output TIFF field.");}
};
bool UsableMatrix(const std::array<float,9>& m) {
    for(float v:m)if(!std::isfinite(v))return false;
    const double det=m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
    return std::abs(det)>1e-9;
}
Bytes Pattern(Raw::CfaPattern pattern) {
    switch(pattern){case Raw::CfaPattern::RGGB:return {0,1,1,2};case Raw::CfaPattern::BGGR:return {2,1,1,0};case Raw::CfaPattern::GRBG:return {1,0,2,1};case Raw::CfaPattern::GBRG:return {1,2,0,1};default:throw std::runtime_error("Unsupported DNG Bayer pattern.");}
}
}
namespace {
template<class T> void BigEndian(Bytes& out, T value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    for (std::size_t n = sizeof(T); n; --n) out.push_back(bytes[n-1]);
}
Bytes GainOpcodes(const Raw::RawMetadata& m) {
    Bytes out;
    BigEndian(out, static_cast<std::uint32_t>(m.dngGainMaps.size()));
    for (const auto& map : m.dngGainMaps) {
        if (map.mapPointsV <= 0 || map.mapPointsH <= 0 || map.mapPlanes <= 0 ||
            std::uint64_t(map.mapPointsV)*map.mapPointsH*map.mapPlanes != map.gains.size() ||
            map.gains.size() > (UINT32_MAX - 76u)/4u)
            throw std::runtime_error("Invalid bracket gain map.");
        BigEndian(out, 9u); BigEndian(out, 0x01030000u); BigEndian(out, 0u);
        BigEndian(out, static_cast<std::uint32_t>(76 + map.gains.size()*4));
        for (int v : {map.top,map.left,map.bottom,map.right,map.plane,map.planes,
                map.rowPitch,map.colPitch,map.mapPointsV,map.mapPointsH}) BigEndian(out,v);
        for (double v : {map.mapSpacingV,map.mapSpacingH,map.mapOriginV,map.mapOriginH}) BigEndian(out,v);
        BigEndian(out,map.mapPlanes);
        for (float v : map.gains) BigEndian(out,v);
    }
    return out;
}
}
void WriteDngSamples(const std::filesystem::path& path,const Raw::RawMetadata& m,
    unsigned width,unsigned height,const std::vector<float>& mosaic,const Request& request,
    const std::string& masterMetadata,bool rgb,const std::vector<Raw::DepthMask>& masks) {
    const unsigned channels=rgb?3:1;
    if (!width || !height || std::uint64_t(width)*height*channels != mosaic.size())
        throw std::runtime_error("Invalid RAW raster dimensions.");
    if(!m.hasDngColorMatrix1||!UsableMatrix(m.dngColorMatrix1))
        throw std::runtime_error("Editable DNG output requires the source camera's XYZ calibration matrix.");
    if(m.dngIlluminant1==255||m.dngIlluminant2==255)
        throw std::runtime_error("DNG output does not yet support custom spectral calibration illuminants.");
    double maximum=1;
    for(std::size_t i=0;i<mosaic.size();++i) {
        if((i&65535)==0)CheckCanceled(request);
        if (!std::isfinite(mosaic[i])) throw std::runtime_error("Non-finite Bayer measurement.");
        maximum=std::max(maximum,double(mosaic[i]));
    }
    if(maximum>1000000)throw std::runtime_error("RAW measurement range is too large for DNG output.");
    const auto white=std::uint32_t(std::ceil(maximum));
    Ifd raw,exif;
    raw.Longs(254,{0});raw.Longs(256,{width});raw.Longs(257,{height});
    if(rgb)raw.Shorts(258,{32,32,32});else raw.Shorts(258,{32});
    raw.Shorts(259,{1});raw.Shorts(262,{rgb?34892u:32803u});
    raw.Text(270,masterMetadata.empty() ? (rgb?"Stack burst reconstructed linear camera RGB.":"Stack burst denoise; calibrated floating-point camera CFA; source captures unchanged.") : masterMetadata);
    if (!masterMetadata.empty() && !m.dngGainMaps.empty())
        raw.BytesTag(51009,7,GainOpcodes(m));
    raw.Text(271,m.cameraMake);raw.Text(272,m.cameraModel);
    raw.Longs(273,{0});raw.Shorts(274,{unsigned(m.orientation>=1&&m.orientation<=8?m.orientation:1)});
    raw.Shorts(277,{channels});raw.Longs(278,{height});
    const auto rasterBytes=std::uint64_t(width)*height*channels*sizeof(float);
    if(rasterBytes>std::numeric_limits<std::uint32_t>::max()-65536)throw std::runtime_error("This image exceeds the supported 4 GB DNG file size.");
    raw.Longs(279,{std::uint32_t(rasterBytes)});raw.Shorts(284,{1});
    raw.Text(305,std::string("StackBurst ")+AppVersion::kVersionString);
    if(rgb)raw.Shorts(339,{3,3,3});else raw.Shorts(339,{3});
    if(!rgb){raw.Shorts(33421,{2,2});raw.BytesTag(33422,1,Pattern(m.cfaPattern));}
    raw.Longs(34665,{0});
    raw.BytesTag(50706,1,{1,std::uint8_t(masks.empty()?4:6),0,0});raw.BytesTag(50707,1,{1,4,0,0});
    raw.Text(50708,m.dngUniqueCameraModel.empty()?m.cameraMake+" "+m.cameraModel:m.dngUniqueCameraModel);
    if(!rgb){raw.BytesTag(50710,1,{0,1,2});raw.Shorts(50711,{1});}
    raw.Shorts(50713,{1,1});
    if(rgb){raw.Rational(50714,{0,0,0});raw.Longs(50717,{white,white,white});}
    else {raw.Rational(50714,{0});raw.Longs(50717,{white});}
    raw.Rational(50718,{1,1});raw.Longs(50719,{0,0});raw.Longs(50720,{width,height});
    raw.Array(50721,m.dngColorMatrix1,true);raw.Shorts(50778,{unsigned(m.dngIlluminant1)});
    const bool second=m.hasDngColorMatrix2&&UsableMatrix(m.dngColorMatrix2)&&m.dngIlluminant1>0&&m.dngIlluminant2>0;
    if(second){raw.Array(50722,m.dngColorMatrix2,true);raw.Shorts(50779,{unsigned(m.dngIlluminant2)});}
    if(m.hasDngCameraCalibration1)raw.Array(50723,m.dngCameraCalibration1,true);
    if(second&&m.hasDngCameraCalibration2)raw.Array(50724,m.dngCameraCalibration2,true);
    if(m.hasDngAnalogBalance)raw.Array(50727,m.dngAnalogBalance);
    const auto wb=Raw::ResolveRawWhiteBalance(m,Raw::RawDevelopSettings{});
    raw.Rational(50728,{1./wb[0],1./wb[1],1./wb[2]});
    raw.Rational(50730,{request.exposureStops+(m.hasDngBaselineExposure?m.dngBaselineExposure:0)+std::log2(double(white))},true);
    raw.Longs(50829,{0,0,height,width});
    // NoiseProfile from a single source is not the noise model of a fused result.
    if(m.hasDngForwardMatrix1)raw.Array(50964,m.dngForwardMatrix1,true);
    else if(!m.isDng&&m.hasCameraMatrix) {
        // LibRaw's rgb_cam maps white-balanced camera values to linear sRGB.
        // DNG ForwardMatrix maps the same white-balanced reference values to
        // D50 XYZ; keep that rendering transform alongside real cam_xyz.
        constexpr std::array<float,9> srgbToD50{
            .4360747f,.3850649f,.1430804f,
            .2225045f,.7168786f,.0606169f,
            .0139322f,.0971045f,.7141733f};
        std::array<float,9> forward{};
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)for(unsigned k=0;k<3;++k)
            forward[r*3+c]+=srgbToD50[r*3+k]*m.cameraToSrgb[k*3+c];
        raw.Array(50964,forward,true);
    }
    if(second&&m.hasDngForwardMatrix2)raw.Array(50965,m.dngForwardMatrix2,true);
    exif.BytesTag(36864,7,{'0','2','3','2'});
    if(m.hasExposureTime&&m.exposureTimeSeconds>0)exif.Rational(33434,{m.exposureTimeSeconds});
    if(m.hasApertureFNumber&&m.apertureFNumber>0)exif.Rational(33437,{m.apertureFNumber});
    if(m.hasIsoSpeed&&m.isoSpeed>0)exif.Shorts(34855,{unsigned(std::clamp(m.isoSpeed,1.f,65535.f))});
    if(m.hasFocalLength&&m.focalLengthMm>0)exif.Rational(37386,{m.focalLengthMm});
    if(!m.lensModel.empty())exif.Text(42036,m.lensModel);
    // DNG 1.6 semantic-mask SubIFDs, stored at their native sampling and crop.
    std::vector<Ifd> maskDirs;
    for(const auto& mask:masks) {
        if(!mask.Valid())throw std::runtime_error("Invalid DNG depth mask.");
        Ifd dir;
        dir.Longs(254,{65540});dir.Longs(256,{unsigned(mask.w)});dir.Longs(257,{unsigned(mask.h)});
        dir.Shorts(258,{8});dir.Shorts(259,{1});dir.Shorts(262,{52527});dir.Longs(273,{0});
        dir.Shorts(274,{unsigned(std::clamp(m.orientation,1,8))});dir.Shorts(277,{1});dir.Longs(278,{unsigned(mask.h)});
        dir.Longs(279,{unsigned(mask.w*mask.h)});dir.Shorts(284,{1});dir.Text(52526,mask.name);dir.Text(52528,mask.id);
        dir.Longs(52536,{unsigned(mask.y),unsigned(mask.x),unsigned(mask.width),unsigned(mask.height)});
        maskDirs.push_back(std::move(dir));
    }
    if(!masks.empty())raw.entries.push_back({330,4,std::uint32_t(masks.size()),Bytes(masks.size()*4)});
    const auto exifOffset=8+raw.Size();
    std::uint32_t rasterOffset=exifOffset+exif.Size();
    Bytes ifdOffsets;
    for(const auto& dir:maskDirs){U32(ifdOffsets,rasterOffset);rasterOffset+=dir.Size();}
    for(auto& entry:raw.entries)if(entry.tag==330)entry.bytes=ifdOffsets;
    std::uint64_t end=std::uint64_t(rasterOffset)+rasterBytes;
    for(std::size_t i=0;i<masks.size();++i) {
        end=(end+1)&~std::uint64_t(1);
        if(end+std::uint64_t(masks[i].w)*masks[i].h>UINT32_MAX)
            throw std::runtime_error("Depth masks exceed the classic DNG file-size limit.");
        maskDirs[i].SetLong(273,static_cast<std::uint32_t>(end));end+=std::uint64_t(masks[i].w)*masks[i].h;
    }
    raw.SetLong(34665,exifOffset);raw.SetLong(273,rasterOffset);
    const auto rawBytes=raw.Encode(8),exifBytes=exif.Encode(exifOffset);
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);
    stream.exceptions(std::ios::badbit|std::ios::failbit);
    const unsigned char header[]{'I','I',42,0,8,0,0,0};stream.write(reinterpret_cast<const char*>(header),8);
    stream.write(reinterpret_cast<const char*>(rawBytes.data()),rawBytes.size());stream.write(reinterpret_cast<const char*>(exifBytes.data()),exifBytes.size());
    std::uint32_t directoryOffset=exifOffset+exif.Size();
    for(auto& dir:maskDirs) {
        auto bytes=dir.Encode(directoryOffset);stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        directoryOffset+=dir.Size();
    }
    for(unsigned y=0;y<height;++y) {
        if((y&31)==0)Progress(request,.15+.83*double(y)/height,"Writing editable DNG");
        stream.write(reinterpret_cast<const char*>(mosaic.data()+std::size_t(y)*width*channels),std::streamsize(width)*channels*sizeof(float));
    }
    for(const auto& mask:masks) {
        CheckCanceled(request);
        if(std::streamoff(stream.tellp())%2)stream.put(0);
        auto bytes=mask.Read();stream.write(reinterpret_cast<const char*>(bytes->data()),bytes->size());
    }
    stream.flush();stream.close();
}
void WriteDng(const std::filesystem::path& path,const Image& image,const Request& request) {
    WriteDngSamples(path,image.metadata,image.width,image.height,image.reconstructed?*image.reconstructed:image.mosaic,request,{},image.reconstructed!=nullptr, image.depth?image.depth->masks:std::vector<Raw::DepthMask>{});
}
}

namespace Stack::Project::DngMaster {
void Write(const std::filesystem::path& path, const Raw::RawImageData& raw, const nlohmann::json& marker) {
    if (!raw.normalizedMosaicBuffer || raw.normalizedMosaicInputContract != Raw::NormalizedMosaicInputContract::BracketingPreGain)
        throw std::runtime_error("Expected a pre-gain Bayer bracket result.");
    // Keep the exact engine samples. A non-unity pointwise calibration is
    // represented by DNG GainMap opcodes; external DNG development applies it
    // once, and Stack can restore the pre-gain measurement without roundoff.
    BurstEngine::Output::Internal::WriteDngSamples(path,raw.metadata,raw.metadata.rawWidth,raw.metadata.rawHeight,
        *raw.normalizedMosaicBuffer,{},marker.dump(),false,raw.reconstructionDepth?raw.reconstructionDepth->masks:std::vector<Raw::DepthMask>{});
}
}
