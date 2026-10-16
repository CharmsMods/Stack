#include "OutputInternal.h"
#include "Raw/Bracketing/OutputSource.h"
#include "Raw/RawColorTransform.h"
#include "Raw/RawProcessingMath.h"
#include "NodeMath/Uuid.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace Stack::BurstEngine::Output {
namespace {
struct TemporaryFile {
    std::filesystem::path path;
    ~TemporaryFile(){if(!path.empty()){std::error_code ignored;std::filesystem::remove(path,ignored);}}
};
std::filesystem::path ReserveTemporary(const std::filesystem::path& parent) {
    for(unsigned i=0;i<8;++i) {
        const auto path=parent/(".stack-burst-"+NodeMath::GenerateCanonicalUuid()+".tmp");
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file!=INVALID_HANDLE_VALUE){CloseHandle(file);return path;}
        if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Cannot create output in the selected folder (Windows error "+std::to_string(GetLastError())+").");
    }
    throw std::runtime_error("Cannot reserve a unique output file.");
}
std::filesystem::path Publish(const std::filesystem::path& temporary,const std::filesystem::path& desired,const Request& request) {
    for(unsigned i=0;i<10000;++i) {
        Internal::CheckCanceled(request);
        auto candidate=desired;
        if(i)candidate=desired.parent_path()/(desired.stem().wstring()+L" ("+std::to_wstring(i+1)+L")"+desired.extension().wstring());
        // No REPLACE_EXISTING: the rename is the collision check and commit.
        if(MoveFileExW(temporary.c_str(),candidate.c_str(),MOVEFILE_WRITE_THROUGH))return candidate;
        const DWORD error=GetLastError();
        if(error!=ERROR_FILE_EXISTS&&error!=ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Cannot publish output (Windows error "+std::to_string(error)+").");
    }
    throw std::runtime_error("Too many existing files have this output name.");
}
}
const char* Extension(Format format) {
    switch(format){case Format::Dng:return ".dng";case Format::Tiff16:return ".tif";case Format::Jpeg:return ".jpg";case Format::Png:return ".png";}
    return "";
}
std::array<float,3> Internal::Image::Pixel(unsigned x,unsigned y) const {
    // Map display coordinates back to sensor coordinates; rendered files are
    // physically upright and carry Orientation=1. DNG retains sensor order.
    unsigned sx=x,sy=y;
    switch(metadata.orientation) {
        case 2:sx=width-1-x;break;
        case 3:sx=width-1-x;sy=height-1-y;break;
        case 4:sy=height-1-y;break;
        case 5:sx=y;sy=x;break;
        case 6:sx=y;sy=height-1-x;break;
        case 7:sx=width-1-y;sy=height-1-x;break;
        case 8:sx=width-1-y;sy=x;break;
        default:break;
    }
    const auto camera=Raw::Processing::DemosaicMalvarHeCutlerAt(mosaic,width,height,metadata.cfaPattern,sx,sy);
    std::array<float,3> rgb{};
    for(unsigned c=0;c<3;++c) {
        float value=0;for(unsigned k=0;k<3;++k)value+=cameraToWorking[c*3+k]*camera[k];
        rgb[c]=value*exposure;
    }
    // Match Stack's default RAW finish: retain signed scene-linear color and
    // highlight headroom until the shared tone/gamut transform, then encode.
    // Editable DNG never takes this display-development path.
    return display.Rgb(rgb);
}
Result Write(const Raw::Bracketing::BracketingResult& result,const Request& request) {
    Result out;
    try {
        Internal::CheckCanceled(request);
        if(result.status!=Raw::Bracketing::BracketingResult::Status::Completed||!result.raw||result.panorama||result.raw->reconstructedCameraRgb)
            throw std::runtime_error("File output currently supports completed standard Bayer bursts.");
        if(!std::isfinite(request.exposureStops)||std::abs(request.exposureStops)>24)
            throw std::runtime_error("Output exposure must be between -24 and +24 stops.");
        if(request.destination.filename().empty()||request.destination.stem().empty())
            throw std::runtime_error("Choose an output filename.");
        auto desired=std::filesystem::absolute(request.destination);
        desired.replace_extension(Extension(request.format));
        if(!std::filesystem::is_directory(desired.parent_path()))throw std::runtime_error("The output folder does not exist.");
        const auto& metadata=result.raw->metadata;
        if(metadata.rawWidth<2||metadata.rawHeight<2||metadata.cfaPattern==Raw::CfaPattern::Unknown)
            throw std::runtime_error("Output requires a known Bayer pattern and valid sensor dimensions.");
        const auto pixels=std::uint64_t(metadata.rawWidth)*metadata.rawHeight;
        // One calibrated CFA copy, a reference tile and bounded encoder rows.
        if(pixels>(request.memoryBudgetBytes>32ull*1024*1024?(request.memoryBudgetBytes-32ull*1024*1024)/4:0))
            throw std::runtime_error("The available output memory budget is too small for this image.");
        Internal::Image image{metadata};image.width=metadata.rawWidth;image.height=metadata.rawHeight;
        image.outputWidth=image.width;image.outputHeight=image.height;
        if(metadata.orientation>=5&&metadata.orientation<=8)std::swap(image.outputWidth,image.outputHeight);
        image.mosaic=Raw::Bracketing::BuildOutputMosaic(result,request.shouldCancel,[&](double progress){Internal::Progress(request,progress*.15,"Preparing output calibration");});
        if(request.format!=Format::Dng) {
            if(!metadata.hasCameraMatrix&&!(metadata.isDng&&(metadata.hasDngColorMatrix1||metadata.hasDngColorMatrix2||metadata.hasDngForwardMatrix1||metadata.hasDngForwardMatrix2)))
                throw std::runtime_error("The source has no camera color calibration for finished output.");
            Raw::RawDevelopSettings settings;settings.whiteBalanceMode=Raw::WhiteBalanceMode::AsShot;
            settings.workingSpace=Raw::RawWorkingSpace::LinearRec2020D65;
            const auto wb=Raw::ResolveRawWhiteBalance(metadata,settings);
            image.cameraToWorking=Raw::BuildRawCameraToWorkingTransform(metadata,settings,true);
            for(float value:image.cameraToWorking)if(!std::isfinite(value))throw std::runtime_error("Camera color calibration is invalid.");
            std::string error;
            if(!Raw::Processing::ApplyWhiteBalanceToCfaMosaic(metadata,wb,image.mosaic,&error))throw std::runtime_error(error);
            image.exposure=std::exp2(request.exposureStops+(metadata.hasDngBaselineExposure?metadata.dngBaselineExposure:0));
        }
        TemporaryFile temporary{ReserveTemporary(desired.parent_path())};
        if(request.format==Format::Dng)Internal::WriteDng(temporary.path,image,request);
        else Internal::WriteFinished(temporary.path,image,request);
        Internal::Progress(request,.99,"Finishing output");
        out.path=Publish(temporary.path,desired,request);temporary.path.clear();
        out.width=request.format==Format::Dng?image.width:image.outputWidth;
        out.height=request.format==Format::Dng?image.height:image.outputHeight;
        out.status=Result::Status::Completed;out.message="Output saved.";
    } catch(const std::exception& error) {
        out.status=request.shouldCancel&&request.shouldCancel()?Result::Status::Canceled:Result::Status::Failed;
        out.message=error.what();
    }
    return out;
}
}
