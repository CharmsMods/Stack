#include "Library/Internal/LibraryStorageHelpers.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#if !defined(_WIN32)
#include <csignal>
#include <sys/resource.h>
#endif

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void Fixture(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary);
    output << bytes;
    output.close();
    Require(output.good(), "Could not write fixture");
}
struct TemporaryDirectory {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("stack-library-write-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() {
        Require(std::filesystem::create_directory(root), "Could not reserve fixtures");
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};
#if !defined(_WIN32)
struct FileSizeLimit {
    rlimit previous{};
    using Handler = void (*)(int);
    Handler handler;
    FileSizeLimit() {
        Require(getrlimit(RLIMIT_FSIZE, &previous) == 0, "Could not read test file limit");
        handler = std::signal(SIGXFSZ, SIG_IGN);
        auto limit = previous;
        limit.rlim_cur = 1;
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0) {
            std::signal(SIGXFSZ, handler);
            throw std::runtime_error("Could not set temporary test file limit");
        }
    }
    ~FileSizeLimit() {
        setrlimit(RLIMIT_FSIZE, &previous);
        std::signal(SIGXFSZ, handler);
    }
};
#endif
}

int main() try {
    using Stack::Library::StorageHelpers::WriteFileBytes;
    TemporaryDirectory fixture;
    const std::vector<unsigned char> bytes {'N', 'E', 'W'};
    for (const auto* name : {"asset.png", "asset.JPG", "asset.jpeg", "asset.bmp", "asset.tga"}) {
        const auto path = fixture.root / name;
        Require(WriteFileBytes(path, bytes), "Raster write failed");
        Require(Read(path) == "NEW", "Raster bytes differ");
        Require(!WriteFileBytes(path, {}), "Empty raster was accepted");
        Require(Read(path) == "NEW", "Failed write destroyed previous raster");
        Require(WriteFileBytes(path, {'O', 'K'}), "Owned raster replacement failed");
        Require(Read(path) == "OK", "Replacement bytes differ");
    }
    const auto original = fixture.root / "camera.DNG";
    Fixture(original, "ORIGINAL");
    Require(!WriteFileBytes(original, bytes), "Camera extension was accepted");
    Require(Read(original) == "ORIGINAL", "Camera original changed");

    std::error_code error;
    const auto hardlink = fixture.root / "hardlink.png";
    std::filesystem::create_hard_link(original, hardlink, error);
    if (!error) {
        Require(!WriteFileBytes(hardlink, {}), "Empty alias write was accepted");
        Require(Read(original) == "ORIGINAL" && Read(hardlink) == "ORIGINAL",
            "Failed alias write changed original");
        Require(WriteFileBytes(hardlink, bytes), "Atomic alias replacement failed");
        Require(Read(original) == "ORIGINAL" && Read(hardlink) == "NEW",
            "Alias replacement modified original inode");
    } else std::cout << "SKIP hardlink unavailable: " << error.message() << '\n';
    const auto symlink = fixture.root / "symlink.png";
    error.clear();
    std::filesystem::create_symlink(original, symlink, error);
    if (!error) {
        Require(!WriteFileBytes(symlink, bytes), "Symbolic link destination was accepted");
        Require(Read(original) == "ORIGINAL", "Symbolic link write modified original");
    } else std::cout << "SKIP symlink unavailable: " << error.message() << '\n';

    const auto directory = fixture.root / "directory.png";
    std::filesystem::create_directory(directory);
    Fixture(directory / "sentinel", "KEEP");
    Require(!WriteFileBytes(directory, bytes), "Directory destination was accepted");
    Require(Read(directory / "sentinel") == "KEEP", "Failed write modified directory");
    const auto blocker = fixture.root / "parent-file";
    Fixture(blocker, "KEEP");
    Require(!WriteFileBytes(blocker / "asset.png", bytes), "Invalid parent accepted");
    Require(Read(blocker) == "KEEP", "Failed write modified parent");
#if !defined(_WIN32)
    const auto limited = fixture.root / "limited.png";
    Fixture(limited, "EXISTING");
    {
        // The small write can stay buffered until close, reproducing the old
        // false-success path without replacing a device or touching real data.
        FileSizeLimit limit;
        Require(!WriteFileBytes(limited, std::vector<unsigned char>(1024, 'X')),
            "Buffered close failure was reported as success");
    }
    Require(Read(limited) == "EXISTING", "Failed staged write destroyed old bytes");
#endif
    for (const auto& entry : std::filesystem::directory_iterator(fixture.root))
        Require(entry.path().filename().u8string().find(".stack-export-") != 0,
            "Staging directory leaked");
    std::cout << "Library raster atomic writes, failure preservation and aliases passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
