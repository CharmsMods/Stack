#include "OutputInternal.h"
#include "App/AppVersion.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <icm.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Stack::BurstEngine::Output::Internal {
namespace {
using Microsoft::WRL::ComPtr;
void Require(HRESULT result,const char* action) {
    if(FAILED(result))throw std::runtime_error(std::string(action)+" (Windows imaging error "+std::to_string(std::uint32_t(result))+").");
}
struct Apartment {
    HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    Apartment(){if(FAILED(result)&&result!=RPC_E_CHANGED_MODE)Require(result,"Cannot initialize image encoding");}
    ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
};
void Metadata(IWICMetadataQueryWriter* writer,const std::wstring& name,const PROPVARIANT& value) {
    Require(writer->SetMetadataByName(name.c_str(),&value),"Cannot write image metadata");
}
void Short(IWICMetadataQueryWriter* writer,const std::wstring& name,USHORT value) {
    PROPVARIANT item{};item.vt=VT_UI2;item.uiVal=value;Metadata(writer,name,item);
}
void Text(IWICMetadataQueryWriter* writer,const std::wstring& name,const std::string& value) {
    if(value.empty())return;
    PROPVARIANT item{};item.vt=VT_LPSTR;item.pszVal=const_cast<char*>(value.c_str());Metadata(writer,name,item);
}
void Rational(IWICMetadataQueryWriter* writer,const std::wstring& name,double value) {
    if(!std::isfinite(value)||value<=0||value>4000)return;
    PROPVARIANT item{};item.vt=VT_UI8;
    item.uhVal.QuadPart=(std::uint64_t(1000000)<<32)|std::uint32_t(std::llround(value*1000000));Metadata(writer,name,item);
}
}
void WriteFinished(const std::filesystem::path& path,const Image& image,const Request& request) {
    const bool jpeg=request.format==Format::Jpeg,png=request.format==Format::Png;
    if(jpeg&&(image.outputWidth>65535||image.outputHeight>65535))throw std::runtime_error("JPEG supports dimensions up to 65535 pixels.");
    const auto rowBytes64=std::uint64_t(image.outputWidth)*(jpeg?3u:6u);
    if(rowBytes64>std::numeric_limits<UINT>::max())throw std::runtime_error("Output row is too large for the image encoder.");
    Apartment apartment;
    ComPtr<IWICImagingFactory> factory;
    Require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)),"Cannot create image encoder");
    ComPtr<IWICStream> stream;Require(factory->CreateStream(&stream),"Cannot create output stream");
    Require(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE),"Cannot open output file");
    ComPtr<IWICBitmapEncoder> encoder;
    Require(factory->CreateEncoder(jpeg?GUID_ContainerFormatJpeg:png?GUID_ContainerFormatPng:GUID_ContainerFormatTiff,&GUID_VendorMicrosoft,&encoder),"Cannot create image encoder");
    Require(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Cannot initialize image encoder");
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> properties;
    Require(encoder->CreateNewFrame(&frame,&properties),"Cannot create image frame");
    PROPBAG2 option{};VARIANT value{};
    if(jpeg) {
        option.pstrName=const_cast<wchar_t*>(L"ImageQuality");value.vt=VT_R4;value.fltVal=std::clamp(request.jpegQuality,1u,100u)/100.f;
    } else if(!png) {
        option.pstrName=const_cast<wchar_t*>(L"TiffCompressionMethod");value.vt=VT_UI1;value.bVal=WICTiffCompressionNone;
    }
    if(!png)Require(properties->Write(1,&option,&value),"Cannot set output quality");
    Require(frame->Initialize(properties.Get()),"Cannot initialize image frame");
    Require(frame->SetSize(image.outputWidth,image.outputHeight),"Cannot set output dimensions");
    Require(frame->SetResolution(300,300),"Cannot set output resolution");
    const auto requestedFormat=jpeg?GUID_WICPixelFormat24bppBGR:GUID_WICPixelFormat48bppRGB;
    auto actualFormat=requestedFormat;Require(frame->SetPixelFormat(&actualFormat),"Cannot select output precision");
    if(!IsEqualGUID(actualFormat,requestedFormat))throw std::runtime_error("Windows image encoder cannot preserve the requested output precision.");
    ComPtr<IWICMetadataQueryWriter> metadata;
    Require(frame->GetMetadataQueryWriter(&metadata),"Cannot create output metadata");
    if(!png) {
    const std::wstring base=jpeg?L"/app1/ifd":L"/ifd";
    Short(metadata.Get(),base+L"/{ushort=274}",1);
    Short(metadata.Get(),base+L"/exif/{ushort=40961}",1); // sRGB
    Text(metadata.Get(),base+L"/{ushort=305}",std::string("StackBurst ")+AppVersion::kVersionString);
    Text(metadata.Get(),base+L"/{ushort=271}",image.metadata.cameraMake);
    Text(metadata.Get(),base+L"/{ushort=272}",image.metadata.cameraModel);
    Text(metadata.Get(),base+L"/{ushort=270}","Stack burst denoise; full-resolution as-shot development; sRGB.");
    Text(metadata.Get(),base+L"/exif/{ushort=42036}",image.metadata.lensModel);
    if(image.metadata.hasExposureTime)Rational(metadata.Get(),base+L"/exif/{ushort=33434}",image.metadata.exposureTimeSeconds);
    if(image.metadata.hasApertureFNumber)Rational(metadata.Get(),base+L"/exif/{ushort=33437}",image.metadata.apertureFNumber);
    if(image.metadata.hasFocalLength)Rational(metadata.Get(),base+L"/exif/{ushort=37386}",image.metadata.focalLengthMm);
    if(image.metadata.hasIsoSpeed)Short(metadata.Get(),base+L"/exif/{ushort=34855}",USHORT(std::clamp(image.metadata.isoSpeed,1.f,65535.f)));
    } else {
        PROPVARIANT intent{};intent.vt=VT_UI1;intent.bVal=0;
        Metadata(metadata.Get(),L"/sRGB/RenderingIntent",intent);
        Text(metadata.Get(),L"/tEXt/{str=Software}",std::string("StackBurst ")+AppVersion::kVersionString);
        Text(metadata.Get(),L"/tEXt/{str=Description}","Full-resolution as-shot burst development; 16-bit sRGB.");
    }
    // TIFF/JPEG embed the system's standard sRGB profile. PNG carries its
    // explicit standard sRGB chunk (PNG forbids combining sRGB with iCCP).
    if(!png) {
        DWORD count=0;GetStandardColorSpaceProfileW(nullptr,LCS_sRGB,nullptr,&count);
        if(!count)throw std::runtime_error("Windows' standard sRGB profile is unavailable.");
        std::vector<wchar_t> profile(count);
        if(!GetStandardColorSpaceProfileW(nullptr,LCS_sRGB,profile.data(),&count))
            throw std::runtime_error("Windows' standard sRGB profile cannot be opened.");
        ComPtr<IWICColorContext> color;Require(factory->CreateColorContext(&color),"Cannot create sRGB color context");
        Require(color->InitializeFromFilename(profile.data()),"Cannot load sRGB color profile");
        IWICColorContext* contexts[]{color.Get()};Require(frame->SetColorContexts(1,contexts),"Cannot tag sRGB output");
    }
    const auto rowBytes=UINT(rowBytes64);
    std::vector<std::uint8_t> row(rowBytes);
    for(unsigned y=0;y<image.outputHeight;++y) {
        if((y&7)==0)Progress(request,.15+.83*double(y)/image.outputHeight,jpeg?"Developing JPEG":png?"Developing 16-bit PNG":"Developing 16-bit TIFF");
        for(unsigned x=0;x<image.outputWidth;++x) {
            const auto pixel=image.Pixel(x,y);
            if(jpeg)for(unsigned c=0;c<3;++c)row[std::size_t(x)*3+c]=std::uint8_t(std::lround(std::clamp(pixel[2-c],0.f,1.f)*255));
            else for(unsigned c=0;c<3;++c) {
                const auto sample=std::uint16_t(std::lround(std::clamp(pixel[c],0.f,1.f)*65535));
                const auto offset=std::size_t(x)*6+c*2;row[offset]=sample&255;row[offset+1]=sample>>8;
            }
        }
        Require(frame->WritePixels(1,rowBytes,rowBytes,row.data()),"Cannot write output pixels");
    }
    CheckCanceled(request);Require(frame->Commit(),"Cannot finish image frame");
    Require(encoder->Commit(),"Cannot finish output image");
}
}
