#pragma once
#include "Hdr/ImageFile.h"
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace Stack::Queue {
// One application-owned setting. Read once at startup; only explicit changes
// start a writer. Shutdown joins it so the last selection cannot be discarded.
class ExportPreferences {
public:
    explicit ExportPreferences(std::filesystem::path path);
    ~ExportPreferences();
    Hdr::FileFormat Read() const;
    void Set(Hdr::FileFormat format);
    std::string Flush();
private:
    void WritePending();
    std::filesystem::path m_Path;
    mutable std::mutex m_Mutex;
    Hdr::FileFormat m_Format=Hdr::FileFormat::GainMapJpeg;
    std::uint64_t m_Revision=0;
    bool m_Writing=false;
    std::string m_Error;
    std::thread m_Writer;
};
}
