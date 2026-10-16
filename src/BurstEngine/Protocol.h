#pragma once
#include "ThirdParty/json.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Stack::BurstEngine {
inline constexpr unsigned ProtocolVersion=1;
using Json=nlohmann::json;
#ifdef _WIN32
class Handle {
public:
    Handle()=default;
    explicit Handle(HANDLE value):value_(value){}
    ~Handle(){Reset();}
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
    Handle(Handle&& other) noexcept:value_(other.Release()){}
    Handle& operator=(Handle&& other) noexcept {if(this!=&other)Reset(other.Release());return *this;}
    HANDLE Get() const{return value_;}
    explicit operator bool()const{return value_&&value_!=INVALID_HANDLE_VALUE;}
    HANDLE Release(){const auto value=value_;value_=nullptr;return value;}
    void Reset(HANDLE value=nullptr){if(*this)CloseHandle(value_);value_=value;}
private:
    HANDLE value_=nullptr;
};
void WriteMessage(HANDLE,const Json&);
Json ReadMessage(HANDLE);
std::string WindowsError(const char* operation);
#endif
std::string UniqueId();
std::string PathText(const std::filesystem::path&);
std::filesystem::path TextPath(const std::string&);
void WriteDocument(const std::filesystem::path&,const Json&);
Json ReadDocument(const std::filesystem::path&);
}
