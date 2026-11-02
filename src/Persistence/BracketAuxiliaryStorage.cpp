#include "BracketAuxiliaryStorage.h"
#include <zlib.h>
#include <array>
#include <fstream>
#include <stdexcept>

namespace Stack::Project::BracketAuxiliaryStorage {
namespace {
class File {
public:
    File(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
        handle = gzopen_w(path.c_str(),mode);
#else
        handle = gzopen(path.c_str(),mode);
#endif
        if (!handle) throw std::runtime_error("Could not open compressed bracket data.");
    }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    ~File() { if (handle) gzclose(handle); }
    void Finish() {
        const int status=gzclose(handle);
        handle=nullptr;
        if (status!=Z_OK) throw std::runtime_error("Could not finish compressed bracket data.");
    }
    gzFile handle=nullptr;
};
class InputBuffer final : public std::streambuf {
public:
    explicit InputBuffer(gzFile file) : file_(file) {}
    int_type underflow() override {
        const int count=gzread(file_,bytes_.data(),static_cast<unsigned>(bytes_.size()));
        if (count<0) throw std::runtime_error("The compressed bracket data is damaged.");
        if (!count) return traits_type::eof();
        setg(bytes_.data(),bytes_.data(),bytes_.data()+count);
        return traits_type::to_int_type(*gptr());
    }
private:
    gzFile file_;
    std::array<char,65536> bytes_;
};
}
void Compress(const std::filesystem::path& source,const std::filesystem::path& destination) {
    std::ifstream input(source,std::ios::binary);
    if (!input) throw std::runtime_error("Could not open bracket maps for compression.");
    File output(destination,"wb1");
    std::array<char,65536> block;
    while (input.read(block.data(),block.size()) || input.gcount()) {
        const auto count=static_cast<unsigned>(input.gcount());
        if (gzwrite(output.handle,block.data(),count)!=static_cast<int>(count))
            throw std::runtime_error("Could not write compressed bracket maps.");
    }
    if (!input.eof() || input.bad()) throw std::runtime_error("Could not finish reading bracket maps.");
    output.Finish();
}
void Read(const std::filesystem::path& path,const std::function<void(std::istream&)>& consume) {
    File file(path,"rb");
    InputBuffer buffer(file.handle);
    std::istream input(&buffer);
    input.exceptions(std::ios::badbit);
    consume(input);
    if (input.peek()!=std::istream::traits_type::eof()) throw std::runtime_error("Unexpected trailing bracket data.");
    file.Finish();
}
}
