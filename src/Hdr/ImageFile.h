#pragma once
#include <cstdint>
#include <vector>

namespace Stack::Hdr {
enum class FileFormat { SdrPng, GainMapJpeg, PqPng };
inline const char* Extension(FileFormat format) { return format==FileFormat::GainMapJpeg ? ".jpg" : ".png"; }
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
Image DecodeGainMapJpeg(const std::vector<unsigned char>& bytes);
}
