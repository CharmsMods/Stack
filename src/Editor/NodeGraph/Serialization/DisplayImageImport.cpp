#include "DisplayImageImport.h"
#include "EditorNodeGraphImageSerialization.h"
#include "Hdr/RasterSource.h"
#include "Hdr/Color.h"
#include "NodeMath/SourceImageSemantics.h"
#include "ThirdParty/stb_image.h"
#include "Utils/HashUtils.h"
#include <climits>
#include <filesystem>
#include <fstream>

namespace EditorNodeGraph {
bool ReadImageFilePayload(const std::string& path,ImagePayload& payload) {
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
    const std::streamoff length=file.tellg();
    if(length<=0 || length>INT_MAX) return false;
    std::vector<unsigned char> encoded(static_cast<size_t>(length));
    file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(encoded.data()),length)) return false;
    payload.sourcePath=path;
    return DecodeImagePayloadPngBytes(encoded,payload);
}
bool DecodeManagedImagePayload(const std::vector<unsigned char>& bytes,ImagePayload& payload) {
    using namespace Stack;
    Hdr::Image image;
    Hdr::FileFormat format;
    if(!Hdr::DecodeDisplaySource(bytes,image,&format)) return false;
    const auto w=image.width,h=image.height;
    const size_t stride=size_t(w)*4;
    auto pixels=std::make_shared<std::vector<float>>(std::move(image.rgba));
    // Keep one persistent full-resolution float allocation across history,
    // graph snapshots and viewport requests. Only initial adoption flips it.
    for(int y=0;y<h/2;++y)
        std::swap_ranges(pixels->begin()+y*stride,pixels->begin()+(y+1)*stride,pixels->begin()+(h-1-y)*stride);
    std::vector<unsigned char> sdr(pixels->size());
    if(format==Hdr::FileFormat::GainMapJpeg) {
        // The primary JPEG is the author's SDR rendition.
        Hdr::Image baseImage;
        if(Hdr::DecodeProfiledSdr(bytes,baseImage)) {
            if(baseImage.width!=w || baseImage.height!=h) throw std::runtime_error("Invalid HDR JPEG base extent.");
            for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
                const size_t source=(size_t(h-1-y)*w+x)*4,dest=(size_t(y)*w+x)*4;
                for(int c=0;c<3;++c) sdr[dest+c]=Hdr::SdrPreviewByte(baseImage.rgba[source+c]);
                sdr[dest+3]=255;
            }
        } else {
        stbi_set_flip_vertically_on_load_thread(1);
        int sw=0,sh=0,channels=0;
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> base(stbi_load_from_memory(bytes.data(),int(bytes.size()),&sw,&sh,&channels,4),stbi_image_free);
        if(!base || sw!=w || sh!=h) throw std::runtime_error("Invalid SDR base in HDR JPEG.");
        std::copy_n(base.get(),sdr.size(),sdr.data());
        }
    } else {
        // Use the shared luminance/color finish for a PQ source without a base.
        // Profiled SDR sources have ordinary relative luminance already.
        for(size_t i=0;i<sdr.size();i+=4) {
            const auto rgb=Hdr::SdrPreviewRgb({(*pixels)[i],(*pixels)[i+1],(*pixels)[i+2]},format);
            for(int channel=0;channel<3;++channel) {
                sdr[i+channel]=Hdr::SdrPreviewByte(rgb[channel]);
            }
            sdr[i+3]=static_cast<unsigned char>(std::lround(std::clamp((*pixels)[i+3],0.f,1.f)*255.f));
        }
    }
    // Prepare before publishing since bytes may alias payload.pngBytes.
    std::vector<unsigned char> retained(bytes);
    const size_t fingerprint=StackHash::HashBytes(bytes);
    NodeMath::SourceColorMetadata metadata;
    metadata.label=format==Hdr::FileFormat::PqPng ? "Rec.2100 PQ PNG" :
        format==Hdr::FileFormat::GainMapJpeg ? "HDR gain-map JPEG" : "Color-managed SDR image";
    auto& d=metadata.descriptor;
    d=NodeMath::DescribeStoredImageValue({},4,w,h,true);
    d.color=NodeMath::SemanticField<NodeMath::ColorIdentity>::Known({"srgb-d65",{},NodeMath::ColorRelation::Standard});
    d.transfer=NodeMath::SemanticField<NodeMath::TransferDescriptor>::Known({NodeMath::TransferKind::Linear,0.0,{}});
    d.reference=NodeMath::SemanticField<NodeMath::ReferenceState>::Known(NodeMath::ReferenceState::Display);
    // Signed Rec.709 includes colors outside the sRGB triangle.
    d.range=NodeMath::SemanticField<NodeMath::NumericRange>::Known({0.,1.,true,true,NodeMath::NonFinitePolicy::Forbidden});
    payload.width=w; payload.height=h; payload.channels=payload.originalChannels=4;
    payload.pixels.swap(sdr); payload.hdrPixels=std::move(pixels);
    payload.hdrFingerprint=fingerprint;
    payload.hdrStorageQuarterTurns=0;
    payload.pngBytes.swap(retained); payload.sourceColorMetadata=std::move(metadata);
    InvalidateImagePayloadRuntime(payload);
    return true;
}
}
