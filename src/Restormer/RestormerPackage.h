#pragma once

#include "Raw/RawDevelopmentRecipe.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Stack::Restormer {

inline constexpr int kPackageManifestSchemaVersion = 1;
inline constexpr int kModelServiceProtocolVersion = 1;

enum class ModelKind {
    RealPhoto,
    GaussianBlind
};

struct Artifact {
    std::filesystem::path relativePath;
    std::string sha256;
};

struct ModelArtifact : Artifact {
    ModelKind kind = ModelKind::RealPhoto;
    std::string inputName = "input";
    std::string outputName = "output";
};

struct PackageManifest {
    int schemaVersion = 0;
    std::string packageId;
    std::string packageVersion;
    int protocolVersion = 0;
    std::string adapterVersion;
    bool developmentPackage = false;
    std::string authorizationStatus;
    Artifact service;
    std::vector<Artifact> runtimeArtifacts;
    std::vector<Artifact> legalArtifacts;
    std::vector<ModelArtifact> models;
};

struct TrustPolicy {
    bool allowDevelopmentPackage = false;
    std::vector<std::string> allowlistedManifestSha256;
};

struct ValidationRequest {
    RawRecipe::RawRgbDenoiseMethod method =
        RawRecipe::RawRgbDenoiseMethod::RestormerRealV1;
    std::string exactPackageVersion;
    std::string exactModelSha256;
    std::string exactAdapterVersion = RawRecipe::kRestormerDenoiseAdapterVersion;
};

struct ValidationResult {
    bool ok = false;
    std::string error;
    std::filesystem::path packageRoot;
    std::filesystem::path manifestPath;
    std::string manifestSha256;
    PackageManifest manifest;
    ModelArtifact selectedModel;
};

bool IsAiMethod(RawRecipe::RawRgbDenoiseMethod method);
ModelKind ModelKindForMethod(RawRecipe::RawRgbDenoiseMethod method);
const char* ModelKindStableString(ModelKind kind);
std::filesystem::path ResolvePackageRoot(
    const std::filesystem::path& executableDirectory);
TrustPolicy TrustPolicyFromEnvironment();
ValidationResult ValidatePackage(
    const std::filesystem::path& packageRoot,
    const ValidationRequest& request,
    const TrustPolicy& trustPolicy);

} // namespace Stack::Restormer
