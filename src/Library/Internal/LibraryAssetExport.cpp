#include "Library/Internal/LibraryAssetExport.h"
#include "Utils/AtomicExportFile.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>

namespace Stack::Library {
namespace {
const std::vector<std::string> imageExtensions{".png", ".jpg", ".jpeg", ".bmp", ".tga"};
std::string LowerExtension(const std::filesystem::path& path) {
    auto result=path.extension().u8string();
    std::transform(result.begin(),result.end(),result.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return result;
}
std::string Detect(std::istream& input, const std::filesystem::path& source) {
    std::array<unsigned char,18> header{};
    input.read(reinterpret_cast<char*>(header.data()),header.size());
    const auto size=input.gcount();
    static constexpr std::array<unsigned char,8> png{137,80,78,71,13,10,26,10};
    if(size>=8 && std::equal(png.begin(),png.end(),header.begin())) return ".png";
    if(size>=3 && header[0]==0xff && header[1]==0xd8 && header[2]==0xff) return ".jpg";
    if(size>=2 && header[0]=='B' && header[1]=='M') return ".bmp";
    if(size==18 && LowerExtension(source)==".tga") {
        const auto type=header[2], depth=header[16];
        const bool mapped=type==1 || type==9;
        const bool rgb=type==2 || type==10;
        const bool gray=type==3 || type==11;
        const bool dimensions=(header[12] || header[13]) && (header[14] || header[15]);
        const bool palette=mapped && header[1]==1 && (header[5] || header[6]) &&
            (header[7]==15 || header[7]==16 || header[7]==24 || header[7]==32) && (depth==8 || depth==16);
        const bool direct=header[1]==0 && ((rgb && (depth==15 || depth==16 || depth==24 || depth==32)) ||
            (gray && (depth==8 || depth==16)));
        if(dimensions && (palette || direct)) return ".tga";
    }
    return {};
}
bool SourceMatches(const FileSave::ExportFileTarget& captured) {
    FileSave::ExportFileTarget now; std::string error; std::error_code ec;
    return FileSave::TargetApprovalStillMatches(captured.approval,ec) &&
        FileSave::PrepareExportFileTarget(captured.approval.path,{},imageExtensions,now,error) &&
        now.deviceId==captured.deviceId && now.fileId==captured.fileId;
}
}
std::string LibraryAssetEncodedExtension(const std::filesystem::path& source,std::string& error) try {
    error.clear(); std::ifstream input(source,std::ios::binary);
    const auto extension=input ? Detect(input,source) : std::string{};
    if(extension.empty()) error="The asset is not a recognized PNG, JPEG, BMP, or .tga image. No file was exported.";
    return extension;
} catch(const std::exception& exception) {error=exception.what();return {};}

bool ExportLibraryAsset(const std::filesystem::path& source,
    const std::filesystem::path& destination,std::string& error,
    const std::function<bool()>& isCanceled) try {
    FileSave::ExportFileTarget inputTarget,target;
    if(!FileSave::PrepareExportFileTarget(source,{},imageExtensions,inputTarget,error)) return false;
    std::ifstream input(inputTarget.approval.path,std::ios::binary);
    const auto extension=input ? Detect(input,source) : std::string{};
    if(extension.empty()) {error="The asset encoding is unsupported. No file was exported.";return false;}
    const auto allowed=extension==".jpg" ? std::vector<std::string>{".jpg",".jpeg"} : std::vector<std::string>{extension};
    if(!FileSave::PrepareExportFileTarget(destination,{inputTarget.approval.path},allowed,target,error)) {
        if(std::find(allowed.begin(),allowed.end(),LowerExtension(destination))==allowed.end())
            error="This asset contains "+extension+" image bytes. Choose a matching filename; export does not convert image formats.";
        return false;
    }
    input.clear(); input.seekg(0,std::ios::beg);
    if(!input || !SourceMatches(inputTarget)) {error="The source image changed. Retry the export.";return false;}
    bool sourceChanged=false;
    const auto checkCanceled=[&] {
        const bool canceled=isCanceled && isCanceled();
        sourceChanged=!SourceMatches(inputTarget);
        return canceled || sourceChanged;
    };
    if(checkCanceled()) {error=sourceChanged ? "The source image changed. Retry the export." : "Asset export canceled.";return false;}
    const auto size=inputTarget.approval.size;
    const bool result=FileSave::WriteExportStreamAtomically(target,size,[&](std::ostream& output) {
        std::array<char,64*1024> buffer{};
        std::uintmax_t remaining=size;
        while(remaining) {
            const auto count=static_cast<std::streamsize>(std::min<std::uintmax_t>(remaining,buffer.size()));
            input.read(buffer.data(),count);
            if(input.gcount()!=count || input.bad()) return false;
            output.write(buffer.data(),count);
            if(!output.good()) return false;
            remaining-=static_cast<std::uintmax_t>(count);
        }
        char extra=0; input.read(&extra,1);
        if(input.gcount()!=0 || input.bad()) return false;
        sourceChanged=!SourceMatches(inputTarget);
        return !sourceChanged;
    },error,checkCanceled);
    if(sourceChanged) error="The source image changed. Retry the export.";
    return result;
} catch(const std::exception& exception) {error="Could not export the asset: "+std::string(exception.what());return false;}
}
