#include "Library/Internal/LibraryNewAssetWrite.h"
#include <atomic>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>
#include <set>
namespace {
namespace fs=std::filesystem;
void Require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void Put(const fs::path& path,const std::string& bytes){std::ofstream f(path,std::ios::binary);f<<bytes;f.close();Require(f.good(),"write fixture");}
std::string Read(const fs::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct Sandbox{fs::path path=fs::temp_directory_path()/("stack-new-assets-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));Sandbox(){Require(fs::create_directory(path),"sandbox");}~Sandbox(){std::error_code ec;fs::remove_all(path,ec);}};
}
int main(){
    Sandbox box; fs::path published; std::string error;
    const std::vector<unsigned char> bytes{'n','e','w'};
    for (const auto* extension : {".jpg", ".jpeg", ".bmp", ".tga", ".png"}) {
        const auto originalName=std::filesystem::path(std::string("source")+extension);
        const std::vector<unsigned char> encoded{0xff,0xd8,0x00,0x7f,0xfe};
        Require(Stack::Library::WriteNewLibraryAsset(box.path,originalName,encoded,published,error),"original format write");
        Require(published.extension()==extension && Read(published)==std::string(encoded.begin(),encoded.end()),"preserve input extension and encoded bytes");
    }
    Put(box.path/"existing.png","keep existing");
    Require(Stack::Library::WriteNewLibraryAsset(box.path,"existing.png",bytes,published,error),"collision imported uniquely");
    Require(published.filename()!="existing.png"&&Read(box.path/"existing.png")=="keep existing"&&Read(published)=="new","collision preserves bytes");
    Put(box.path/"original.NEF","original");
    fs::create_hard_link(box.path/"original.NEF",box.path/"hard.png");
    Require(Stack::Library::WriteNewLibraryAsset(box.path,"hard.png",bytes,published,error)&&Read(box.path/"original.NEF")=="original","hardlink preserved");
    std::error_code ec;fs::create_symlink(box.path/"original.NEF",box.path/"sym.png",ec);
    if(!ec)Require(Stack::Library::WriteNewLibraryAsset(box.path,"sym.png",bytes,published,error)&&Read(box.path/"original.NEF")=="original","symlink preserved");
    Put(box.path/"new.png.tmp","unrelated staging");
    Require(Stack::Library::WriteNewLibraryAsset(box.path,"new.png",bytes,published,error)&&Read(box.path/"new.png.tmp")=="unrelated staging","old temp untouched");
    Require(!Stack::Library::WriteNewLibraryAsset(box.path,"../escape.png",bytes,published,error),"reject traversal");
    Require(!Stack::Library::WriteNewLibraryAsset(box.path,"original.NEF",bytes,published,error),"reject RAW destination");
    Require(!Stack::Library::WriteNewLibraryAsset(box.path,"empty.png",{},published,error)&&!fs::exists(box.path/"empty.png"),"empty rejected");
    Put(box.path/"not-directory","block");
    Require(!Stack::Library::WriteNewLibraryAsset(box.path/"not-directory","image.png",bytes,published,error)&&Read(box.path/"not-directory")=="block","invalid parent fails");
    constexpr int count=12; std::atomic<int> ready{0};std::atomic<bool> go{false};
    std::array<std::thread,count> threads;
    std::array<fs::path,count> paths;std::array<bool,count> success{};
    for(int i=0;i<count;++i)threads[i]=std::thread([&,i]{++ready;while(!go.load())std::this_thread::yield();std::string issue;success[i]=Stack::Library::WriteNewLibraryAsset(box.path,"parallel.png",{static_cast<unsigned char>('A'+i)},paths[i],issue);});
    while (ready.load()!=count) std::this_thread::yield();
    go=true;
    for(auto& thread:threads)thread.join();
    std::set<fs::path> unique;
    for(int i=0;i<count;++i){Require(success[i],"parallel publication");Require(Read(paths[i])==std::string(1,char('A'+i)),"each contender retained");unique.insert(paths[i]);}
    Require(unique.size()==count,"distinct destinations");
    for(const auto& entry:fs::directory_iterator(box.path))Require(entry.path().filename().string().find(".stack-export-")!=0,"staging cleaned");
    std::cout<<"New Library asset tests passed\n";
}
