#include "Protocol.h"
#include <algorithm>
#include <fstream>
#include <random>
#include <stdexcept>

namespace Stack::BurstEngine {
namespace {
constexpr std::uint64_t MaximumMessageBytes=512ull*1024*1024;
}
std::string UniqueId(){
    std::random_device source;std::string out(32,'0');
    constexpr char digits[]="0123456789abcdef";
    for(auto& character:out)character=digits[source()&15];return out;
}
std::string PathText(const std::filesystem::path& path){
    const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};
}
std::filesystem::path TextPath(const std::string& text){return std::filesystem::u8path(text);}
void WriteDocument(const std::filesystem::path& path,const Json& value){
    const auto bytes=Json::to_cbor(value);
    if(bytes.size()>MaximumMessageBytes)throw std::runtime_error("Burst metadata exceeds the protocol limit.");
    std::ofstream stream(path,std::ios::binary|std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());stream.close();
    if(!stream)throw std::runtime_error("Could not write burst transfer metadata.");
}
Json ReadDocument(const std::filesystem::path& path){
    const auto size=std::filesystem::file_size(path);
    if(size>MaximumMessageBytes)throw std::runtime_error("Burst metadata exceeds the protocol limit.");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    std::ifstream stream(path,std::ios::binary);
    if(size&&!stream.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("Could not read burst transfer metadata.");
    return Json::from_cbor(bytes);
}
#ifdef _WIN32
std::string WindowsError(const char* operation){return std::string(operation)+" (Windows error "+std::to_string(GetLastError())+").";}
namespace {
void Transfer(HANDLE pipe,void* data,std::uint64_t size,bool write){
    auto* bytes=static_cast<unsigned char*>(data);
    while(size){
        const DWORD part=static_cast<DWORD>(std::min<std::uint64_t>(size,1024*1024));DWORD done=0;
        const BOOL success=write?WriteFile(pipe,bytes,part,&done,nullptr):ReadFile(pipe,bytes,part,&done,nullptr);
        if(!success||!done)throw std::runtime_error(WindowsError("The burst engine connection closed"));
        bytes+=done;size-=done;
    }
}
}
void WriteMessage(HANDLE pipe,const Json& value){
    auto bytes=Json::to_cbor(value);std::uint64_t count=bytes.size();
    if(count>MaximumMessageBytes)throw std::runtime_error("Burst message exceeds the protocol limit.");
    Transfer(pipe,&count,sizeof(count),true);Transfer(pipe,bytes.data(),count,true);
}
Json ReadMessage(HANDLE pipe){
    std::uint64_t count=0;Transfer(pipe,&count,sizeof(count),false);
    if(!count||count>MaximumMessageBytes)throw std::runtime_error("The burst engine returned an invalid message size.");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(count));Transfer(pipe,bytes.data(),count,false);
    return Json::from_cbor(bytes);
}
#endif
}
