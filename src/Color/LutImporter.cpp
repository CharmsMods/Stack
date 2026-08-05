#include "Color/LutImporter.h"
#include "Color/LutCreator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ColorLut {
namespace {

bool TryComputeLut1DValueCount(int size, std::size_t& outCount) {
    outCount = 0;
    if (size <= 0) {
        return false;
    }
    const std::size_t edge = static_cast<std::size_t>(size);
    if (edge > std::numeric_limits<std::size_t>::max() / 3u) {
        return false;
    }
    outCount = edge * 3u;
    return true;
}

bool TryComputeLut3DValueCount(int size, std::size_t& outCount) {
    outCount = 0;
    if (size <= 0) {
        return false;
    }
    const std::size_t edge = static_cast<std::size_t>(size);
    if (edge > std::numeric_limits<std::size_t>::max() / edge) {
        return false;
    }
    const std::size_t square = edge * edge;
    if (square > std::numeric_limits<std::size_t>::max() / edge) {
        return false;
    }
    const std::size_t cube = square * edge;
    if (cube > std::numeric_limits<std::size_t>::max() / 3u) {
        return false;
    }
    outCount = cube * 3u;
    return true;
}

bool TryParseGridIndex(float value, int& outIndex) {
    if (!std::isfinite(value)) {
        return false;
    }
    const double rounded = std::round(static_cast<double>(value));
    if (rounded < 0.0 ||
        rounded > static_cast<double>(std::numeric_limits<int>::max()) ||
        std::abs(static_cast<double>(value) - rounded) > 0.0001) {
        return false;
    }
    outIndex = static_cast<int>(rounded);
    return true;
}

std::string Trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string Lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string FileStem(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string FileName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string FileExtension(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    return dot == std::string::npos ? std::string() : Lowercase(path.substr(dot));
}

bool ParseIntStrict(const std::string& token, int& outValue) {
    if (token.empty()) {
        return false;
    }
    char* end = nullptr;
    const long value = std::strtol(token.c_str(), &end, 10);
    if (!end || *end != '\0') {
        return false;
    }
    if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) {
        return false;
    }
    outValue = static_cast<int>(value);
    return true;
}

bool ParseFloatStrict(const std::string& token, float& outValue) {
    if (token.empty()) {
        return false;
    }
    char* end = nullptr;
    const float value = std::strtof(token.c_str(), &end);
    if (!end || *end != '\0' || !std::isfinite(value)) {
        return false;
    }
    outValue = value;
    return true;
}

bool ParseFloatTokens(const std::string& line, std::vector<float>& outValues) {
    outValues.clear();
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) {
        if (token == "{" || token == "}") {
            continue;
        }
        float value = 0.0f;
        if (!ParseFloatStrict(token, value)) {
            return false;
        }
        outValues.push_back(value);
    }
    return true;
}

bool ParseIntTokens(const std::string& line, std::vector<int>& outValues) {
    outValues.clear();
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) {
        int value = 0;
        if (!ParseIntStrict(token, value)) {
            return false;
        }
        outValues.push_back(value);
    }
    return true;
}

std::array<float, 3> Repeated3(float value) {
    return { value, value, value };
}

void SetUniformDomain(
    std::array<float, 3>& outMin,
    std::array<float, 3>& outMax,
    const std::vector<float>& values) {
    if (values.size() == 2) {
        outMin = Repeated3(values[0]);
        outMax = Repeated3(values[1]);
    } else if (values.size() == 6) {
        outMin = { values[0], values[1], values[2] };
        outMax = { values[3], values[4], values[5] };
    }
}

bool ValidateStage(const Lut1DStage& stage) {
    std::size_t expected = 0;
    if (!TryComputeLut1DValueCount(stage.size, expected)) {
        return false;
    }
    if (stage.values.size() != expected) {
        return false;
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
        if (!IsFiniteFloat(stage.domainMin[channel]) ||
            !IsFiniteFloat(stage.domainMax[channel]) ||
            stage.domainMax[channel] <= stage.domainMin[channel]) {
            return false;
        }
    }
    for (float value : stage.values) {
        if (!IsFiniteFloat(value)) {
            return false;
        }
    }
    return true;
}

bool ValidateStage(const Lut3DStage& stage) {
    std::size_t expected = 0;
    if (!TryComputeLut3DValueCount(stage.size, expected)) {
        return false;
    }
    if (stage.values.size() != expected) {
        return false;
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
        if (!IsFiniteFloat(stage.domainMin[channel]) ||
            !IsFiniteFloat(stage.domainMax[channel]) ||
            stage.domainMax[channel] <= stage.domainMin[channel]) {
            return false;
        }
    }
    for (float value : stage.values) {
        if (!IsFiniteFloat(value)) {
            return false;
        }
    }
    return true;
}

bool FinalizeResult(LutImportResult& result, const std::string& path) {
    result.payload.sourcePath = path;
    if (result.payload.label.empty()) {
        result.payload.label = FileName(path);
    }
    if (result.payload.importedTitle.empty()) {
        result.payload.importedTitle = FileStem(path);
    }
    if (!result.success) {
        result.payload.importError = result.message.empty() ? "Failed to import LUT." : result.message;
        ClearCanonicalLutData(result.payload);
        return false;
    }
    if (!HasAnyLutData(result.payload)) {
        result.success = false;
        result.message = "The LUT file did not contain usable LUT samples.";
        result.payload.importError = result.message;
        ClearCanonicalLutData(result.payload);
        return false;
    }
    ApplyLutCreatorSidecarMetadata(path, result.payload);
    result.payload.importError.clear();
    return true;
}

bool ParseCube(std::istream& input, LutImportResult& result) {
    result.payload.importFormat = LutImportFormat::Cube;

    int size1D = 0;
    int size3D = 0;
    std::array<float, 3> domainMin { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> domainMax { 1.0f, 1.0f, 1.0f };
    std::vector<float> oneDValues;
    std::vector<float> threeDValues;

    std::string line;
    while (std::getline(input, line)) {
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.rfind("TITLE", 0) == 0) {
            std::string title = Trim(line.substr(5));
            if (!title.empty() && title.front() == '"' && title.back() == '"' && title.size() >= 2) {
                title = title.substr(1, title.size() - 2);
            }
            result.payload.importedTitle = title;
            continue;
        }
        if (line.rfind("DOMAIN_MIN", 0) == 0 || line.rfind("DOMAIN_MAX", 0) == 0) {
            std::vector<float> values;
            if (!ParseFloatTokens(line.substr(10), values) || values.size() != 3) {
                result.message = "Invalid DOMAIN_MIN / DOMAIN_MAX entry in .cube file.";
                return false;
            }
            if (line.rfind("DOMAIN_MIN", 0) == 0) {
                domainMin = { values[0], values[1], values[2] };
            } else {
                domainMax = { values[0], values[1], values[2] };
            }
            continue;
        }
        if (line.rfind("LUT_1D_SIZE", 0) == 0) {
            const std::string tail = Trim(line.substr(11));
            std::size_t ignored = 0;
            if (!ParseIntStrict(tail, size1D) ||
                size1D <= 1 ||
                !TryComputeLut1DValueCount(size1D, ignored)) {
                result.message = "Invalid LUT_1D_SIZE in .cube file.";
                return false;
            }
            continue;
        }
        if (line.rfind("LUT_3D_SIZE", 0) == 0) {
            const std::string tail = Trim(line.substr(11));
            std::size_t ignored = 0;
            if (!ParseIntStrict(tail, size3D) ||
                size3D <= 1 ||
                !TryComputeLut3DValueCount(size3D, ignored)) {
                result.message = "Invalid LUT_3D_SIZE in .cube file.";
                return false;
            }
            continue;
        }

        std::vector<float> sample;
        if (!ParseFloatTokens(line, sample) || sample.size() != 3) {
            result.message = "Invalid LUT sample row in .cube file.";
            return false;
        }
        std::size_t expected1DValues = 0;
        std::size_t expected3DValues = 0;
        const bool has1D =
            TryComputeLut1DValueCount(size1D, expected1DValues);
        const bool has3D =
            TryComputeLut3DValueCount(size3D, expected3DValues);
        if (has1D && oneDValues.size() < expected1DValues) {
            oneDValues.insert(oneDValues.end(), sample.begin(), sample.end());
        } else if (has3D) {
            if (threeDValues.size() > expected3DValues - sample.size()) {
                result.message =
                    "The .cube file contained more samples than its declared LUT sizes.";
                return false;
            }
            threeDValues.insert(threeDValues.end(), sample.begin(), sample.end());
        } else {
            result.message = "Encountered LUT samples before a declared LUT size in .cube file.";
            return false;
        }
    }

    if (size1D > 0 && size3D > 0) {
        result.payload.shaper1D.size = size1D;
        result.payload.shaper1D.values = std::move(oneDValues);
        result.payload.shaper1D.domainMin = domainMin;
        result.payload.shaper1D.domainMax = domainMax;
        result.payload.lut3D.size = size3D;
        result.payload.lut3D.values = std::move(threeDValues);
        if (!ValidateStage(result.payload.shaper1D) || !ValidateStage(result.payload.lut3D)) {
            result.message = "Combined .cube LUT sample counts did not match the declared sizes.";
            return false;
        }
        return true;
    }
    if (size3D > 0) {
        result.payload.lut3D.size = size3D;
        result.payload.lut3D.values = std::move(threeDValues);
        result.payload.lut3D.domainMin = domainMin;
        result.payload.lut3D.domainMax = domainMax;
        if (!ValidateStage(result.payload.lut3D)) {
            result.message = "The .cube 3D LUT sample count did not match the declared size.";
            return false;
        }
        return true;
    }
    if (size1D > 0) {
        result.payload.lut1D.size = size1D;
        result.payload.lut1D.values = std::move(oneDValues);
        result.payload.lut1D.domainMin = domainMin;
        result.payload.lut1D.domainMax = domainMax;
        if (!ValidateStage(result.payload.lut1D)) {
            result.message = "The .cube 1D LUT sample count did not match the declared size.";
            return false;
        }
        return true;
    }

    result.message = "The .cube file did not declare a 1D or 3D LUT size.";
    return false;
}

bool ParseSpi1d(std::istream& input, LutImportResult& result) {
    result.payload.importFormat = LutImportFormat::Spi1d;

    int length = 0;
    int components = 0;
    bool insideTable = false;
    Lut1DStage stage;
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        if (line == "{") {
            insideTable = true;
            continue;
        }
        if (line == "}") {
            insideTable = false;
            continue;
        }
        if (!insideTable) {
            if (line.rfind("Version", 0) == 0) {
                continue;
            }
            if (line.rfind("From", 0) == 0) {
                std::vector<float> values;
                if (!ParseFloatTokens(line.substr(4), values) || (values.size() != 2 && values.size() != 6)) {
                    result.message = "Invalid From domain in .spi1d file.";
                    return false;
                }
                SetUniformDomain(stage.domainMin, stage.domainMax, values);
                continue;
            }
            if (line.rfind("Length", 0) == 0) {
                std::size_t ignored = 0;
                if (!ParseIntStrict(Trim(line.substr(6)), length) ||
                    length <= 1 ||
                    !TryComputeLut1DValueCount(length, ignored)) {
                    result.message = "Invalid Length in .spi1d file.";
                    return false;
                }
                continue;
            }
            if (line.rfind("Components", 0) == 0) {
                if (!ParseIntStrict(Trim(line.substr(10)), components) || (components != 1 && components != 3)) {
                    result.message = "Only 1- and 3-component .spi1d files are supported.";
                    return false;
                }
                continue;
            }
            continue;
        }

        std::vector<float> values;
        if (!ParseFloatTokens(line, values)) {
            result.message = "Invalid .spi1d sample row.";
            return false;
        }
        if (components == 0) {
            components = static_cast<int>(values.size());
        }
        std::size_t expectedValues = 0;
        if (length > 0 &&
            TryComputeLut1DValueCount(length, expectedValues) &&
            stage.values.size() >= expectedValues) {
            result.message =
                "The .spi1d file contained more rows than its declared length.";
            return false;
        }
        if (components == 1) {
            if (values.size() != 1) {
                result.message = "Expected single-value rows in .spi1d table.";
                return false;
            }
            stage.values.push_back(values[0]);
            stage.values.push_back(values[0]);
            stage.values.push_back(values[0]);
        } else {
            if (values.size() != 3) {
                result.message = "Expected RGB rows in .spi1d table.";
                return false;
            }
            stage.values.insert(stage.values.end(), values.begin(), values.end());
        }
    }

    if (length <= 0) {
        length = static_cast<int>(stage.values.size() / 3u);
    }
    stage.size = length;
    result.payload.lut1D = std::move(stage);
    if (!ValidateStage(result.payload.lut1D)) {
        result.message = "The .spi1d table length did not match the sample data.";
        return false;
    }
    return true;
}

bool ParseSpi3d(std::istream& input, LutImportResult& result) {
    result.payload.importFormat = LutImportFormat::Spi3d;

    std::array<float, 3> domainMin { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> domainMax { 1.0f, 1.0f, 1.0f };
    int declaredSize = 0;
    std::vector<std::array<float, 6>> rows;

    std::string line;
    while (std::getline(input, line)) {
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.rfind("SPILUT", 0) == 0 || line.rfind("Version", 0) == 0) {
            continue;
        }
        if (line.rfind("From", 0) == 0) {
            std::vector<float> values;
            if (!ParseFloatTokens(line.substr(4), values) || (values.size() != 2 && values.size() != 6)) {
                result.message = "Invalid From domain in .spi3d file.";
                return false;
            }
            SetUniformDomain(domainMin, domainMax, values);
            continue;
        }

        std::vector<float> floatValues;
        if (!ParseFloatTokens(line, floatValues)) {
            result.message = "Invalid numeric row in .spi3d file.";
            return false;
        }
        if (floatValues.size() == 2) {
            continue;
        }
        if (floatValues.size() == 3 && declaredSize <= 0) {
            int sx = 0;
            int sy = 0;
            int sz = 0;
            if (TryParseGridIndex(floatValues[0], sx) &&
                TryParseGridIndex(floatValues[1], sy) &&
                TryParseGridIndex(floatValues[2], sz) &&
                sx > 1 && sx == sy && sx == sz) {
                declaredSize = sx;
                continue;
            }
        }
        if (floatValues.size() != 6) {
            result.message = "Expected indexed RGB rows in .spi3d file.";
            return false;
        }
        rows.push_back({ floatValues[0], floatValues[1], floatValues[2], floatValues[3], floatValues[4], floatValues[5] });
    }

    int maxIndex = -1;
    for (const auto& row : rows) {
        for (int axis = 0; axis < 3; ++axis) {
            int index = 0;
            if (!TryParseGridIndex(row[axis], index)) {
                result.message = "The .spi3d file used non-integer grid coordinates.";
                return false;
            }
            maxIndex = std::max(maxIndex, index);
        }
    }
    const int size = declaredSize > 0
        ? declaredSize
        : (maxIndex < std::numeric_limits<int>::max() ? maxIndex + 1 : 0);
    if (size <= 1) {
        result.message = "The .spi3d file did not declare a usable grid size.";
        return false;
    }

    std::size_t valueCount = 0;
    if (!TryComputeLut3DValueCount(size, valueCount)) {
        result.message =
            "The .spi3d grid dimensions exceed supported memory limits.";
        return false;
    }
    const std::size_t sampleCount = valueCount / 3u;
    if (rows.size() != sampleCount) {
        result.message =
            "The .spi3d sample count did not match its declared grid.";
        return false;
    }

    Lut3DStage stage;
    stage.size = size;
    stage.domainMin = domainMin;
    stage.domainMax = domainMax;
    stage.values.assign(valueCount, 0.0f);
    std::vector<unsigned char> populated(sampleCount, 0u);
    for (const auto& row : rows) {
        int r = 0;
        int g = 0;
        int b = 0;
        if (!TryParseGridIndex(row[0], r) ||
            !TryParseGridIndex(row[1], g) ||
            !TryParseGridIndex(row[2], b)) {
            result.message =
                "The .spi3d file used non-integer grid coordinates.";
            return false;
        }
        if (r >= size || g >= size || b >= size) {
            result.message = "The .spi3d file referenced a sample outside its declared grid.";
            return false;
        }
        const std::size_t offset =
            ((static_cast<std::size_t>(r) * static_cast<std::size_t>(size) + static_cast<std::size_t>(g)) * static_cast<std::size_t>(size) +
                static_cast<std::size_t>(b)) * 3u;
        const std::size_t sampleIndex = offset / 3u;
        if (populated[sampleIndex] != 0u) {
            result.message =
                "The .spi3d file contained duplicate grid coordinates.";
            return false;
        }
        populated[sampleIndex] = 1u;
        stage.values[offset + 0] = row[3];
        stage.values[offset + 1] = row[4];
        stage.values[offset + 2] = row[5];
    }

    result.payload.lut3D = std::move(stage);
    if (!ValidateStage(result.payload.lut3D)) {
        result.message = "The .spi3d data did not produce a valid RGB 3D LUT.";
        return false;
    }
    return true;
}

bool Parse3dl(std::istream& input, LutImportResult& result) {
    result.payload.importFormat = LutImportFormat::Format3dl;

    std::vector<float> gridLine;
    std::vector<std::array<float, 3>> samples;
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }
        std::vector<float> values;
        if (!ParseFloatTokens(line, values)) {
            result.message = "Invalid numeric row in .3dl file.";
            return false;
        }
        if (values.size() > 3 && gridLine.empty()) {
            gridLine = values;
            continue;
        }
        if (values.size() != 3) {
            result.message = "Expected RGB sample rows in .3dl file.";
            return false;
        }
        samples.push_back({ values[0], values[1], values[2] });
    }

    int size = gridLine.size() <=
            static_cast<std::size_t>(std::numeric_limits<int>::max())
        ? static_cast<int>(gridLine.size())
        : 0;
    if (size <= 1 && !samples.empty()) {
        const double cubeRoot = std::cbrt(static_cast<double>(samples.size()));
        const int rounded = static_cast<int>(std::round(cubeRoot));
        std::size_t inferredValueCount = 0;
        if (rounded > 1 &&
            TryComputeLut3DValueCount(rounded, inferredValueCount) &&
            inferredValueCount / 3u == samples.size()) {
            size = rounded;
        }
    }
    if (size <= 1) {
        result.message = "The .3dl file did not declare a usable LUT grid.";
        return false;
    }
    std::size_t valueCount = 0;
    if (!TryComputeLut3DValueCount(size, valueCount) ||
        samples.size() != valueCount / 3u) {
        result.message = "The .3dl sample count did not match the inferred LUT size.";
        return false;
    }

    float sampleMax = 0.0f;
    for (const auto& sample : samples) {
        sampleMax = std::max(sampleMax, std::max(sample[0], std::max(sample[1], sample[2])));
    }
    if (sampleMax <= 0.0f && !gridLine.empty()) {
        sampleMax = *std::max_element(gridLine.begin(), gridLine.end());
    }
    if (sampleMax <= 0.0f) {
        sampleMax = 1.0f;
    }

    Lut3DStage stage;
    stage.size = size;
    stage.values.assign(samples.size() * 3u, 0.0f);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        stage.values[index * 3u + 0] = samples[index][0] / sampleMax;
        stage.values[index * 3u + 1] = samples[index][1] / sampleMax;
        stage.values[index * 3u + 2] = samples[index][2] / sampleMax;
    }
    result.payload.lut3D = std::move(stage);
    if (!ValidateStage(result.payload.lut3D)) {
        result.message = "The .3dl data did not produce a valid RGB 3D LUT.";
        return false;
    }
    return true;
}

} // namespace

LutImportResult ImportLutFile(const std::string& path) {
    LutImportResult result;
    result.payload.label = FileName(path);

    std::ifstream input(path);
    if (!input.is_open()) {
        result.message = "Could not open the LUT file.";
        FinalizeResult(result, path);
        return result;
    }

    try {
        const std::string extension = FileExtension(path);
        if (extension == ".cube") {
            result.success = ParseCube(input, result);
        } else if (extension == ".spi1d") {
            result.success = ParseSpi1d(input, result);
        } else if (extension == ".spi3d") {
            result.success = ParseSpi3d(input, result);
        } else if (extension == ".3dl") {
            result.success = Parse3dl(input, result);
        } else {
            result.message = "Unsupported LUT format.";
        }
    } catch (const std::bad_alloc&) {
        result.success = false;
        result.message =
            "The LUT requires more memory than is safely available.";
    } catch (const std::length_error&) {
        result.success = false;
        result.message =
            "The LUT dimensions exceed supported memory limits.";
    } catch (const std::exception& error) {
        result.success = false;
        result.message =
            std::string("The LUT could not be parsed safely: ") + error.what();
    }

    FinalizeResult(result, path);
    return result;
}

} // namespace ColorLut
