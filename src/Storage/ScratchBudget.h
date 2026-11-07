#pragma once
// Shared by the desktop and burst worker. Only versioned, locked scratch
// sessions opt in; exports, originals and project stores are never classified
// as disposable storage by their filename or extension.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <streambuf>
#include <string>

namespace Stack::Storage::ScratchBudget {
inline constexpr std::uint64_t GiB=1024ull*1024*1024;
inline constexpr std::uint64_t DefaultLimit=16*GiB;
inline constexpr char Failure[]="Temporary bracket storage limit reached, or less than 2 GB is free. Close unused bracket tabs or raise the temporary storage limit in Settings > Storage.";
inline bool Plain(const std::filesystem::path& path) {
    auto a=GetFileAttributesW(path.c_str());
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_REPARSE_POINT);
}
inline bool Owned(const std::filesystem::path& session) {
    const auto name=session.filename().wstring();
    const auto bucket=session.parent_path().filename();
    return (bucket==L"BurstSessions"||bucket==L"Bracketing") && name.size()==32 &&
        std::all_of(name.begin(),name.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f');}) &&
        Plain(session) && Plain(session/L".stack-scratch-owner");
}
inline std::filesystem::path SessionFor(const std::filesystem::path& file) {
    for(auto p=file.parent_path();!p.empty()&&p!=p.parent_path();p=p.parent_path())
        if(Owned(p))return p;
    return {};
}
class Lock {
    HANDLE handle_=INVALID_HANDLE_VALUE;
    OVERLAPPED position_{};
    bool locked_=false;
public:
    explicit Lock(const std::filesystem::path& root) {
        handle_=CreateFileW((root/L".stack-scratch-budget-lock").c_str(),GENERIC_READ|GENERIC_WRITE,
            FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(handle_!=INVALID_HANDLE_VALUE)locked_=LockFileEx(handle_,LOCKFILE_EXCLUSIVE_LOCK,0,1,0,&position_)!=FALSE;
    }
    ~Lock(){if(locked_)UnlockFileEx(handle_,0,1,0,&position_);if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);}
    explicit operator bool()const{return locked_;}
};
inline std::uint64_t Limit(const std::filesystem::path& root) {
    unsigned gib=16;std::ifstream in(root/"ScratchLimitGiB.txt");
    if(in>>gib)return std::clamp(gib,4u,64u)*GiB;
    return DefaultLimit;
}
inline std::uint64_t Reserved(const std::filesystem::path& session) {
    const auto path=session/".stack-write-budget";
    std::error_code ec;if(!std::filesystem::exists(path,ec))return ec?UINT64_MAX:0;
    std::uint64_t bytes=0;std::ifstream in(path);
    return in>>bytes ? bytes : UINT64_MAX;
}
inline std::uint64_t Total(const std::filesystem::path& root) {
    std::uint64_t total=0;
    for(const auto* bucket:{"BurstSessions","Bracketing"}) {
        std::error_code ec;auto folder=root/bucket;if(!Plain(folder))continue;
        for(std::filesystem::directory_iterator it(folder,ec),end;!ec&&it!=end;it.increment(ec))
            if(Owned(it->path())) {auto n=Reserved(it->path());total=std::min(UINT64_MAX-n,total)+n;}
    }
    return total;
}
inline bool Reserve(const std::filesystem::path& session,std::uint64_t bytes) {
    if(session.empty())return true;
    const auto root=session.parent_path().parent_path();Lock lock(root);
    if(!lock||!Owned(session))return false;
    const auto total=Total(root),limit=Limit(root);
    std::error_code ec;const auto space=std::filesystem::space(root,ec);
    if(ec||bytes>limit||total>limit-bytes||space.available<2*GiB||bytes>space.available-2*GiB){errno=ENOSPC;return false;}
    const auto previous=Reserved(session);
    if(previous>UINT64_MAX-bytes)return false;
    std::ofstream out(session/".stack-write-budget",std::ios::trunc);
    out<<previous+bytes;out.close();return bool(out);
}
inline bool ReserveFile(const std::filesystem::path& path,std::uint64_t bytes) {return Reserve(SessionFor(path),bytes);}
inline void Release(const std::filesystem::path& session,std::uint64_t bytes) {
    if(session.empty()||!bytes)return;
    const auto root=session.parent_path().parent_path();Lock lock(root);
    if(!lock||!Owned(session))return;
    const auto previous=Reserved(session);
    if(previous==UINT64_MAX)return;
    std::ofstream out(session/".stack-write-budget",std::ios::trunc);
    out<<previous-std::min(previous,bytes);out.close();
}
// Transport files have one writer and are retired only after the command has
// completed. Return their admission after successful removal, across processes.
inline void RemoveTransferFile(const std::filesystem::path& path) noexcept {
    try {
        const auto session=SessionFor(path);
        std::error_code ec;const auto bytes=std::filesystem::file_size(path,ec);
        if(ec)return;
        if(std::filesystem::remove(path,ec)&&!ec)Release(session,bytes);
    } catch(...) { /* Conservative accounting survives failed cleanup. */ }
}
inline bool SetLimit(const std::filesystem::path& root,unsigned gib) {
    Lock lock(root);if(!lock)return false;
    std::ofstream out(root/"ScratchLimitGiB.txt",std::ios::trunc);out<<std::clamp(gib,4u,64u);out.close();return bool(out);
}

// Cached intermediates retain conservative session reservations. Streaming
// writers return unused chunk headroom on close; consumed transfer files return
// their bytes on removal. No directory census runs on the drawing thread.
class Output : public std::ostream {
    class Buffer : public std::streambuf {
        std::filebuf file_;
        std::filesystem::path session_;
        std::uint64_t available_=0,reserved_=0;
        bool closed_=false,budgetExceeded_=false;
        bool Admit(std::uint64_t size) {
            if(session_.empty())return true;
            if(size>available_) {
                const auto amount=std::max<std::uint64_t>(1024*1024,size-available_);
                if(!Reserve(session_,amount)){budgetExceeded_=true;return false;}
                available_+=amount;reserved_+=amount;
            }
            available_-=size;return true;
        }
    public:
        Buffer(const std::filesystem::path& path,std::ios::openmode mode):session_(SessionFor(path)){file_.open(path,mode|std::ios::out);}
        ~Buffer()override {if(file_.is_open())Close();}
        bool Open()const{return file_.is_open();}
        bool BudgetExceeded()const{return budgetExceeded_;}
        bool Close(){
            if(closed_)return false;
            closed_=true;const bool ok=file_.close()!=nullptr;
            // available_ is admitted but unwritten; never refund persisted data.
            if(reserved_)Release(session_,available_);
            return ok;
        }
        std::streamsize xsputn(const char* data,std::streamsize size)override {
            return size>=0 && Admit(static_cast<std::uint64_t>(size))?file_.sputn(data,size):0;
        }
        int_type overflow(int_type value)override {
            if(traits_type::eq_int_type(value,traits_type::eof()))return traits_type::not_eof(value);
            return Admit(1)?file_.sputc(traits_type::to_char_type(value)):traits_type::eof();
        }
        int sync()override{return file_.pubsync();}
    } buffer_;
public:
    Output(const std::filesystem::path& path,std::ios::openmode mode=std::ios::out):std::ostream(nullptr),buffer_(path,mode){rdbuf(&buffer_);if(!buffer_.Open())setstate(std::ios::failbit);}
    void close(){if(!buffer_.Close())setstate(std::ios::failbit);}
    bool BudgetExceeded()const{return buffer_.BudgetExceeded();}
};
}
