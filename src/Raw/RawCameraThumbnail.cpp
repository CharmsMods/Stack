#include "Raw/RawCameraThumbnail.h"
#include "Raw/RawGalleryThumbnailPixels.h"
#include "Raw/RawOrientation.h"
#include "Raw/RawLoader.h"
#include "Raw/LibRawRuntime.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#ifdef STACK_READ_BRACKET_MASTERS
#include "Hdr/Color.h"
#include "Hdr/RasterSource.h"
#include <array>
#include <cstring>
#endif
#ifdef STACK_ENABLE_LIBRAW
#include <libraw/libraw.h>
#endif
#ifdef _WIN32
#include "Utils/ScopedComApartment.h"
#include <wincodec.h>
#include <wrl/client.h>
#endif

namespace Stack::RawWorkspace {

namespace {
// Camera directories can omit a preview's dimensions or give the sensor size.
// Read only JPEG segment headers, seeking past EXIF/ICC payloads inside its range.
bool ReadJpegSize(std::ifstream& input, CameraThumbnail& candidate, std::uintmax_t fileSize) {
    if (!candidate.offset || candidate.offset > fileSize || candidate.bytes < 4 ||
        candidate.bytes > 32u * 1024u * 1024u || candidate.bytes > fileSize - candidate.offset) return false;
    input.clear();
    input.seekg(static_cast<std::streamoff>(candidate.offset));
    if (input.get() != 0xff || input.get() != 0xd8) return false;
    std::uint32_t remaining = candidate.bytes - 2;
    while (remaining >= 4 && input.get() == 0xff) {
        --remaining;
        int marker;
        do { marker = input.get(); --remaining; } while (marker == 0xff && remaining);
        if (remaining < 2 || marker < 0 || marker == 0xda || marker == 0xd9) return false;
        unsigned char size[2];
        if (!input.read(reinterpret_cast<char*>(size), 2)) return false;
        const auto length = static_cast<unsigned>((size[0] << 8) | size[1]);
        if (length < 2 || length > remaining) return false;
        if (marker == 0xc0 || marker == 0xc1 || marker == 0xc2) {
            unsigned char frame[5];
            if (length < 8 || !input.read(reinterpret_cast<char*>(frame), 5) || frame[0] != 8) return false;
            candidate.height = (frame[1] << 8) | frame[2];
            candidate.width = (frame[3] << 8) | frame[4];
            return candidate.Fits(fileSize);
        }
        input.seekg(length - 2, std::ios::cur);
        remaining -= length;
    }
    return false;
}
#ifdef _WIN32
#ifdef STACK_READ_BRACKET_MASTERS
// WIC's ordinary format converter does not apply a PQ transfer. Inspect the
// PNG chunks before image data without loading the full-size raster.
bool PqThumbnailSource(const std::filesystem::path& path) {
    if(_wcsicmp(path.extension().c_str(),L".png")!=0) return false;
    std::ifstream input(path,std::ios::binary);
    std::array<unsigned char,8> header{};
    constexpr unsigned char signature[]{137,80,78,71,13,10,26,10};
    if(!input.read(reinterpret_cast<char*>(header.data()),8) || std::memcmp(header.data(),signature,8)) return false;
    while(input.read(reinterpret_cast<char*>(header.data()),8)) {
        const auto length=(uint32_t(header[0])<<24)|(uint32_t(header[1])<<16)|(uint32_t(header[2])<<8)|header[3];
        if(length>INT_MAX || !std::memcmp(header.data()+4,"IDAT",4)) return false;
        if(!std::memcmp(header.data()+4,"cICP",4)) {
            std::array<unsigned char,4> code{};
            return length==4 && input.read(reinterpret_cast<char*>(code.data()),4) &&
                code==std::array<unsigned char,4>{9,16,0,1};
        }
        input.seekg(std::streamoff(length)+4,std::ios::cur);
    }
    return false;
}
#endif
bool DecodeScaledFrame(IWICImagingFactory* factory, IWICBitmapFrameDecode* frame, int edge,
    std::vector<unsigned char>& pixels, int& width, int& height, bool pq=false) {
    UINT w = 0, h = 0;
    if (edge <= 0 || edge > 4096 || FAILED(frame->GetSize(&w, &h)) || !w || !h || w > 65535 || h > 65535) return false;
    CameraThumbnail size;
    size.width = static_cast<int>(w);
    size.height = static_cast<int>(h);
    size.DisplaySize(edge, width, height);
    Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(frame, width, height, WICBitmapInterpolationModeFant)) ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(scaler.Get(), pq ? GUID_WICPixelFormat64bppRGBA : GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
            nullptr, 0, WICBitmapPaletteTypeCustom))) return false;
    pixels.resize(static_cast<std::size_t>(width) * height * 4);
#ifdef STACK_READ_BRACKET_MASTERS
    if(pq) {
        std::vector<std::uint16_t> words(pixels.size());
        if(FAILED(converter->CopyPixels(nullptr,width*8,static_cast<UINT>(words.size()*2),
            reinterpret_cast<BYTE*>(words.data())))) { pixels.clear(); return false; }
        for(size_t i=0;i<pixels.size();i+=4) {
            Stack::Hdr::Rgb linear{};
            for(int c=0;c<3;++c) linear[c]=Stack::Hdr::Nits(words[i+c]/65535.f)/Stack::Hdr::ReferenceWhite;
            linear=Stack::Hdr::To709(linear);
            for(int c=0;c<3;++c) pixels[i+c]=Stack::Hdr::SdrPreviewByte(linear[c]);
            pixels[i+3]=static_cast<unsigned char>((words[i+3]+128)/257);
        }
        return true;
    }
#endif
    if (SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) return true;
    pixels.clear();
    return false;
}
#endif
} // namespace

bool CameraThumbnail::Fits(std::uintmax_t fileSize) const {
    return width > 0 && height > 0 && width <= 65535 && height <= 65535 &&
        orientation >= 1 && orientation <= 8 && bytes >= 4 && bytes <= 32u * 1024u * 1024u &&
        offset > 0 && offset <= fileSize && bytes <= fileSize - offset;
}

void CameraThumbnail::DisplaySize(int maxEdge, int& outWidth, int& outHeight) const {
    const double scale = std::min(1.0, double(maxEdge) / std::max({1, width, height}));
    outWidth = std::max(1, int(std::lround(width * scale)));
    outHeight = std::max(1, int(std::lround(height * scale)));
    if (orientation >= 5) std::swap(outWidth, outHeight);
}

CameraThumbnail FindCameraThumbnail(const std::filesystem::path& path, std::uintmax_t fileSize, int targetEdge, const std::function<bool()>& cancelled) {
    CameraThumbnail best;
    if(cancelled && cancelled())return best;
#ifdef STACK_ENABLE_LIBRAW
    if (!Raw::IsLibRawRuntimeAvailable()) return best;
    LibRaw raw;
    if (raw.open_file(path.c_str()) != LIBRAW_SUCCESS) return best;
    if(cancelled && cancelled())return best;
    const auto& list = raw.imgdata.thumbs_list;
    std::ifstream input(path, std::ios::binary);
    for (int i = 0; i < std::min(list.thumbcount, int(LIBRAW_THUMBNAIL_MAXCOUNT)); ++i) {
        if(cancelled && cancelled())return {};
        const auto& item = list.thumblist[i];
        if (item.tformat != LIBRAW_INTERNAL_THUMBNAIL_JPEG || item.toffset <= 0) continue;
        CameraThumbnail candidate{static_cast<std::uint64_t>(item.toffset), item.tlength,
            item.twidth, item.theight, Raw::ExifOrientationFromLibRawFlip(
                item.tflip <= 7 ? int(item.tflip) : raw.imgdata.sizes.flip)};
        if (!ReadJpegSize(input, candidate, fileSize)) continue;
        const int edge = std::max(candidate.width, candidate.height);
        const int bestEdge = std::max(best.width, best.height);
        if (best.bytes && (bestEdge >= targetEdge ? (edge < targetEdge || edge >= bestEdge) : edge <= bestEdge)) continue;
        best = candidate;
    }
#endif
    return best;
}

bool DecodeCameraThumbnail(const std::filesystem::path& path, const CameraThumbnail& preview,
    int maxEdge, std::vector<unsigned char>& rgba, int& width, int& height, const std::function<bool()>& cancelled) {
    rgba.clear();
    width = height = 0;
#ifdef _WIN32
    if (maxEdge < 1 || maxEdge > 4096) return false;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto length = file.tellg();
    if (length < 0 || !preview.Fits(static_cast<std::uintmax_t>(length))) return false;
    file.seekg(static_cast<std::streamoff>(preview.offset));
    std::vector<unsigned char> jpeg(preview.bytes);
    for(std::size_t offset=0;offset<jpeg.size();) {
        if(cancelled && cancelled())return false;
        const auto count=std::min<std::size_t>(64*1024,jpeg.size()-offset);
        if(!file.read(reinterpret_cast<char*>(jpeg.data()+offset),count))return false;
        offset+=count;
    }
    if(jpeg.size()<2||jpeg[0]!=0xff||jpeg[1]!=0xd8)return false;

    Stack::Win32::ScopedComApartment apartment(COINIT_MULTITHREADED);
    if (!apartment && apartment.Result() != RPC_E_CHANGED_MODE) return false;
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromMemory(jpeg.data(), preview.bytes)) ||
        FAILED(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame))) return false;
    return DecodeScaledFrame(factory.Get(), frame.Get(), maxEdge, rgba, width, height) &&
        OrientThumbnailPixels(rgba, width, height, preview.orientation, {});
#else
    return false;
#endif
}
bool DecodeRasterThumbnail(const std::filesystem::path& path, int maxEdge, std::vector<unsigned char>& rgba,
    int& width, int& height) {
#ifdef _WIN32
    Stack::Win32::ScopedComApartment apartment(COINIT_MULTITHREADED);
    if (!apartment && apartment.Result() != RPC_E_CHANGED_MODE) return false;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
#ifdef STACK_READ_BRACKET_MASTERS
    const bool pq=PqThumbnailSource(path);
#else
    const bool pq=false;
#endif
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && DecodeScaledFrame(factory.Get(), frame.Get(), maxEdge, rgba, width, height, pq);
#else
    return false;
#endif
}
GalleryPixelSource LoadOriginalGalleryPixels(const std::filesystem::path& path, const CameraThumbnail& preview,
    int maxEdge, bool largeView, std::vector<unsigned char>& rgba, int& width, int& height,
    const std::function<bool()>& cancelled) {
    if ((cancelled && cancelled()) || maxEdge < 1 || maxEdge > 4096) return GalleryPixelSource::Unavailable;
    auto candidate = preview;
    if (largeView && std::max(candidate.width, candidate.height) < maxEdge) {
        // Some cameras store several sizes. Look for a larger JPEG only after
        // a particular image has settled in a view that needs those pixels.
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (!error) candidate = FindCameraThumbnail(path, size, maxEdge, cancelled);
    }
    if (candidate.bytes && (!largeView || std::max(candidate.width, candidate.height) >= maxEdge) &&
        DecodeCameraThumbnail(path, candidate, maxEdge, rgba, width, height, cancelled) &&
        (!largeView || std::max(width, height) >= maxEdge)) return GalleryPixelSource::CameraJpeg;

    Raw::RawImageData raw;
    if ((!cancelled || !cancelled()) && Raw::RawLoader::LoadFile(path.u8string(), raw, cancelled)) {
        const int native = std::max(raw.metadata.visibleWidth, raw.metadata.visibleHeight);
        if (BuildRawGalleryThumbnailPixels(raw, std::min(maxEdge, native), rgba, width, height, !largeView, cancelled))
            return GalleryPixelSource::RawSamples;
    }
    rgba.clear();
    width = height = 0;
    return GalleryPixelSource::Unavailable;
}
} // namespace Stack::RawWorkspace
