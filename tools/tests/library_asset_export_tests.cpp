#include "Library/Internal/LibraryAssetExport.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/stb_image_write.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace {
namespace fs=std::filesystem;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string Read(const fs::path& path){std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void Put(const fs::path& path,const std::string& bytes){std::ofstream output(path,std::ios::binary);output.write(bytes.data(),bytes.size());output.close();Require(output.good(),"fixture write");}
void Append(void* context,void* bytes,int count){static_cast<std::string*>(context)->append(static_cast<char*>(bytes),count);}
struct Sandbox{fs::path path=fs::temp_directory_path()/("stack-asset-export-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Sandbox(){Require(fs::create_directory(path),"sandbox");}~Sandbox(){std::error_code ec;fs::remove_all(path,ec);}};
}
int main(){
    Sandbox box;std::string error;
    const unsigned char pixels[]{255,0,0,255,0,255,0,255,0,0,255,255,80,90,100,255};
    std::string png,jpg,bmp,tga;
    Require(stbi_write_png_to_func(Append,&png,2,2,4,pixels,8)!=0,"PNG fixture");
    Require(stbi_write_jpg_to_func(Append,&jpg,2,2,4,pixels,90)!=0,"JPEG fixture");
    Require(stbi_write_bmp_to_func(Append,&bmp,2,2,4,pixels)!=0,"BMP fixture");
    Require(stbi_write_tga_to_func(Append,&tga,2,2,4,pixels)!=0,"TGA fixture");
    const std::pair<const char*,const std::string*> cases[]={{".png",&png},{".jpg",&jpg},{".bmp",&bmp},{".tga",&tga}};
    for(const auto& item:cases){
        const auto source=box.path/(std::string("source")+item.first), destination=box.path/(std::string("copy")+item.first);
        Put(source,*item.second);
        Require(Stack::Library::LibraryAssetEncodedExtension(source,error)==item.first,"detect encoded format");
        Require(Stack::Library::ExportLibraryAsset(source,destination,error)&&Read(destination)==*item.second,"byte preserving format copy");
    }
    const auto source=box.path/"old-mislabeled.png";
    Put(source,jpg);
    const auto extension=Stack::Library::LibraryAssetEncodedExtension(source,error);
    Require(extension==".jpg"&&Stack::Library::LibraryAssetExportSuggestedName(source,extension)=="old-mislabeled.jpg","JPEG suggestion from actual bytes");
    const auto destination=box.path/"accepted.jpg";
    Put(destination,"previous export");
    Require(!Stack::Library::ExportLibraryAsset(source,box.path/"wrong.png",error)&&!fs::exists(box.path/"wrong.png"),"reject misleading PNG destination");
    Require(Stack::Library::ExportLibraryAsset(source,destination,error)&&Read(destination)==jpg,"export old mislabeled bytes");
    Require(Stack::Library::ExportLibraryAsset(source,box.path/"alternate.jpeg",error),"JPEG alias extension");
    const auto jpegSource=box.path/"source.jpg";
    Require(!Stack::Library::ExportLibraryAsset(jpegSource,jpegSource,error),"reject source itself");
    const auto hard=box.path/"hard.jpg";fs::create_hard_link(jpegSource,hard);
    Require(!Stack::Library::ExportLibraryAsset(jpegSource,hard,error)&&Read(jpegSource)==jpg,"reject source hardlink alias");
    std::error_code ec;const auto sym=box.path/"sym.jpg";fs::create_symlink(jpegSource,sym,ec);
    if(!ec)Require(!Stack::Library::ExportLibraryAsset(jpegSource,sym,error)&&Read(jpegSource)==jpg,"reject source symlink alias");
    const auto raw=box.path/"original.NEF";Put(raw,"original raw marker");
    Require(!Stack::Library::ExportLibraryAsset(source,raw,error)&&Read(raw)=="original raw marker","reject RAW overwrite");
    Put(box.path/"unsupported.png","not an image");
    Require(!Stack::Library::ExportLibraryAsset(box.path/"unsupported.png",destination,error)&&Read(destination)==jpg,"unknown encoding preserves destination");
    Put(box.path/"ambiguous.png",tga);
    Require(Stack::Library::LibraryAssetEncodedExtension(box.path/"ambiguous.png",error).empty(),"TGA requires matching source extension");
    Put(destination,"previous export");
    int checks=0;
    Require(!Stack::Library::ExportLibraryAsset(source,destination,error,[&]{if(++checks==2)Put(source,jpg.substr(0,3));return false;})&&Read(destination)=="previous export","late source truncation preserves destination");
    Put(source,jpg);checks=0;
    Require(!Stack::Library::ExportLibraryAsset(source,destination,error,[&]{if(++checks==2)Put(source,jpg+"growth");return false;})&&Read(destination)=="previous export","late source growth preserves destination");
    Put(source,jpg);checks=0;
    Require(!Stack::Library::ExportLibraryAsset(source,destination,error,[&]{if(++checks==2)Put(destination,"late file");return false;})&&Read(destination)=="late file","late destination retains itself");
    Require(!Stack::Library::ExportLibraryAsset(source,destination,error,[]{return true;})&&Read(destination)=="late file","cancellation preserves destination");
    for(const auto& entry:fs::directory_iterator(box.path))Require(entry.path().filename().string().find(".stack-export-")!=0,"staging cleaned");
    std::cout<<"Library asset export tests passed\n";
}
