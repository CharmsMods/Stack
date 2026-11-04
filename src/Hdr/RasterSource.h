#pragma once
#include "Hdr/ImageFile.h"

namespace Stack::Hdr {
// Recognizes a finished HDR photo or a color-managed SDR photo. A false
// result lets untagged/legacy images continue through their ordinary decoder.
// RGBA is upright, straight alpha, linear Rec.709 relative to diffuse white.
bool DecodeDisplaySource(const std::vector<unsigned char>& encoded, Image& image, FileFormat* format = nullptr);
// RGB used for SDR view of a PQ master that has no embedded SDR rendition.
unsigned char SdrPreviewByte(float value, FileFormat source = FileFormat::PqPng);
// Reads only the base image and its SDR source profile, including gain-map JPEGs.
bool DecodeProfiledSdr(const std::vector<unsigned char>& encoded, Image& image);
}
