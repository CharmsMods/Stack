#include "Restormer/RestormerPackage.h"

#include "Raw/RawTechnicalEvidence.h"
#include "ThirdParty/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace Stack::Restormer {
namespace {

using json = nlohmann::json;

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool LooksLikeSha256(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

bool PathStaysInside(
    const std::filesystem::path& root,
    const std::filesystem::path& relative,
    std::filesystem::path& resolved) {
    if (relative.empty() || relative.is_absolute()) {
        return false;
    }
    std::error_code ec;
    const std::filesystem::path canonicalRoot =
        std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        return false;
    }
    resolved = std::filesystem::weakly_canonical(root / relative, ec);
    if (ec) {
        return false;
    }
    auto rootIterator = canonicalRoot.begin();
    auto resolvedIterator = resolved.begin();
    for (; rootIterator != canonicalRoot.end(); ++rootIterator, ++resolvedIterator) {
        if (resolvedIterator == resolved.end() ||
            LowerAscii(rootIterator->string()) !=
                LowerAscii(resolvedIterator->string())) {
            return false;
        }
    }
    return true;
}

bool ParseArtifact(const json& value, Artifact& artifact) {
    if (!value.is_object()) {
        return false;
    }
    artifact.relativePath = value.value("path", std::string());
    artifact.sha256 = LowerAscii(value.value("sha256", std::string()));
    return !artifact.relativePath.empty() && LooksLikeSha256(artifact.sha256);
}

bool ParseModel(const json& value, ModelArtifact& model) {
    Artifact artifact;
    if (!ParseArtifact(value, artifact)) {
        return false;
    }
    const std::string kind = value.value("kind", std::string());
    if (kind == "real-photo") {
        model.kind = ModelKind::RealPhoto;
    } else if (kind == "gaussian-blind") {
        model.kind = ModelKind::GaussianBlind;
    } else {
        return false;
    }
    model.relativePath = std::move(artifact.relativePath);
    model.sha256 = std::move(artifact.sha256);
    model.inputName = value.value("inputName", std::string("input"));
    model.outputName = value.value("outputName", std::string("output"));
    return !model.inputName.empty() && !model.outputName.empty();
}

bool VerifyArtifact(
    const std::filesystem::path& packageRoot,
    const Artifact& artifact,
    std::string& error) {
    std::filesystem::path resolved;
    if (!PathStaysInside(packageRoot, artifact.relativePath, resolved)) {
        error = "Package artifact path escapes the approved package directory: " +
            artifact.relativePath.generic_string();
        return false;
    }
    const RawEvidence::SourceIdentity identity =
        RawEvidence::ComputeSourceIdentity(resolved);
    if (!identity.valid) {
        error = "Package artifact is missing or unreadable: " +
            artifact.relativePath.generic_string();
        return false;
    }
    if (LowerAscii(identity.sha256) != LowerAscii(artifact.sha256)) {
        error = "Package artifact hash does not match the manifest: " +
            artifact.relativePath.generic_string();
        return false;
    }
    return true;
}

bool ParseArtifactArray(
    const json& root,
    const char* key,
    std::vector<Artifact>& artifacts) {
    const json values = root.value(key, json::array());
    if (!values.is_array()) {
        return false;
    }
    for (const json& value : values) {
        Artifact artifact;
        if (!ParseArtifact(value, artifact)) {
            return false;
        }
        artifacts.push_back(std::move(artifact));
    }
    return true;
}

} // namespace

bool IsAiMethod(RawRecipe::RawRgbDenoiseMethod method) {
    return method == RawRecipe::RawRgbDenoiseMethod::RestormerRealV1 ||
        method == RawRecipe::RawRgbDenoiseMethod::RestormerGaussianBlindV1;
}

ModelKind ModelKindForMethod(RawRecipe::RawRgbDenoiseMethod method) {
    return method == RawRecipe::RawRgbDenoiseMethod::RestormerGaussianBlindV1
        ? ModelKind::GaussianBlind
        : ModelKind::RealPhoto;
}

const char* ModelKindStableString(ModelKind kind) {
    switch (kind) {
        case ModelKind::RealPhoto: return "real-photo";
        case ModelKind::GaussianBlind: return "gaussian-blind";
    }
    return "real-photo";
}

std::filesystem::path ResolvePackageRoot(
    const std::filesystem::path& executableDirectory) {
    if (const char* explicitRoot = std::getenv("STACK_RESTORMER_DENOISE_DIR")) {
        if (*explicitRoot != '\0') {
            return std::filesystem::path(explicitRoot);
        }
    }
    return executableDirectory /
        "packages" /
        RawRecipe::kRestormerDenoisePackageId;
}

TrustPolicy TrustPolicyFromEnvironment() {
    TrustPolicy policy;
    if (const char* allowDevelopment =
            std::getenv("STACK_ALLOW_LOCAL_RESTORMER_PACKAGE")) {
        policy.allowDevelopmentPackage =
            std::string(allowDevelopment) == "1";
    }
    // Public release manifest digests are intentionally compiled here only
    // after legal and artifact review. The V1 list remains empty while
    // checkpoint redistribution authorization is unresolved.
    return policy;
}

ValidationResult ValidatePackage(
    const std::filesystem::path& packageRoot,
    const ValidationRequest& request,
    const TrustPolicy& trustPolicy) {
    ValidationResult result;
    result.packageRoot = packageRoot;
    result.manifestPath = packageRoot / "manifest.json";

    std::ifstream input(result.manifestPath, std::ios::binary);
    if (!input.is_open()) {
        result.error =
            "Install stack-restormer-denoise-v1 or select Classical Multiscale.";
        return result;
    }
    const json root = json::parse(input, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        result.error = "The Restormer package manifest is invalid JSON.";
        return result;
    }

    result.manifest.schemaVersion = root.value("schemaVersion", 0);
    result.manifest.packageId = root.value("packageId", std::string());
    result.manifest.packageVersion = root.value("packageVersion", std::string());
    result.manifest.protocolVersion = root.value("protocolVersion", 0);
    result.manifest.adapterVersion = root.value("adapterVersion", std::string());
    result.manifest.developmentPackage =
        root.value("developmentPackage", false);
    result.manifest.authorizationStatus =
        root.value("authorizationStatus", std::string());
    if (!ParseArtifact(root.value("service", json::object()), result.manifest.service) ||
        !ParseArtifactArray(root, "runtimeArtifacts", result.manifest.runtimeArtifacts) ||
        !ParseArtifactArray(root, "legalArtifacts", result.manifest.legalArtifacts)) {
        result.error = "The Restormer package artifact ledger is incomplete.";
        return result;
    }
    const json models = root.value("models", json::array());
    if (!models.is_array()) {
        result.error = "The Restormer package model ledger is invalid.";
        return result;
    }
    for (const json& value : models) {
        ModelArtifact model;
        if (!ParseModel(value, model)) {
            result.error = "The Restormer package contains an invalid model record.";
            return result;
        }
        result.manifest.models.push_back(std::move(model));
    }
    const auto modelCount = [&](ModelKind kind) {
        return std::count_if(
            result.manifest.models.begin(),
            result.manifest.models.end(),
            [kind](const ModelArtifact& model) {
                return model.kind == kind;
            });
    };
    if (modelCount(ModelKind::RealPhoto) != 1 ||
        modelCount(ModelKind::GaussianBlind) != 1) {
        result.error =
            "The Restormer package must contain exactly one Real Photo model "
            "and one Gaussian Blind model.";
        return result;
    }
    if (std::any_of(
            result.manifest.models.begin(),
            result.manifest.models.end(),
            [](const ModelArtifact& model) {
                return model.inputName != "input" ||
                    model.outputName != "output";
            })) {
        result.error =
            "The Restormer package uses an unsupported ONNX input/output contract.";
        return result;
    }

    const RawEvidence::SourceIdentity manifestIdentity =
        RawEvidence::ComputeSourceIdentity(result.manifestPath);
    if (!manifestIdentity.valid) {
        result.error = "The Restormer package manifest could not be hashed.";
        return result;
    }
    result.manifestSha256 = LowerAscii(manifestIdentity.sha256);

    if (result.manifest.schemaVersion != kPackageManifestSchemaVersion ||
        result.manifest.packageId != RawRecipe::kRestormerDenoisePackageId ||
        result.manifest.protocolVersion != kModelServiceProtocolVersion ||
        result.manifest.adapterVersion != request.exactAdapterVersion) {
        result.error =
            "The Restormer package is incompatible with this Stack build.";
        return result;
    }
    if (!request.exactPackageVersion.empty() &&
        result.manifest.packageVersion != request.exactPackageVersion) {
        result.error =
            "This project requires Restormer package " +
            request.exactPackageVersion + ".";
        return result;
    }

    if (result.manifest.developmentPackage) {
        if (!trustPolicy.allowDevelopmentPackage) {
            result.error =
                "This local Restormer development package is not allowed by this build.";
            return result;
        }
    } else {
        if (result.manifest.authorizationStatus != "redistribution-approved") {
            result.error =
                "The Restormer checkpoint authorization has not passed release review.";
            return result;
        }
        const bool allowlisted = std::find(
            trustPolicy.allowlistedManifestSha256.begin(),
            trustPolicy.allowlistedManifestSha256.end(),
            result.manifestSha256) !=
            trustPolicy.allowlistedManifestSha256.end();
        if (!allowlisted) {
            result.error =
                "The Restormer package is not signed and allowlisted for this Stack release.";
            return result;
        }
    }

    const ModelKind requestedKind = ModelKindForMethod(request.method);
    const auto selected = std::find_if(
        result.manifest.models.begin(),
        result.manifest.models.end(),
        [requestedKind](const ModelArtifact& model) {
            return model.kind == requestedKind;
        });
    if (selected == result.manifest.models.end()) {
        result.error =
            "The selected Restormer model is missing from the package.";
        return result;
    }
    result.selectedModel = *selected;
    if (!request.exactModelSha256.empty() &&
        LowerAscii(request.exactModelSha256) !=
            LowerAscii(result.selectedModel.sha256)) {
        result.error =
            "This project requires a different frozen Restormer model hash.";
        return result;
    }

    if (!VerifyArtifact(packageRoot, result.manifest.service, result.error)) {
        return result;
    }
    for (const ModelArtifact& model : result.manifest.models) {
        if (!VerifyArtifact(packageRoot, model, result.error)) {
            return result;
        }
    }
    for (const Artifact& artifact : result.manifest.runtimeArtifacts) {
        if (!VerifyArtifact(packageRoot, artifact, result.error)) {
            return result;
        }
    }
    for (const Artifact& artifact : result.manifest.legalArtifacts) {
        if (!VerifyArtifact(packageRoot, artifact, result.error)) {
            return result;
        }
    }

    result.ok = true;
    return result;
}

} // namespace Stack::Restormer
