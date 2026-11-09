#include "Raw/RawGallerySimilarity.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Stack::RawWorkspace {
namespace {

constexpr float kMaximumAspectRatioScale = 1.08f;
constexpr float kMinimumEdgeEnergy = 0.0125f;
constexpr float kPi = 3.14159265358979323846f;

template <std::size_t Size>
std::string EncodeBytes(const std::array<std::uint8_t, Size>& bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string encoded;
    encoded.resize(Size * 2u);
    for (std::size_t index = 0; index < Size; ++index) {
        encoded[index * 2u] = digits[(bytes[index] >> 4u) & 0x0fu];
        encoded[index * 2u + 1u] = digits[bytes[index] & 0x0fu];
    }
    return encoded;
}

int DecodeHexDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

template <std::size_t Size>
bool DecodeBytes(
    const std::string& encoded,
    std::array<std::uint8_t, Size>& bytes) {
    if (encoded.size() != Size * 2u) return false;
    for (std::size_t index = 0; index < Size; ++index) {
        const int high = DecodeHexDigit(encoded[index * 2u]);
        const int low = DecodeHexDigit(encoded[index * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

float SampleLuminance(
    const unsigned char* rgba,
    int width,
    int height,
    float x,
    float y) {
    const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, width - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, height - 1);
    const int x1 = std::min(width - 1, x0 + 1);
    const int y1 = std::min(height - 1, y0 + 1);
    const float tx = std::clamp(x - static_cast<float>(x0), 0.0f, 1.0f);
    const float ty = std::clamp(y - static_cast<float>(y0), 0.0f, 1.0f);
    const auto at = [&](int px, int py) {
        const std::size_t offset =
            (static_cast<std::size_t>(py) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(px)) * 4u;
        return (
            0.2126f * static_cast<float>(rgba[offset]) +
            0.7152f * static_cast<float>(rgba[offset + 1u]) +
            0.0722f * static_cast<float>(rgba[offset + 2u])) / 255.0f;
    };
    const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
    const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
    return top + (bottom - top) * ty;
}

float GradientCosineSimilarity(
    const RawGallerySimilarityDescriptor& left,
    const RawGallerySimilarityDescriptor& right) {
    double dot = 0.0;
    double leftLength = 0.0;
    double rightLength = 0.0;
    for (std::size_t index = 0;
         index < left.gradientOrientation.size();
         ++index) {
        const double a = static_cast<double>(left.gradientOrientation[index]);
        const double b = static_cast<double>(right.gradientOrientation[index]);
        dot += a * b;
        leftLength += a * a;
        rightLength += b * b;
    }
    if (leftLength <= 0.0 || rightLength <= 0.0) return 0.0f;
    return static_cast<float>(dot / std::sqrt(leftLength * rightLength));
}

float NormalizedLuminanceCorrelation(
    const RawGallerySimilarityDescriptor& left,
    const RawGallerySimilarityDescriptor& right) {
    double leftMean = 0.0;
    double rightMean = 0.0;
    for (std::size_t index = 0;
         index < left.normalizedLuminance.size();
         ++index) {
        leftMean += left.normalizedLuminance[index];
        rightMean += right.normalizedLuminance[index];
    }
    leftMean /= static_cast<double>(left.normalizedLuminance.size());
    rightMean /= static_cast<double>(right.normalizedLuminance.size());

    double dot = 0.0;
    double leftLength = 0.0;
    double rightLength = 0.0;
    for (std::size_t index = 0;
         index < left.normalizedLuminance.size();
         ++index) {
        const double a = left.normalizedLuminance[index] - leftMean;
        const double b = right.normalizedLuminance[index] - rightMean;
        dot += a * b;
        leftLength += a * a;
        rightLength += b * b;
    }
    if (leftLength <= 0.0 || rightLength <= 0.0) return 0.0f;
    return static_cast<float>(dot / std::sqrt(leftLength * rightLength));
}

bool AspectRatiosCompatible(float left, float right) {
    if (!(left > 0.0f) || !(right > 0.0f) ||
        !std::isfinite(left) || !std::isfinite(right)) {
        return false;
    }
    return std::max(left, right) / std::min(left, right) <=
        kMaximumAspectRatioScale;
}

int HashDistance(std::uint64_t left, std::uint64_t right) {
    std::uint64_t bits = left ^ right;
    int count = 0;
    while (bits != 0) {
        bits &= bits - 1u;
        ++count;
    }
    return count;
}



} // namespace

bool RawGallerySimilarityDescriptor::IsValid() const {
    return version == kRawGallerySimilarityDescriptorVersion &&
        aspectRatio > 0.0f && std::isfinite(aspectRatio) &&
        edgeEnergy >= 0.0f && std::isfinite(edgeEnergy);
}

RawGallerySimilarityDescriptor BuildRawGallerySimilarityDescriptor(
    const unsigned char* rgba,
    int width,
    int height) {
    RawGallerySimilarityDescriptor descriptor;
    if (rgba == nullptr || width <= 0 || height <= 0) return descriptor;

    std::array<float, kRawGallerySimilarityLuminanceCount> luminance {};
    for (int y = 0; y < kRawGallerySimilarityGridSize; ++y) {
        for (int x = 0; x < kRawGallerySimilarityGridSize; ++x) {
            const float sourceX =
                (static_cast<float>(x) + 0.5f) *
                    static_cast<float>(width) /
                    static_cast<float>(kRawGallerySimilarityGridSize) -
                0.5f;
            const float sourceY =
                (static_cast<float>(y) + 0.5f) *
                    static_cast<float>(height) /
                    static_cast<float>(kRawGallerySimilarityGridSize) -
                0.5f;
            luminance[static_cast<std::size_t>(y) *
                          kRawGallerySimilarityGridSize +
                      static_cast<std::size_t>(x)] =
                SampleLuminance(rgba, width, height, sourceX, sourceY);
        }
    }

    const std::array<float, kRawGallerySimilarityLuminanceCount>
        sourceLuminance = luminance;
    std::array<float, kRawGallerySimilarityLuminanceCount> sorted = luminance;
    std::sort(sorted.begin(), sorted.end());
    const float dark = sorted[sorted.size() * 5u / 100u];
    const float bright = sorted[sorted.size() * 95u / 100u];
    const float range = std::max(1.0f / 255.0f, bright - dark);
    for (std::size_t index = 0; index < luminance.size(); ++index) {
        luminance[index] = std::clamp((luminance[index] - dark) / range, 0.0f, 1.0f);
        descriptor.normalizedLuminance[index] = static_cast<std::uint8_t>(
            std::lround(luminance[index] * 255.0f));
    }

    std::array<float, kRawGallerySimilarityGradientCount> gradientBins {};
    double totalGradient = 0.0;
    for (int y = 1; y < kRawGallerySimilarityGridSize - 1; ++y) {
        for (int x = 1; x < kRawGallerySimilarityGridSize - 1; ++x) {
            const auto normalizedSample = [&](int sx, int sy) {
                return luminance[static_cast<std::size_t>(sy) *
                                     kRawGallerySimilarityGridSize +
                                 static_cast<std::size_t>(sx)];
            };
            const float gx =
                normalizedSample(x + 1, y) - normalizedSample(x - 1, y);
            const float gy =
                normalizedSample(x, y + 1) - normalizedSample(x, y - 1);
            const float magnitude = std::sqrt(gx * gx + gy * gy);
            const auto sourceSample = [&](int sx, int sy) {
                return sourceLuminance[static_cast<std::size_t>(sy) *
                                           kRawGallerySimilarityGridSize +
                                       static_cast<std::size_t>(sx)];
            };
            const float sourceGx =
                sourceSample(x + 1, y) - sourceSample(x - 1, y);
            const float sourceGy =
                sourceSample(x, y + 1) - sourceSample(x, y - 1);
            totalGradient += std::sqrt(
                sourceGx * sourceGx + sourceGy * sourceGy);
            float angle = std::atan2(gy, gx);
            if (angle < 0.0f) angle += kPi;
            if (angle >= kPi) angle -= kPi;
            const int bin = std::clamp(
                static_cast<int>(angle / kPi *
                    static_cast<float>(kRawGallerySimilarityGradientBinCount)),
                0,
                kRawGallerySimilarityGradientBinCount - 1);
            const int cellX = std::min(
                kRawGallerySimilarityGradientCellCount - 1,
                x * kRawGallerySimilarityGradientCellCount /
                    kRawGallerySimilarityGridSize);
            const int cellY = std::min(
                kRawGallerySimilarityGradientCellCount - 1,
                y * kRawGallerySimilarityGradientCellCount /
                    kRawGallerySimilarityGridSize);
            const std::size_t gradientIndex =
                (static_cast<std::size_t>(cellY) *
                     kRawGallerySimilarityGradientCellCount +
                 static_cast<std::size_t>(cellX)) *
                    kRawGallerySimilarityGradientBinCount +
                static_cast<std::size_t>(bin);
            gradientBins[gradientIndex] += magnitude;
        }
    }
    descriptor.edgeEnergy = static_cast<float>(
        totalGradient /
        static_cast<double>(
            (kRawGallerySimilarityGridSize - 2) *
            (kRawGallerySimilarityGridSize - 2)));
    descriptor.aspectRatio =
        static_cast<float>(width) / static_cast<float>(height);

    double gradientLength = 0.0;
    for (float value : gradientBins) {
        gradientLength += static_cast<double>(value) * value;
    }
    gradientLength = std::sqrt(gradientLength);
    if (gradientLength > 0.0) {
        for (std::size_t index = 0; index < gradientBins.size(); ++index) {
            descriptor.gradientOrientation[index] =
                static_cast<std::uint8_t>(std::clamp(
                    std::lround(gradientBins[index] / gradientLength * 255.0),
                    0l,
                    255l));
        }
    }

    // Keep exactly 64 low-frequency AC coefficients. The first 64 entries
    // form the conventional 8x8 block; the final entry supplies one more
    // horizontal coefficient so the brightness/DC term can be excluded
    // without reducing the hash to 63 effective bits.
    std::array<float, 65> dct {};
    // Immutable basis shared by descriptor requests. Evaluating the same
    // trigonometric terms for every thumbnail adds avoidable large-folder work.
    static const auto cosineBasis=[] {
        std::array<std::array<double,kRawGallerySimilarityGridSize>,9> basis{};
        for(int frequency=0;frequency<9;++frequency)
            for(int position=0;position<kRawGallerySimilarityGridSize;++position)
                basis[frequency][position]=std::cos((2.0*static_cast<double>(position)+1.0)*
                    static_cast<double>(frequency)*kPi/(2.0*kRawGallerySimilarityGridSize));
        return basis;
    }();
    const auto computeDct = [&](int u, int v) {
        double sum = 0.0;
        for (int y = 0; y < kRawGallerySimilarityGridSize; ++y) {
            for (int x = 0; x < kRawGallerySimilarityGridSize; ++x) {
                sum += static_cast<double>(luminance[
                    static_cast<std::size_t>(y) *
                        kRawGallerySimilarityGridSize +
                    static_cast<std::size_t>(x)]) *
                    cosineBasis[u][x] * cosineBasis[v][y];
            }
        }
        return static_cast<float>(sum);
    };
    for (int v = 0; v < 8; ++v) {
        for (int u = 0; u < 8; ++u) {
            dct[static_cast<std::size_t>(v) * 8u +
                static_cast<std::size_t>(u)] = computeDct(u, v);
        }
    }
    dct.back() = computeDct(8, 0);
    std::array<float, 64> nonDc {};
    std::copy(dct.begin() + 1, dct.end(), nonDc.begin());
    std::nth_element(
        nonDc.begin(),
        nonDc.begin() + static_cast<std::ptrdiff_t>(nonDc.size() / 2u),
        nonDc.end());
    const float median = nonDc[nonDc.size() / 2u];
    descriptor.perceptualHash = 0;
    for (std::size_t index = 1; index < dct.size(); ++index) {
        if (dct[index] > median) {
            descriptor.perceptualHash |=
                std::uint64_t { 1 } << (index - 1u);
        }
    }
    descriptor.version = kRawGallerySimilarityDescriptorVersion;
    return descriptor;
}

nlohmann::json SerializeRawGallerySimilarityDescriptor(
    const RawGallerySimilarityDescriptor& descriptor) {
    if (!descriptor.IsValid()) return nlohmann::json();
    nlohmann::json value = nlohmann::json::object();
    value["version"] = descriptor.version;
    value["perceptualHash"] = std::to_string(descriptor.perceptualHash);
    value["normalizedLuminance"] = EncodeBytes(
        descriptor.normalizedLuminance);
    value["gradientOrientation"] = EncodeBytes(
        descriptor.gradientOrientation);
    value["edgeEnergy"] = descriptor.edgeEnergy;
    value["aspectRatio"] = descriptor.aspectRatio;
    return value;
}

bool DeserializeRawGallerySimilarityDescriptor(
    const nlohmann::json& value,
    RawGallerySimilarityDescriptor& descriptor) {
    descriptor = {};
    if (!value.is_object()) return false;
    RawGallerySimilarityDescriptor parsed;
    try {
        parsed.version = value.value("version", 0);
        const std::string hash = value.value("perceptualHash", std::string());
        if (hash.empty()) return false;
        parsed.perceptualHash = std::stoull(hash);
        parsed.edgeEnergy = value.value("edgeEnergy", 0.0f);
        parsed.aspectRatio = value.value("aspectRatio", 0.0f);
        if (!DecodeBytes(
                value.value("normalizedLuminance", std::string()),
                parsed.normalizedLuminance) ||
            !DecodeBytes(
                value.value("gradientOrientation", std::string()),
                parsed.gradientOrientation) ||
            !parsed.IsValid()) {
            return false;
        }
    } catch (...) {
        return false;
    }
    descriptor = std::move(parsed);
    return true;
}

nlohmann::json SerializeRawGalleryManualGrouping(
    const RawGalleryManualGrouping& grouping) {
    nlohmann::json value = nlohmann::json::object();
    value["version"] = kRawGalleryManualGroupingVersion;
    value["stacks"] = grouping.stacks;
    value["detached"] = grouping.detachedSourceKeys;
    value["sortMode"] = static_cast<int>(grouping.sortMode);
    value["manualSourceOrder"] = grouping.manualSourceOrder;
    return value;
}

bool DeserializeRawGalleryManualGrouping(
    const nlohmann::json& value,
    RawGalleryManualGrouping& grouping) {
    grouping = {};
    if (!value.is_object()) {
        return false;
    }
    const int encodedVersion = value.value("version", 0);
    if (encodedVersion != 1 &&
        encodedVersion != kRawGalleryManualGroupingVersion) return false;
    try {
        if (value.contains("stacks") && value["stacks"].is_array()) {
            for (const auto& encodedStack : value["stacks"]) {
                if (!encodedStack.is_array()) continue;
                std::vector<std::string> stack;
                std::unordered_set<std::string> seen;
                for (const auto& encodedKey : encodedStack) {
                    if (!encodedKey.is_string()) continue;
                    std::string key = encodedKey.get<std::string>();
                    if (!key.empty() && seen.insert(key).second) {
                        stack.push_back(std::move(key));
                    }
                }
                if (stack.size() > 1u) {
                    grouping.stacks.push_back(std::move(stack));
                }
            }
        }
        if (value.contains("detached") && value["detached"].is_array()) {
            std::unordered_set<std::string> seen;
            for (const auto& encodedKey : value["detached"]) {
                if (!encodedKey.is_string()) continue;
                std::string key = encodedKey.get<std::string>();
                if (!key.empty() && seen.insert(key).second) {
                    grouping.detachedSourceKeys.push_back(std::move(key));
                }
            }
        }
        if (encodedVersion >= 2) {
            const int sortMode = value.value(
                "sortMode",
                static_cast<int>(
                    RawGalleryFilmstripSortMode::TimelineAll));
            if (sortMode < static_cast<int>(
                    RawGalleryFilmstripSortMode::Manual) ||
                sortMode > static_cast<int>(
                    RawGalleryFilmstripSortMode::TimelineByFolderOldestFirst)) {
                return false;
            }
            grouping.sortMode =
                static_cast<RawGalleryFilmstripSortMode>(sortMode);
            if (value.contains("manualSourceOrder") &&
                value["manualSourceOrder"].is_array()) {
                std::unordered_set<std::string> seen;
                for (const auto& encodedKey : value["manualSourceOrder"]) {
                    if (!encodedKey.is_string()) continue;
                    std::string key = encodedKey.get<std::string>();
                    if (!key.empty() && seen.insert(key).second) {
                        grouping.manualSourceOrder.push_back(std::move(key));
                    }
                }
            }
        }
        grouping.version = kRawGalleryManualGroupingVersion;
    } catch (...) {
        grouping = {};
        return false;
    }
    return true;
}

namespace {

void PreserveManualRemainder(
    RawGalleryManualGrouping& grouping,
    std::vector<std::string> remainder) {
    if (remainder.size() > 1u) {
        grouping.stacks.push_back(std::move(remainder));
    } else if (remainder.size() == 1u) {
        grouping.detachedSourceKeys.push_back(std::move(remainder.front()));
    }
}

void RemoveManualMembers(
    RawGalleryManualGrouping& grouping,
    const std::unordered_set<std::string>& removed) {
    std::vector<std::vector<std::string>> previousStacks =
        std::move(grouping.stacks);
    grouping.stacks.clear();
    for (auto& stack : previousStacks) {
        stack.erase(
            std::remove_if(
                stack.begin(),
                stack.end(),
                [&](const std::string& key) {
                    return removed.find(key) != removed.end();
                }),
            stack.end());
        PreserveManualRemainder(grouping, std::move(stack));
    }
    grouping.detachedSourceKeys.erase(
        std::remove_if(
            grouping.detachedSourceKeys.begin(),
            grouping.detachedSourceKeys.end(),
            [&](const std::string& key) {
                return removed.find(key) != removed.end();
            }),
        grouping.detachedSourceKeys.end());
}

} // namespace

bool MergeRawGalleryManualStack(
    RawGalleryManualGrouping& grouping,
    const std::string& sourceKey,
    const std::vector<std::string>& targetStackSourceKeys) {
    if (sourceKey.empty() || targetStackSourceKeys.empty() ||
        std::find(
            targetStackSourceKeys.begin(),
            targetStackSourceKeys.end(),
            sourceKey) != targetStackSourceKeys.end()) {
        return false;
    }
    std::vector<std::string> merged;
    merged.reserve(targetStackSourceKeys.size() + 1u);
    std::unordered_set<std::string> affected;
    for (const std::string& key : targetStackSourceKeys) {
        if (!key.empty() && affected.insert(key).second) {
            merged.push_back(key);
        }
    }
    if (merged.empty()) return false;
    affected.insert(sourceKey);
    RemoveManualMembers(grouping, affected);
    merged.push_back(sourceKey);
    grouping.stacks.push_back(std::move(merged));
    grouping.version = kRawGalleryManualGroupingVersion;
    return true;
}

bool DetachRawGalleryManualStackMember(
    RawGalleryManualGrouping& grouping,
    const std::string& sourceKey) {
    if (sourceKey.empty()) return false;
    const bool alreadyDetached = std::find(
        grouping.detachedSourceKeys.begin(),
        grouping.detachedSourceKeys.end(),
        sourceKey) != grouping.detachedSourceKeys.end();
    bool belongedToManualStack = false;
    for (const auto& stack : grouping.stacks) {
        belongedToManualStack = belongedToManualStack ||
            std::find(stack.begin(), stack.end(), sourceKey) != stack.end();
    }
    if (alreadyDetached && !belongedToManualStack) return false;
    RemoveManualMembers(grouping, { sourceKey });
    grouping.detachedSourceKeys.push_back(sourceKey);
    grouping.version = kRawGalleryManualGroupingVersion;
    return true;
}

std::vector<RawGallerySimilarityStack> ApplyRawGalleryManualGrouping(
    const std::vector<RawGallerySimilarityStack>& automaticStacks,
    const std::vector<RawGallerySimilarityInput>& catalog,
    const RawGalleryManualGrouping& grouping) {
    std::unordered_map<std::string, const RawGallerySimilarityInput*> inputs;
    inputs.reserve(catalog.size());
    for (const auto& input : catalog) {
        if (!input.sourceKey.empty()) inputs.emplace(input.sourceKey, &input);
    }

    std::vector<RawGallerySimilarityStack> resolved;
    resolved.reserve(automaticStacks.size() + grouping.stacks.size() +
                     grouping.detachedSourceKeys.size());
    std::unordered_set<std::string> assigned;
    assigned.reserve(catalog.size());
    const auto appendStack = [&](const std::vector<std::string>& sourceKeys,
                                 std::vector<RawGallerySimilarityStack>& output,
                                 std::unordered_set<std::string>& used) {
        RawGallerySimilarityStack stack;
        for (const std::string& sourceKey : sourceKeys) {
            const auto found = inputs.find(sourceKey);
            if (found == inputs.end() || !used.insert(sourceKey).second) continue;
            if (stack.sourceKeys.empty()) {
                stack.folderKey = found->second->folderKey;
                stack.anchorCatalogIndex = found->second->catalogIndex;
            }
            stack.sourceKeys.push_back(sourceKey);
        }
        if (!stack.sourceKeys.empty()) output.push_back(std::move(stack));
    };

    for (const auto& manualStack : grouping.stacks) {
        appendStack(manualStack, resolved, assigned);
    }
    for (const std::string& detached : grouping.detachedSourceKeys) {
        appendStack({ detached }, resolved, assigned);
    }
    for (const auto& automaticStack : automaticStacks) {
        appendStack(automaticStack.sourceKeys, resolved, assigned);
    }
    for (const auto& input : catalog) {
        appendStack({ input.sourceKey }, resolved, assigned);
    }
    std::stable_sort(
        resolved.begin(),
        resolved.end(),
        [](const auto& left, const auto& right) {
            return left.anchorCatalogIndex < right.anchorCatalogIndex;
        });
    return resolved;
}

std::vector<RawGallerySimilarityStack> BuildRawGallerySimilarityStacks(
    const std::vector<RawGallerySimilarityInput>& inputs) {
    return BuildRawGallerySimilarityStacks(inputs, {});
}

namespace {
std::vector<RawGallerySimilarityStack> CaptureStacks(const std::vector<RawGallerySimilarityInput>& inputs,
    const std::function<bool()>& cancelled, bool brackets) {
    struct Prepared { const RawGallerySimilarityInput* source; std::string partition; };
    std::vector<Prepared> ordered;ordered.reserve(inputs.size());
    for(const auto& input:inputs) {
        const auto& c=input.capture;
        ordered.push_back({&input,input.folderKey+"\n"+c.make+"\n"+c.model+"\n"+
            std::to_string(c.width)+"x"+std::to_string(c.height)});
    }
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) {
        if(a.partition!=b.partition)return a.partition<b.partition;
        if(a.source->captureTimestamp!=b.source->captureTimestamp)return a.source->captureTimestamp<b.source->captureTimestamp;
        return a.source->sourceKey<b.source->sourceKey;
    });
    struct Group {RawGallerySimilarityStack stack;std::vector<const RawGallerySimilarityInput*> members;double shutterSeconds=0;};
    std::vector<Group> groups;
    std::vector<std::size_t> recent;
    std::string partition;
    const auto exposureCompatible=[](const GalleryCaptureMetadata& a,const GalleryCaptureMetadata& b) {
        return a.ExposureKnown()&&b.ExposureKnown()&&std::abs(std::log2(a.aperture/b.aperture))<=.10 &&
            std::abs(std::log2(a.iso/b.iso))<=.10;
    };
    const auto visual=[&](const auto& a,const auto& b,bool exposureSequence) {
        const auto& x=a.descriptor;const auto& y=b.descriptor;
        if(!x.IsValid()||!y.IsValid()||x.edgeEnergy<kMinimumEdgeEnergy||y.edgeEnergy<kMinimumEdgeEnergy||
            !AspectRatiosCompatible(x.aspectRatio,y.aspectRatio))return -1.f;
        const float gradient=GradientCosineSimilarity(x,y),light=NormalizedLuminanceCorrelation(x,y);
        const int hash=HashDistance(x.perceptualHash,y.perceptualHash);
        const bool ordinary=gradient>=.82f&&light>=.55f&&hash<=24;
        const bool exposure=exposureSequence&&gradient>=(brackets?.75f:.65f)&&light>=(brackets?.35f:.20f)&&hash<=32;
        return ordinary||exposure?gradient+.2f*light:-1.f;
    };
    for(const auto& item:ordered) {
        if(cancelled&&cancelled())return {};
        const auto& input=*item.source;
        if(partition!=item.partition){partition=item.partition;recent.clear();}
        std::size_t chosen=groups.size();float best=-1;
        const bool known=input.captureTimestamp>0&&input.capture.CameraKnown();
        if(known)for(auto it=recent.rbegin();it!=recent.rend();++it) {
            auto& group=groups[*it];const auto& first=*group.members.front();const auto& last=*group.members.back();
            const auto span=input.captureTimestamp-first.captureTimestamp;
            const auto& c=input.capture;const auto& anchor=first.capture;
            const bool exposure=exposureCompatible(anchor,c);
            // Start-to-start time includes the previous shutter duration. Long
            // exposures need that allowance, but every sequence has a hard cap.
            const double gapLimit=exposure?std::max(brackets?5.:10.,std::min(65.,last.capture.shutter+3.)):(brackets?5.:10.);
            const double spanLimit=exposure?std::max(brackets?12.:30.,std::min(180.,group.shutterSeconds+8.)):(brackets?12.:30.);
            if(span>spanLimit||input.captureTimestamp-last.captureTimestamp>gapLimit)continue;
            if(brackets&&group.members.size()>=9)continue;
            if(first.folderKey!=input.folderKey||anchor.make!=c.make||anchor.model!=c.model)continue;
            if(!anchor.lens.empty()&&!c.lens.empty()&&anchor.lens!=c.lens)continue;
            if(anchor.focalLength>0&&c.focalLength>0&&
                std::max(anchor.focalLength,c.focalLength)/std::min(anchor.focalLength,c.focalLength)>1.05)continue;
            if(brackets&&!exposure)continue;
            // Keep repeated bracket cycles separate, even if they share a scene.
            if(brackets&&group.members.size()>=2) {
                bool repeated=false;
                for(const auto* member:group.members)if(std::abs(member->capture.ExposureStops()-c.ExposureStops())<.20)repeated=true;
                if(repeated)continue;
            }
            // A fixed first reference bounds scene drift; midpoint and latest
            // references tolerate exposure changes without an all-pairs search.
            const auto exposureChange=[&](const auto& member) {return exposureCompatible(member.capture,c)&&
                std::abs(member.capture.ExposureStops()-c.ExposureStops())>=.3;};
            const float anchorScore=visual(first,input,exposureChange(first));
            if(anchorScore<0)continue;
            float score=anchorScore;
            if(group.members.size()>1)score=std::max(score,visual(last,input,exposureChange(last)));
            if(group.members.size()>2) {
                const auto& middle=*group.members[group.members.size()/2];
                score=std::max(score,visual(middle,input,exposureChange(middle)));
            }
            if(score>best){best=score;chosen=*it;}
        }
        if(chosen==groups.size()) {
            Group group;group.stack.folderKey=input.folderKey;group.stack.anchorCatalogIndex=input.catalogIndex;
            groups.push_back(std::move(group));
            if(known) {
                recent.push_back(chosen);
                if(recent.size()>64)recent.erase(recent.begin());
            }
        }
        groups[chosen].members.push_back(&input);groups[chosen].stack.sourceKeys.push_back(input.sourceKey);
        if(input.capture.ExposureKnown())groups[chosen].shutterSeconds+=std::min(180.,input.capture.shutter);
    }
    std::vector<RawGallerySimilarityStack> result;result.reserve(groups.size());
    for(auto& group:groups) {
        if(brackets) {
            if(group.members.size()<2)continue;
            double low=group.members.front()->capture.ExposureStops(),high=low;
            for(const auto* member:group.members) {const double ev=member->capture.ExposureStops();low=std::min(low,ev);high=std::max(high,ev);}
            if(high-low<.7)continue;
        }
        result.push_back(std::move(group.stack));
    }
    return result;
}
}
std::vector<RawGallerySimilarityStack> BuildRawGallerySimilarityStacks(
    const std::vector<RawGallerySimilarityInput>& inputs,const std::function<bool()>& shouldCancel) {
    return CaptureStacks(inputs,shouldCancel,false);
}

RawGallerySimilarityResult BuildRawGallerySimilarityResult(
    const RawGallerySimilarityRequest& request, const std::function<bool()>& shouldCancel) {
    RawGallerySimilarityResult result;
    result.generation = request.generation;
    result.workspaceKey = request.workspaceKey;
    result.stacks = BuildRawGallerySimilarityStacks(request.inputs,shouldCancel);
    result.bracketCandidates = CaptureStacks(request.inputs,shouldCancel,true);
    return result;
}

bool IsRawGallerySimilarityResultCurrent(
    const RawGallerySimilarityResult& result,
    std::uint64_t currentGeneration,
    const std::string& currentWorkspaceKey) {
    return result.generation == currentGeneration &&
        result.workspaceKey == currentWorkspaceKey;
}

std::string ResolveRawGalleryStackCover(
    const std::vector<std::string>& sourceKeys,
    const std::string& selectedSourceKey) {
    const auto selected = std::find(
        sourceKeys.begin(), sourceKeys.end(), selectedSourceKey);
    if (selected != sourceKeys.end()) return *selected;
    return sourceKeys.empty() ? std::string {} : sourceKeys.front();
}

std::vector<std::string> BuildRawGalleryExpansionOrder(
    const std::vector<std::string>& sourceKeys,
    const std::string& coverSourceKey) {
    std::vector<std::string> ordered;
    ordered.reserve(sourceKeys.size());
    const auto cover = std::find(
        sourceKeys.begin(), sourceKeys.end(), coverSourceKey);
    if (cover != sourceKeys.end()) ordered.push_back(*cover);
    for (const std::string& sourceKey : sourceKeys) {
        if (cover == sourceKeys.end() || sourceKey != *cover) {
            ordered.push_back(sourceKey);
        }
    }
    return ordered;
}

RawGalleryFilmstripDrawerState UpdateRawGalleryFilmstripDrawerState(
    const RawGalleryFilmstripDrawerState& current,
    const RawGalleryFilmstripDrawerInput& input) {
    constexpr double kHoverDelaySeconds = 0.40;
    constexpr double kLeaveGraceSeconds = 0.10;

    RawGalleryFilmstripDrawerState next = current;
    if (!input.enabled) return {};

    if (input.galleryWorkspace) {
        // Gallery expansion is deliberate. Hovering a collapsed stack must
        // not open it (including singleton entries).
        next.keyboardPinned = false;
        next.leaveStartedAt = -1.0;
        next.suppressHoverUntilPointerExit = false;
        if (input.keyboardToggle) {
            next.open = !next.open;
            next.hoverStartedAt = -1.0;
        } else if (!next.open || input.pointerInside ||
            input.interactionRetained || !input.previewHovered) {
            next.hoverStartedAt = -1.0;
        } else {
            if (next.hoverStartedAt < 0.0) next.hoverStartedAt = input.now;
            if (input.now - next.hoverStartedAt + 1.0e-9 >=
                kHoverDelaySeconds) next.open = false;
        }
        return next;
    }

    if (input.keyboardToggle) {
        if (next.open) {
            next.open = false;
            next.keyboardPinned = false;
            next.hoverStartedAt = -1.0;
            next.leaveStartedAt = -1.0;
            next.suppressHoverUntilPointerExit = input.pointerInside;
        } else {
            next.open = true;
            next.keyboardPinned = true;
            next.hoverStartedAt = -1.0;
            next.leaveStartedAt = -1.0;
            next.suppressHoverUntilPointerExit = false;
        }
        return next;
    }

    if (next.suppressHoverUntilPointerExit) {
        if (input.pointerInside) return next;
        next.suppressHoverUntilPointerExit = false;
        next.hoverStartedAt = -1.0;
    }

    if (next.keyboardPinned) {
        next.open = true;
        next.leaveStartedAt = -1.0;
        return next;
    }

    const bool retained = input.pointerInside || input.interactionRetained;
    if (next.open) {
        if (retained) {
            next.leaveStartedAt = -1.0;
        } else {
            if (next.leaveStartedAt < 0.0) {
                next.leaveStartedAt = input.now;
            }
            if (input.now - next.leaveStartedAt + 1.0e-9 >=
                kLeaveGraceSeconds) {
                next.open = false;
                next.hoverStartedAt = -1.0;
                next.leaveStartedAt = -1.0;
            }
        }
        return next;
    }

    next.leaveStartedAt = -1.0;
    if (!input.pointerInside) {
        next.hoverStartedAt = -1.0;
        return next;
    }
    if (next.hoverStartedAt < 0.0) {
        next.hoverStartedAt = input.now;
    }
    if (input.now - next.hoverStartedAt + 1.0e-9 >=
        kHoverDelaySeconds) {
        next.open = true;
        next.hoverStartedAt = -1.0;
    }
    return next;
}

float ComputeRawGalleryFilmstripDrawerTargetHeight(
    float collapsedHeight,
    float tileHeight,
    float naturalGap,
    std::size_t maximumStackSize,
    float availableWorkspaceHeight) {
    const float safeCollapsed = std::max(0.0f, collapsedHeight);
    const float interval = std::max(0.0f, tileHeight) +
        std::max(0.0f, naturalGap);
    const std::size_t riseCount = maximumStackSize > 1u
        ? maximumStackSize - 1u
        : 0u;
    const float requested = safeCollapsed +
        interval * static_cast<float>(riseCount);
    const float cap = std::max(
        safeCollapsed,
        std::max(0.0f, availableWorkspaceHeight) * 0.5f);
    return std::clamp(requested, safeCollapsed, cap);
}

RawGalleryFilmstripStackLayout ComputeRawGalleryFilmstripStackLayout(
    std::size_t cardCount,
    float cardHeight,
    float naturalGap,
    float baseCardTop,
    float topBoundary,
    float expansionProgress) {
    RawGalleryFilmstripStackLayout layout;
    const float safeHeight = std::max(0.0f, cardHeight);
    layout.bottom = baseCardTop + safeHeight;
    layout.top = baseCardTop;
    if (cardCount <= 1u) return layout;

    const float naturalStep = safeHeight + std::max(0.0f, naturalGap);
    const float maximumRise = std::max(0.0f, baseCardTop - topBoundary);
    const float expandedCardStep = std::min(
        naturalStep,
        maximumRise / static_cast<float>(cardCount - 1u));
    layout.cardStep = expandedCardStep *
        std::clamp(expansionProgress, 0.0f, 1.0f);
    layout.top = baseCardTop -
        layout.cardStep * static_cast<float>(cardCount - 1u);
    return layout;
}

bool IsRawGalleryFilmstripExpansionOrderCurrent(
    const std::vector<std::string>& sourceKeys,
    const std::vector<std::string>& expansionOrder) {
    if (sourceKeys.size() != expansionOrder.size()) return false;

    std::unordered_set<std::string> expected(
        sourceKeys.begin(), sourceKeys.end());
    if (expected.size() != sourceKeys.size()) return false;
    for (const std::string& sourceKey : expansionOrder) {
        if (!expected.erase(sourceKey)) return false;
    }
    return expected.empty();
}

bool ShouldRestoreRawGalleryForEmptyWorkspace(
    bool workspaceAvailable,
    bool workspaceLocked,
    bool rawProjectActive,
    bool multiFrameProjectActive,
    bool explicitOpenPending,
    bool galleryClosed) {
    return workspaceAvailable &&
        !workspaceLocked &&
        !rawProjectActive &&
        !multiFrameProjectActive &&
        !explicitOpenPending &&
        galleryClosed;
}

} // namespace Stack::RawWorkspace
