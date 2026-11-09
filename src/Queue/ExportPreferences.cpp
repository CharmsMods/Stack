#include "ExportPreferences.h"
#include "ThirdParty/json.hpp"
#include "Utils/AtomicExportFile.h"
#include <fstream>
#include <iostream>

namespace Stack::Queue {
namespace {
const char* Id(Hdr::FileFormat format) {
    switch(format) {
    case Hdr::FileFormat::SdrPng: return "sdr-png";
    case Hdr::FileFormat::SdrJpeg: return "sdr-jpeg";
    case Hdr::FileFormat::PqPng: return "hdr-png";
    default: return "hdr-jpeg";
    }
}
}
ExportPreferences::ExportPreferences(std::filesystem::path path):m_Path(std::move(path)) {
    std::error_code error;
    if(std::filesystem::file_size(m_Path,error)>4096 || error) return;
    std::ifstream file(m_Path);
    const auto json=nlohmann::json::parse(file,nullptr,false);
    if(!json.is_object()) return;
    const auto it=json.find("format");
    if(it==json.end() || !it->is_string()) return;
    for(auto format:Hdr::ExportFormats) if(*it==Id(format)) {m_Format=format;break;}
}
ExportPreferences::~ExportPreferences() { Flush(); }
Hdr::FileFormat ExportPreferences::Read() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Format;
}
void ExportPreferences::Set(Hdr::FileFormat format) {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if(m_Format==format && m_Error.empty()) return;
        m_Format=format;++m_Revision;
        if(m_Writing) return;
        m_Writing=true;
    }
    if(m_Writer.joinable()) m_Writer.join();
    try {m_Writer=std::thread([this]{WritePending();});}
    catch(const std::exception& e) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Error=e.what();m_Writing=false;
    }
}
std::string ExportPreferences::Flush() {
    if(m_Writer.joinable()) m_Writer.join();
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Error;
}
void ExportPreferences::WritePending() {
    for(;;) {
        Hdr::FileFormat format;std::uint64_t revision;
        {std::lock_guard<std::mutex> lock(m_Mutex);format=m_Format;revision=m_Revision;}
        std::string error;
        try {
            std::filesystem::create_directories(m_Path.parent_path());
            const auto text=nlohmann::json{{"version",1},{"format",Id(format)}}.dump(2);
            FileSave::ExportFileTarget target;
            if(FileSave::PrepareExportFileTarget(m_Path,{}, {".json"},target,error))
                FileSave::WriteExportFileAtomically(target,{text.begin(),text.end()},error);
        } catch(const std::exception& e) {error=e.what();}
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_Error=std::move(error);
        if(!m_Error.empty()) std::cerr<<"[Queue] Could not save export preference: "<<m_Error<<'\n';
        if(revision==m_Revision) {m_Writing=false;return;}
    }
}
}
