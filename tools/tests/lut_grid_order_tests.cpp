#include "Color/LutCreator.h"
#include "Color/LutImporter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct TemporaryDirectory {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("stack-lut-grid-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() {
        Require(std::filesystem::create_directory(root), "Could not reserve LUT fixtures");
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};
void Write(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path);
    output << text;
    output.close();
    Require(output.good(), "Could not write synthetic LUT fixture");
}
std::array<float, 3> Map(const std::array<float, 3>& rgb, bool identity) {
    if (identity) return rgb;
    return { .1f + .4f * rgb[0] + .2f * rgb[2],
        .05f + .25f * rgb[0] + .5f * rgb[1],
        .15f + .25f * rgb[1] + .4f * rgb[2] };
}
void Close(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    for (int channel = 0; channel < 3; ++channel)
        Require(std::abs(a[channel] - b[channel]) < 1e-6f,
            "LUT sampling changed the declared RGB mapping");
}

void CheckMapping(const std::filesystem::path& root, int size, bool identity) {
    const float domainMin = identity ? 0.f : -1.f;
    const float domainMax = identity ? 1.f : 2.f;
    std::ostringstream cube, spi;
    cube << "LUT_3D_SIZE " << size << "\nDOMAIN_MIN " << domainMin << ' ' << domainMin << ' '
         << domainMin << "\nDOMAIN_MAX " << domainMax << ' ' << domainMax << ' ' << domainMax << '\n';
    spi << "SPILUT 1.0\n3 3\nFrom " << domainMin << ' ' << domainMax << '\n'
        << size << ' ' << size << ' ' << size << '\n';
    const auto append = [&](std::ostream& out, int index, bool indexed) {
        const int r = index % size, g = (index / size) % size, b = index / (size * size);
        const auto value = Map({float(r) / (size - 1), float(g) / (size - 1), float(b) / (size - 1)}, identity);
        if (indexed) out << r << ' ' << g << ' ' << b << ' ';
        out << value[0] << ' ' << value[1] << ' ' << value[2] << '\n';
    };
    const int count = size * size * size;
    for (int i = 0; i < count; ++i) {
        append(cube, i, false);
        // Five is coprime to both fixture grids (8 and 27 samples). Shuffle
        // every SPI3D row to ensure coordinates, not file order, own placement.
        append(spi, (i * 5 + 1) % count, true);
    }
    const auto cubePath = root / "equivalent.cube";
    const auto spiPath = root / "equivalent.spi3d";
    Write(cubePath, cube.str());
    Write(spiPath, spi.str());
    const auto cubeLut = ColorLut::ImportLutFile(cubePath.string());
    const auto spiLut = ColorLut::ImportLutFile(spiPath.string());
    Require(cubeLut.success && spiLut.success, "Equivalent synthetic LUTs must import");
    Require(cubeLut.payload.lut3D.values == spiLut.payload.lut3D.values,
        "SPI3D coordinates must match canonical red-fastest cube storage");
    Require(cubeLut.payload.lut3D.domainMin == spiLut.payload.lut3D.domainMin &&
        cubeLut.payload.lut3D.domainMax == spiLut.payload.lut3D.domainMax,
        "SPI3D import must preserve the declared domain");
    const std::array<std::array<float, 3>, 5> probes = {{
        {domainMax, domainMin, domainMin}, {domainMin, domainMin, domainMax},
        {.2f, .4f, .8f}, {.9f, .1f, .3f}, {-2.f, 3.f, .6f}
    }};
    for (const auto& probe : probes) {
        std::array<float, 3> normalized{};
        for (int channel = 0; channel < 3; ++channel)
            normalized[channel] = std::clamp((probe[channel] - domainMin) / (domainMax - domainMin), 0.f, 1.f);
        const auto expected = Map(normalized, identity);
        Close(ColorLut::SampleLut3D(cubeLut.payload.lut3D, probe), expected);
        Close(ColorLut::SampleLut3D(spiLut.payload.lut3D, probe), expected);
    }
}

void CheckInvalidGrid(const std::filesystem::path& root) {
    const auto path = root / "invalid.spi3d";
    // Preserve rejection of duplicate/missing coordinates, out-of-range and
    // noninteger indices, wrong row counts and sparse enormous declarations.
    for (const char* first : {"0 0 1", "2 0 0", "-1 0 0", "0.5 0 0"}) {
        std::ostringstream spi;
        spi << "SPILUT 1.0\n3 3\n2 2 2\n" << first << " 0 0 0\n";
        for (int i = 1; i < 8; ++i)
            spi << i / 4 << ' ' << (i / 2) % 2 << ' ' << i % 2 << " 0 0 0\n";
        Write(path, spi.str());
        Require(!ColorLut::ImportLutFile(path.string()).success, "Malformed SPI3D coordinates must be rejected");
    }
    for (const char* grid : {"2 2 2", "100000 100000 100000"}) {
        Write(path, std::string("SPILUT 1.0\n3 3\n") + grid + "\n0 0 0 0 0 0\n");
        Require(!ColorLut::ImportLutFile(path.string()).success, "Incomplete SPI3D grids must be rejected before allocation");
    }
}
}

int main() try {
    TemporaryDirectory temporary;
    CheckMapping(temporary.root, 2, true);
    CheckMapping(temporary.root, 3, false);
    CheckInvalidGrid(temporary.root);
    std::cout << "LUT grid order: shuffled identity/asymmetric imports, canonical cube equivalence, sampling and malformed rejection passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
