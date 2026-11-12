#include "FolderIndexInternal.h"
#include <algorithm>
#include <fstream>

namespace Stack::Search::Detail {
namespace {
std::filesystem::path CacheFile(const std::filesystem::path& cache,const std::filesystem::path& root) {
    std::uint64_t hash=14695981039346656037ull;
    for(auto c:PathKey(root)){hash^=static_cast<std::uint16_t>(c);hash*=1099511628211ull;}
    return cache/(std::to_wstring(hash)+L".folders");
}
bool ReadText(std::istream& stream,std::wstring& text,std::size_t& budget) {
    std::uint32_t size=0;stream.read(reinterpret_cast<char*>(&size),sizeof(size));
    if(!stream || size>32768 || size>budget)return false;
    budget-=size;text.resize(size);
    stream.read(reinterpret_cast<char*>(text.data()),size*sizeof(wchar_t));
    return !!stream && text.find(L'\0')==std::wstring::npos;
}
void WriteText(std::ostream& stream,const std::wstring& text) {
    const auto size=static_cast<std::uint32_t>(text.size());
    stream.write(reinterpret_cast<const char*>(&size),sizeof(size));
    stream.write(reinterpret_cast<const char*>(text.data()),size*sizeof(wchar_t));
}
}
std::shared_ptr<Snapshot> Load(const std::filesystem::path& cache,const std::filesystem::path& root) {
    try {
        const auto file=CacheFile(cache,root);
        std::error_code ec;const auto bytes=std::filesystem::file_size(file,ec);
        if(ec || bytes>MaxTextUnits*2+MaxEntries*5+65536)return {};
        std::ifstream stream(file,std::ios::binary);
        std::uint32_t magic=0,count=0,partial=0;
        stream.read(reinterpret_cast<char*>(&magic),4);
        stream.read(reinterpret_cast<char*>(&count),4);
        stream.read(reinterpret_cast<char*>(&partial),4);
        if(!stream || magic!=0x32494653 || count>MaxEntries || partial>1)return {};
        std::size_t budget=MaxTextUnits;std::wstring storedRoot;
        if(!ReadText(stream,storedRoot,budget) || storedRoot!=PathKey(root))return {};
        auto snapshot=std::make_shared<Snapshot>();snapshot->root=root;snapshot->fromCache=true;snapshot->partial=partial!=0;
        snapshot->entries.reserve(count);
        for(std::uint32_t i=0;i<count;++i) {
            std::wstring text;if(!ReadText(stream,text,budget) || text.empty())return {};
            const std::filesystem::path relative(text);
            if(relative.is_absolute() || relative.has_root_name() || relative.has_root_directory())return {};
            for(const auto& part:relative)if(part==L".." || part==L".")return {};
            unsigned char directory=0;stream.read(reinterpret_cast<char*>(&directory),1);
            if(!stream || directory>1)return {};
            auto name=Fold(relative.filename().wstring());
            snapshot->entries.push_back({text,name,directory!=0,PrepareName(name),Fold(text)});
        }
        if(stream.peek()!=std::char_traits<char>::eof())return {};
        std::sort(snapshot->entries.begin(),snapshot->entries.end(),[](const Entry& a,const Entry& b){return a.name==b.name?a.relative<b.relative:a.name<b.name;});
        return snapshot;
    } catch(const std::exception&) {return {};}
}
void Save(const std::filesystem::path& cache,const Snapshot& snapshot) {
    std::filesystem::path temporary;
    try {
        std::error_code ec;std::filesystem::create_directories(cache,ec);if(ec)return;
        const auto file=CacheFile(cache,snapshot.root);
        temporary=file;temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId())+L".tmp";
        std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);
        const std::uint32_t magic=0x32494653,count=static_cast<std::uint32_t>(snapshot.entries.size()),partial=snapshot.partial?1:0;
        stream.write(reinterpret_cast<const char*>(&magic),4);stream.write(reinterpret_cast<const char*>(&count),4);
        stream.write(reinterpret_cast<const char*>(&partial),4);WriteText(stream,PathKey(snapshot.root));
        for(const auto& entry:snapshot.entries) {WriteText(stream,entry.relative);const unsigned char directory=entry.directory?1:0;stream.write(reinterpret_cast<const char*>(&directory),1);}
        stream.flush();const bool good=!!stream;stream.close();
        if(!good || !MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary,ec);return;
        }
        // Only this subsystem's disposable catalog files, never source folders.
        std::vector<std::pair<std::filesystem::file_time_type,std::filesystem::path>> files;
        for(const auto& entry:std::filesystem::directory_iterator(cache)) {
            if(entry.path().extension()!=L".folders" || !entry.is_regular_file())continue;
            files.emplace_back(entry.last_write_time(),entry.path());
        }
        std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.first>b.first;});
        for(std::size_t i=8;i<files.size();++i)std::filesystem::remove(files[i].second,ec);
    } catch(const std::exception&) {std::error_code ec;if(!temporary.empty())std::filesystem::remove(temporary,ec);}
}
}
