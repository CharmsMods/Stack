#include "Library/Internal/LibraryFileRead.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct ThrowingBuffer : std::streambuf {
    int_type underflow() override { throw std::runtime_error("read failure"); }
};
}

int main() {
    using namespace Stack::Library::StorageHelpers;
    try {
        std::vector<unsigned char> bytes {9, 9};
        std::istringstream exact("abc");
        Require(ReadExactStreamBytes(exact, 3, bytes) &&
            bytes == std::vector<unsigned char>({'a','b','c'}), "exact stream");
        std::istringstream shortRead("abc");
        Require(!ReadExactStreamBytes(shortRead, 5, bytes) && bytes.empty(), "short read clears output");
        bytes = {9};
        std::istringstream grown("abcde");
        Require(!ReadExactStreamBytes(grown, 3, bytes) && bytes.empty(), "growing stream clears output");
        std::istringstream empty;
        Require(ReadExactStreamBytes(empty, 0, bytes) && bytes.empty(), "empty stream");
        std::istringstream negative("a");
        Require(!ReadExactStreamBytes(negative, -1, bytes) && bytes.empty(), "negative size");
        std::istringstream failed("abc");
        failed.setstate(std::ios::failbit);
        Require(!ReadExactStreamBytes(failed, 3, bytes) && bytes.empty(), "failed stream");
        ThrowingBuffer buffer;
        std::istream throwing(&buffer);
        throwing.exceptions(std::ios::badbit);
        Require(!ReadExactStreamBytes(throwing, 1, bytes) && bytes.empty(), "throwing stream");
        const auto root = std::filesystem::temp_directory_path() /
            ("stack-file-read-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Require(std::filesystem::create_directory(root), "owned temporary directory");
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(path, ec); } } cleanup {root};
        const std::string contents(65539, 'x');
        { std::ofstream output(root / "fixture.bin", std::ios::binary); output << contents; Require(output.good(), "write fixture"); }
        Require(ReadFileBytes(root / "fixture.bin", bytes) &&
            std::string(bytes.begin(), bytes.end()) == contents, "actual multichunk file");
        Require(!ReadFileBytes(root / "missing.bin", bytes) && bytes.empty(), "missing file clears output");
        { std::ofstream output(root / "empty.bin", std::ios::binary); }
        Require(ReadFileBytes(root / "empty.bin", bytes) && bytes.empty(), "actual empty file");
        std::cout << "Library exact file read tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
