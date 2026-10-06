#include "Raw/DngMetadataSupplement.h"
#include "Raw/Internal/DngTiffReader.h"
#include "Raw/RawImageData.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Bytes = std::vector<std::uint8_t>;
using Reader = Raw::Internal::DngTiffReader;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void Put(Bytes& bytes, std::size_t offset, std::uint64_t value, std::size_t size, bool little) {
    if (offset + size > bytes.size()) bytes.resize(offset + size);
    for (std::size_t i = 0; i < size; ++i) {
        bytes[offset + (little ? i : size - 1 - i)] = static_cast<std::uint8_t>(value >> (i * 8));
    }
}

Bytes Integers(std::initializer_list<std::uint64_t> values, std::size_t size, bool little) {
    Bytes bytes;
    for (auto value : values) Put(bytes, bytes.size(), value, size, little);
    return bytes;
}

Bytes Doubles(std::initializer_list<double> values, bool little) {
    Bytes bytes;
    for (double value : values) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        Put(bytes, bytes.size(), bits, sizeof(bits), little);
    }
    return bytes;
}

struct Tag {
    std::uint16_t id;
    std::uint16_t type;
    std::uint32_t count;
    Bytes bytes;
};

struct Tiff {
    bool little;
    Bytes bytes;
    explicit Tiff(bool littleEndian) : little(littleEndian), bytes(8) {
        bytes[0] = bytes[1] = little ? 'I' : 'M';
        Put(bytes, 2, 42, 2, little);
    }
    std::uint32_t Directory(const std::vector<Tag>& tags, std::uint32_t next = 0) {
        const auto offset = static_cast<std::uint32_t>(bytes.size());
        if (offset == 8) Put(bytes, 4, offset, 4, little);
        bytes.resize(offset + 2 + tags.size() * 12 + 4);
        Put(bytes, offset, tags.size(), 2, little);
        for (std::size_t i = 0; i < tags.size(); ++i) {
            const Tag& tag = tags[i];
            const auto p = offset + 2 + i * 12;
            Put(bytes, p, tag.id, 2, little);
            Put(bytes, p + 2, tag.type, 2, little);
            Put(bytes, p + 4, tag.count, 4, little);
            if (tag.bytes.size() <= 4) {
                std::copy(tag.bytes.begin(), tag.bytes.end(), bytes.begin() + p + 8);
            } else {
                Put(bytes, p + 8, bytes.size(), 4, little);
                bytes.insert(bytes.end(), tag.bytes.begin(), tag.bytes.end());
            }
        }
        Put(bytes, offset + 2 + tags.size() * 12, next, 4, little);
        return offset;
    }
    void Write(const std::filesystem::path& path) const {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        Require(file.good(), "write fixture");
    }
};

Bytes GainMap() {
    Bytes bytes = Integers({ 1, 9, 0x01030000, 0, 92 }, 4, false);
    const auto shape = Integers({ 0, 0, 100, 120, 0, 1, 2, 2, 2, 2 }, 4, false);
    bytes.insert(bytes.end(), shape.begin(), shape.end());
    const auto spacing = Doubles({ 0.5, 0.5, 0, 0 }, false);
    bytes.insert(bytes.end(), spacing.begin(), spacing.end());
    Put(bytes, bytes.size(), 1, 4, false);
    for (float gain : { 1.0f, 1.25f, 1.5f, 2.0f }) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &gain, sizeof(bits));
        Put(bytes, bytes.size(), bits, 4, false);
    }
    return bytes;
}

void TestSupplement(const std::filesystem::path& path, bool little) {
    Tiff tiff(little);
    auto ints = [little](std::initializer_list<std::uint64_t> values, int size) { return Integers(values, size, little); };
    tiff.Directory({
        { 330, 13, 1, ints({0}, 4) },
        { 274, 3, 1, ints({6}, 2) },
        { 50708, 2, 12, Bytes {'T','e','s','t',' ','c','a','m','e','r','a',0} },
        // Unknown blobs and profile map presence flags never need their payloads.
        { 65000, 7, 0xffffffff, ints({0xffffffff}, 4) },
        { 52525, 7, 0xffffffff, ints({0xffffffff}, 4) }
    });
    const auto gain = GainMap();
    const auto raw = tiff.Directory({
        { 256, 4, 1, ints({120}, 4) }, { 257, 4, 1, ints({100}, 4) },
        { 262, 3, 1, ints({32803}, 2) }, { 259, 3, 1, ints({1}, 2) },
        { 33421, 3, 2, ints({2,2}, 2) }, { 33422, 1, 4, ints({0,1,1,2}, 1) },
        { 50706, 1, 4, ints({1,4,0,0}, 1) }, { 50710, 1, 3, ints({0,1,2}, 1) },
        { 50712, 3, 4, ints({0,1,200,65535}, 2) },
        { 50713, 3, 2, ints({2,2}, 2) }, { 50714, 5, 4, ints({64,1,65,1,66,1,67,1}, 4) },
        { 50715, 10, 2, ints({0xffffffff,2,1,2}, 4) },
        { 50717, 4, 1, ints({4095}, 4) },
        { 50721, 10, 9, ints({1,1,0,1,0,1,0,1,1,1,0,1,0,1,0,1,1,1}, 4) },
        { 50727, 5, 3, ints({2,1,1,1,1,1}, 4) },
        { 50728, 5, 3, ints({1,2,1,1,2,3}, 4) },
        { 50730, 10, 1, ints({0xffffffff,2}, 4) },
        { 50829, 4, 4, ints({2,4,98,116}, 4) },
        { 50830, 4, 4, ints({0,0,2,120}, 4) },
        { 51009, 7, static_cast<std::uint32_t>(gain.size()), gain },
        { 51041, 12, 2, Doubles({0.02,0.003}, little) }
    }, 8); // Cycle back to IFD0.
    Put(tiff.bytes, 18, raw, 4, little);
    tiff.Write(path);
    Raw::RawMetadata metadata;
    metadata.isDng = true;
    Require(Raw::ApplyDngSupplement(path, metadata), "supplement succeeds");
    Require(metadata.warnings.empty(), "unused payloads must not be read or reported as malformed");
    Require(metadata.orientation == 6 && metadata.dngUniqueCameraModel == "Test camera", "IFD0 metadata preserved");
    Require(metadata.cfaPattern == Raw::CfaPattern::RGGB && metadata.mosaiced, "SubIFD CFA selection");
    Require(metadata.blackLevel == 65.5f && metadata.perChannelBlack[3] == 67, "black pattern and average");
    Require(metadata.whiteLevel == 4095 && metadata.bitDepth == 12, "white level and bit depth");
    Require(metadata.dngLinearizationTable == std::vector<std::uint16_t>({0,1,200,65535}), "linearization payload");
    Require(metadata.dngBlackLevelDeltaH == std::vector<float>({-0.5f,0.5f}), "signed rational payload");
    Require(metadata.hasDngColorMatrix1 && metadata.dngColorMatrix1[8] == 1, "color matrix");
    Require(metadata.hasDngAsShotNeutral && metadata.cameraWhiteBalance[0] == 1 &&
        std::abs(metadata.cameraWhiteBalance[2] - 1.5f) < 1e-6f, "white balance");
    Require(metadata.hasDngBaselineExposure && metadata.dngBaselineExposure == -0.5f, "signed exposure");
    Require(metadata.hasDngActiveArea && metadata.dngActiveArea.right == 116 &&
        metadata.dngMaskedAreas.size() == 1, "sensor areas");
    Require(metadata.hasDngNoiseProfile && metadata.dngNoiseProfile[0].readNoiseVariance == 0.003, "double noise profile");
    Require(metadata.hasDngProfileGainTableMap, "profile map presence");
    Require(metadata.dngGainMapCount == 1 && metadata.dngGainMaps[0].gains ==
        std::vector<float>({1,1.25f,1.5f,2}), "big endian gain map in either TIFF byte order");
    Reader reader(path);
    Require(reader.ReadIfdTree().size() == 2, "cyclic directory traversal terminates");
}

void TestBoundedReads(const std::filesystem::path& path) {
    Tiff tiff(true);
    tiff.Directory({ {50717, 4, 2, Integers({1,2}, 4, true)} });
    // The numeric payload straddles a cache boundary. Most of the file is unused.
    Put(tiff.bytes, 18, 65535, 4, true);
    Put(tiff.bytes, 65535, 4095, 4, true);
    Put(tiff.bytes, 65539, 4094, 4, true);
    tiff.Write(path);
    std::filesystem::resize_file(path, 128 * 1024 * 1024);
    Reader reader(path);
    const auto entries = reader.ReadEntries(reader.FirstIfd());
    Require(entries.size() == 1 && reader.NumberValues(entries[0]) == std::vector<double>({4095,4094}), "cross-page read");
    Require(reader.BytesRead() <= 128 * 1024, "metadata reads must be independent of total file size");
    auto huge = entries[0];
    huge.type = 7; huge.count = 80 * 1024 * 1024; huge.valueOffset = 65535;
    const auto before = reader.BytesRead();
    Require(reader.RawBytes(huge).empty() && !reader.Warning().empty(), "large payload budget");
    Require(reader.BytesRead() == before, "reject huge payload before reading or allocating");
    huge.type = 4; huge.count = 2 * 1024 * 1024;
    Require(reader.NumberValues(huge).empty(), "numeric allocation budget");
}

void TestMalformedAndCancelled(const std::filesystem::path& path) {
    Tiff tiff(false);
    tiff.Directory({ {50717, 12, 1, Doubles({std::numeric_limits<double>::infinity()}, false)},
        {50829, 12, 4, Doubles({0,0,1e100,1e100}, false)} });
    tiff.Write(path);
    Raw::RawMetadata metadata;
    metadata.isDng = true;
    metadata.whiteLevel = 1234;
    Require(Raw::ApplyDngSupplement(path, metadata), "malformed supplement returns diagnostics");
    Require(metadata.whiteLevel == 1234 && !metadata.hasDngActiveArea && !metadata.warnings.empty(), "invalid numbers preserve safe defaults");
    int checks = 0;
    Reader reader(path, [&] { return ++checks == 12; });
    reader.ReadIfdTree();
    Require(reader.Cancelled() && checks == 12, "cancellation latches during directory reads");
    Require(!Raw::ApplyDngSupplement(path, metadata, [] { return true; }), "supplement cancellation reported");

    Tiff empty(true);
    empty.Directory({}, 14);
    empty.Directory({ {50717, 4, 1, Integers({4095}, 4, true)} });
    empty.Write(path);
    Reader linked(path);
    Require(linked.ReadIfdTree().size() == 2, "empty directory follows next IFD");

    Put(empty.bytes, 4, 0xfffffffc, 4, true);
    empty.Write(path);
    Reader invalid(path);
    invalid.ReadIfdTree();
    Require(!invalid.Warning().empty(), "out of range IFD reported");
}

void TestFocusDistance(const std::filesystem::path& path, bool little) {
    Tiff tiff(little);
    tiff.Directory({{34665,4,1,Integers({0},4,little)}});
    const auto exif=tiff.Directory({{37382,5,1,Integers({653,1000},4,little)}});
    Put(tiff.bytes,8+2+8,exif,4,little);
    tiff.Write(path);
    Raw::RawMetadata metadata;metadata.isDng=true;
    Require(Raw::ApplyDngSupplement(path,metadata),"read focus metadata");
    Require(metadata.hasFocusDistance&&std::abs(metadata.focusDistanceMeters-.653f)<1e-6,"Exif focus distance retained");
}

void TestNumericTypesAndLimits(const std::filesystem::path& path, bool little) {
    Tiff tiff(little);
    tiff.Directory({
        {1, 6, 1, Integers({0xfe}, 1, little)},
        {2, 8, 1, Integers({0xfffd}, 2, little)},
        {3, 9, 1, Integers({0xfffffffc}, 4, little)},
        {4, 11, 1, Integers({0x3fc00000}, 4, little)},
        {5, 12, 1, Doubles({-2.25}, little)},
        {6, 5, 1, Integers({10,4}, 4, little)},
        {7, 10, 1, Integers({10,0xfffffffc}, 4, little)},
        {8, 5, 1, Integers({10,0}, 4, little)},
        {9, 12, 1, Doubles({std::numeric_limits<double>::quiet_NaN()}, little)}
    });
    tiff.Write(path);
    {
        Reader reader(path);
        const auto entries = reader.ReadEntries(reader.FirstIfd());
        const double expected[] { -2, -3, -4, 1.5, -2.25, 2.5, -2.5 };
        Require(entries.size() == 9, "numeric fixture directory");
        for (std::size_t i = 0; i < 7; ++i) {
            Require(reader.NumberValues(entries[i]) == std::vector<double>({expected[i]}), "numeric TIFF type decode");
        }
        Require(reader.NumberValues(entries[7]).empty() && reader.NumberValues(entries[8]).empty(),
            "zero denominator and NaN rejected");
    }
    Tiff chain(little);
    for (int i = 0; i < 70; ++i) chain.Directory({}, static_cast<std::uint32_t>(chain.bytes.size() + 6));
    chain.Write(path);
    {
        Reader reader(path);
        Require(reader.ReadIfdTree().size() == 64 && !reader.Warning().empty(), "directory traversal budget");
    }
    tiff.bytes.pop_back();
    tiff.Write(path);
    Reader truncated(path);
    const auto entries = truncated.ReadEntries(truncated.FirstIfd());
    Require(truncated.NumberValues(entries.back()).empty() && !truncated.Warning().empty(), "truncated payload rejected");
}
} // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() /
        ("stack-dng-metadata-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".dng");
    int result = 0;
    try {
        TestSupplement(path, true);
        TestSupplement(path, false);
        TestFocusDistance(path,true);
        TestFocusDistance(path,false);
        TestBoundedReads(path);
        TestMalformedAndCancelled(path);
        TestNumericTypesAndLimits(path, true);
        TestNumericTypesAndLimits(path, false);
        std::cout << "DNG metadata tests passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    return result;
}
