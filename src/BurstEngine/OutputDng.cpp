// This product includes DNG technology under license by Adobe.
// DNG 1.4 float CFA, TIFF 6.0 little-endian uncompressed strips. See Adobe's
// DNG 1.7.1 specification, sections 3, 4 and 5. No source opcode is copied:
// the prepared pointwise calibration has already been applied to the samples.
#include "OutputInternal.h"
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
void WriteDng(const std::filesystem::path& path,const Image& image,const Request& request) {
    const auto& m=image.metadata;
    if(!m.hasDngColorMatrix1||!UsableMatrix(m.dngColorMatrix1))
        throw std::runtime_error("Editable DNG output requires the source camera's XYZ calibration matrix.");
    if(m.dngIlluminant1==255||m.dngIlluminant2==255)
        throw std::runtime_error("DNG output does not yet support custom spectral calibration illuminants.");
    double maximum=1;
    for(std::size_t i=0;i<image.mosaic.size();++i) {
        if((i&65535)==0)CheckCanceled(request);
        maximum=std::max(maximum,double(image.mosaic[i]));
    }
    if(maximum>1000000)throw std::runtime_error("RAW measurement range is too large for DNG output.");
    const auto white=std::uint32_t(std::ceil(maximum));
    Ifd raw,exif;
    raw.Longs(254,{0});raw.Longs(256,{image.width});raw.Longs(257,{image.height});
    raw.Shorts(258,{32});raw.Shorts(259,{1});raw.Shorts(262,{32803});
    raw.Text(270,"Stack burst denoise; calibrated floating-point camera CFA; source captures unchanged.");
    raw.Text(271,m.cameraMake);raw.Text(272,m.cameraModel);
    raw.Longs(273,{0});raw.Shorts(274,{unsigned(m.orientation>=1&&m.orientation<=8?m.orientation:1)});
    raw.Shorts(277,{1});raw.Longs(278,{image.height});
    const auto rasterBytes=std::uint64_t(image.width)*image.height*sizeof(float);
    if(rasterBytes>std::numeric_limits<std::uint32_t>::max()-65536)throw std::runtime_error("This image exceeds the supported 4 GB DNG file size.");
    raw.Longs(279,{std::uint32_t(rasterBytes)});raw.Shorts(284,{1});
    raw.Text(305,std::string("StackBurst ")+AppVersion::kVersionString);raw.Shorts(339,{3});
    raw.Shorts(33421,{2,2});raw.BytesTag(33422,1,Pattern(m.cfaPattern));raw.Longs(34665,{0});
    raw.BytesTag(50706,1,{1,4,0,0});raw.BytesTag(50707,1,{1,4,0,0});
    raw.Text(50708,m.dngUniqueCameraModel.empty()?m.cameraMake+" "+m.cameraModel:m.dngUniqueCameraModel);
    raw.BytesTag(50710,1,{0,1,2});raw.Shorts(50711,{1});raw.Shorts(50713,{1,1});raw.Rational(50714,{0});raw.Longs(50717,{white});
    raw.Rational(50718,{1,1});raw.Longs(50719,{0,0});raw.Longs(50720,{image.width,image.height});
    raw.Array(50721,m.dngColorMatrix1,true);raw.Shorts(50778,{unsigned(m.dngIlluminant1)});
    const bool second=m.hasDngColorMatrix2&&UsableMatrix(m.dngColorMatrix2)&&m.dngIlluminant1>0&&m.dngIlluminant2>0;
    if(second){raw.Array(50722,m.dngColorMatrix2,true);raw.Shorts(50779,{unsigned(m.dngIlluminant2)});}
    if(m.hasDngCameraCalibration1)raw.Array(50723,m.dngCameraCalibration1,true);
    if(second&&m.hasDngCameraCalibration2)raw.Array(50724,m.dngCameraCalibration2,true);
    if(m.hasDngAnalogBalance)raw.Array(50727,m.dngAnalogBalance);
    const auto wb=Raw::ResolveRawWhiteBalance(m,Raw::RawDevelopSettings{});
    raw.Rational(50728,{1./wb[0],1./wb[1],1./wb[2]});
    raw.Rational(50730,{request.exposureStops+(m.hasDngBaselineExposure?m.dngBaselineExposure:0)+std::log2(double(white))},true);
    raw.Longs(50829,{0,0,image.height,image.width});
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
    const auto exifOffset=8+raw.Size(),rasterOffset=exifOffset+exif.Size();
    raw.SetLong(34665,exifOffset);raw.SetLong(273,rasterOffset);
    const auto rawBytes=raw.Encode(8),exifBytes=exif.Encode(exifOffset);
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);
    stream.exceptions(std::ios::badbit|std::ios::failbit);
    const unsigned char header[]{'I','I',42,0,8,0,0,0};stream.write(reinterpret_cast<const char*>(header),8);
    stream.write(reinterpret_cast<const char*>(rawBytes.data()),rawBytes.size());stream.write(reinterpret_cast<const char*>(exifBytes.data()),exifBytes.size());
    for(unsigned y=0;y<image.height;++y) {
        if((y&31)==0)Progress(request,.15+.83*double(y)/image.height,"Writing editable DNG");
        stream.write(reinterpret_cast<const char*>(image.mosaic.data()+std::size_t(y)*image.width),std::streamsize(image.width)*sizeof(float));
    }
    stream.flush();stream.close();
}
}
