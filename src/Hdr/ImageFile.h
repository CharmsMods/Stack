#pragma once
#include <cstdint>
#include <vector>

namespace Stack::Hdr {
enum class FileFormat { SdrPng, GainMapJpeg, PqPng, SdrJpeg };
inline bool IsHdr(FileFormat format) { return format==FileFormat::GainMapJpeg || format==FileFormat::PqPng; }
inline bool IsJpeg(FileFormat format) { return format==FileFormat::GainMapJpeg || format==FileFormat::SdrJpeg; }
inline const char* Extension(FileFormat format) { return IsJpeg(format) ? ".jpg" : ".png"; }
inline constexpr FileFormat ExportFormats[]={FileFormat::GainMapJpeg,FileFormat::SdrJpeg,FileFormat::SdrPng,FileFormat::PqPng};
inline const char* FormatName(FileFormat format) {
    switch(format) {
    case FileFormat::GainMapJpeg: return "HDR JPEG";
    case FileFormat::SdrJpeg: return "SDR JPEG";
    case FileFormat::SdrPng: return "SDR PNG";
    case FileFormat::PqPng: return "HDR PNG (advanced)";
    }
    return "Unknown";
}
std::vector<unsigned char> EncodeSdrJpeg(int width,int height,const std::vector<unsigned char>& rgba);
// Upright rows, straight alpha, linear Rec.709/D65 with diffuse white at one.
struct Image {
    int width=0,height=0;
    std::vector<float> rgba;
};
bool IsPqPng(const std::vector<unsigned char>& bytes);
bool IsGainMapJpeg(const std::vector<unsigned char>& bytes);
std::vector<unsigned char> EncodePqPng(const Image& image);
std::vector<unsigned char> EncodeGainMapJpeg(const Image& image,const std::vector<unsigned char>& sdr);
Image DecodePqPng(const std::vector<unsigned char>& bytes);
Image DecodeGainMapJpeg(const std::vector<unsigned char>& bytes,float displayHeadroom=0.f);
}
