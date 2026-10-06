#include "AppLegal.h"

#include "AppLegalVersion.h"
#include "AppPaths.h"
#include "PlatformHelpers.h"
#include "ThirdParty/json.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#endif

#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <vector>

namespace AppLegal {
namespace {

using json = nlohmann::json;

constexpr const char* kAcceptanceFileName = "StackLegalAcceptance.json";

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    return std::string(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
}

std::string UtcTimestampNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm utc {};
#if defined(_WIN32)
    gmtime_s(&utc, &raw);
#else
    gmtime_r(&raw, &utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

bool ComputeFileSha256(const std::filesystem::path& path, std::string& outDigest) {
#if defined(_WIN32)
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD bytesWritten = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0 ||
        BCryptGetProperty(
            algorithm,
            BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength),
            sizeof(objectLength),
            &bytesWritten,
            0) != 0) {
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        return false;
    }

    std::vector<unsigned char> objectBuffer(objectLength);
    if (BCryptCreateHash(
            algorithm,
            &hash,
            objectBuffer.data(),
            objectLength,
            nullptr,
            0,
            0) != 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::array<char, 64 * 1024> buffer {};
    while (file.good()) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();
        if (count > 0 && BCryptHashData(
                hash,
                reinterpret_cast<PUCHAR>(buffer.data()),
                static_cast<ULONG>(count),
                0) != 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return false;
        }
    }

    unsigned char digest[32] = {};
    const bool success = BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!success) {
        return false;
    }

    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (unsigned char byte : digest) {
        stream << std::setw(2) << static_cast<unsigned int>(byte);
    }
    outDigest = stream.str();
    return true;
#else
    (void)path;
    (void)outDigest;
    return false;
#endif
}

#if defined(_WIN32)
std::wstring RegistrySubkey(bool localTest) {
    return localTest
        ? L"SOFTWARE\\Darynn Ho\\Stack Local Test\\Legal"
        : L"SOFTWARE\\Darynn Ho\\Stack\\Legal";
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int byteCount = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (byteCount <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(byteCount), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            byteCount,
            nullptr,
            nullptr) != byteCount) {
        return {};
    }
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int characterCount = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (characterCount <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(characterCount), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            characterCount) != characterCount) {
        return {};
    }
    return result;
}

bool ReadRegistryString(HKEY key, const wchar_t* valueName, std::string& outValue) {
    DWORD type = 0;
    DWORD byteCount = 0;
    if (RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &byteCount) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || byteCount < sizeof(wchar_t)) {
        return false;
    }
    std::vector<wchar_t> buffer(byteCount / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(
            key,
            valueName,
            nullptr,
            &type,
            reinterpret_cast<BYTE*>(buffer.data()),
            &byteCount) != ERROR_SUCCESS) {
        return false;
    }
    std::wstring value(buffer.data());
    outValue = WideToUtf8(value);
    return value.empty() || !outValue.empty();
}

bool WriteRegistryString(HKEY key, const wchar_t* valueName, const std::string& value) {
    const std::wstring wide = Utf8ToWide(value);
    if (!value.empty() && wide.empty()) {
        return false;
    }
    return RegSetValueExW(
        key,
        valueName,
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(wide.c_str()),
        static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}
#endif

} // namespace

void Manager::Initialize() {
    m_InstalledBuild = AppPaths::IsInstalledBuild();
    m_LocalTestBuild = AppPaths::IsLocalTestBuild();
    const std::filesystem::path eulaPath = GetDocumentPath(Document::Eula);
    m_EulaText = ReadTextFile(eulaPath);

    std::string actualHash;
    m_EulaFileValid = !m_EulaText.empty() &&
        ComputeFileSha256(eulaPath, actualHash) &&
        actualHash == AppLegalVersion::kEulaSha256;
    if (!m_EulaFileValid) {
        m_Accepted = false;
        m_StatusMessage =
            "Stack cannot verify the installed EULA. Reinstall from an official package or rebuild the legal payload.";
        return;
    }

    m_Accepted = LoadAcceptance();
    m_StatusMessage = m_Accepted
        ? "The current Stack EULA has been accepted."
        : "Read and accept the current Stack EULA to continue.";
}

bool Manager::IsAccepted() const { return m_Accepted; }
bool Manager::IsEulaFileValid() const { return m_EulaFileValid; }
const std::string& Manager::GetEulaText() const { return m_EulaText; }
const std::string& Manager::GetStatusMessage() const { return m_StatusMessage; }

std::filesystem::path Manager::GetPortableAcceptancePath() const {
    return AppPaths::GetSettingsDirectory() / kAcceptanceFileName;
}

std::string Manager::GetAcceptanceLocationLabel() const {
    if (m_InstalledBuild) {
        return m_LocalTestBuild
            ? "HKLM\\Software\\Darynn Ho\\Stack Local Test\\Legal"
            : "HKLM\\Software\\Darynn Ho\\Stack\\Legal";
    }
    return GetPortableAcceptancePath().u8string();
}

bool Manager::LoadAcceptance() {
    std::string eulaVersion;
    std::string eulaSha256;

    if (m_InstalledBuild) {
#if defined(_WIN32)
        HKEY key = nullptr;
        const LONG openResult = RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            RegistrySubkey(m_LocalTestBuild).c_str(),
            0,
            KEY_QUERY_VALUE | KEY_WOW64_64KEY,
            &key);
        if (openResult != ERROR_SUCCESS) {
            return false;
        }
        const bool read =
            ReadRegistryString(key, L"EulaVersion", eulaVersion) &&
            ReadRegistryString(key, L"EulaSha256", eulaSha256);
        RegCloseKey(key);
        if (!read) {
            return false;
        }
#else
        return false;
#endif
    } else {
        std::ifstream file(GetPortableAcceptancePath());
        if (!file.is_open()) {
            return false;
        }
        const json root = json::parse(file, nullptr, false);
        if (root.is_discarded() || !root.is_object() ||
            root.value("schemaVersion", 0) != AppLegalVersion::kAcceptanceSchemaVersion) {
            return false;
        }
        eulaVersion = root.value("eulaVersion", std::string());
        eulaSha256 = root.value("eulaSha256", std::string());
    }

    return eulaVersion == AppLegalVersion::kEulaVersion &&
        eulaSha256 == AppLegalVersion::kEulaSha256;
}

bool Manager::Accept(std::string* errorMessage) {
    if (!m_EulaFileValid) {
        if (errorMessage != nullptr) {
            *errorMessage = m_StatusMessage;
        }
        return false;
    }

    const bool saved = m_InstalledBuild
        ? SaveInstalledAcceptance(errorMessage)
        : SavePortableAcceptance(errorMessage);
    if (!saved) {
        return false;
    }
    m_Accepted = true;
    m_StatusMessage = "The current Stack EULA has been accepted.";
    return true;
}

bool Manager::SavePortableAcceptance(std::string* errorMessage) const {
    std::error_code ec;
    std::filesystem::create_directories(AppPaths::GetSettingsDirectory(), ec);
    if (ec) {
        if (errorMessage != nullptr) {
            *errorMessage = "Stack could not create the portable settings directory.";
        }
        return false;
    }

    json root = {
        { "schemaVersion", AppLegalVersion::kAcceptanceSchemaVersion },
        { "eulaVersion", AppLegalVersion::kEulaVersion },
        { "eulaSha256", AppLegalVersion::kEulaSha256 },
        { "privacyVersion", AppLegalVersion::kPrivacyVersion },
        { "acceptedAtUtc", UtcTimestampNow() },
        { "acceptanceSource", "portable-application" }
    };
    std::ofstream file(GetPortableAcceptancePath(), std::ios::trunc);
    if (!file.is_open()) {
        if (errorMessage != nullptr) {
            *errorMessage = "Stack could not save portable legal acceptance.";
        }
        return false;
    }
    file << root.dump(2) << '\n';
    return file.good();
}

bool Manager::SaveInstalledAcceptance(std::string* errorMessage) const {
#if defined(_WIN32)
    HKEY key = nullptr;
    DWORD disposition = 0;
    const LONG createResult = RegCreateKeyExW(
        HKEY_LOCAL_MACHINE,
        RegistrySubkey(m_LocalTestBuild).c_str(),
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE | KEY_WOW64_64KEY,
        nullptr,
        &key,
        &disposition);
    if (createResult != ERROR_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = "Stack could not update the installed EULA acceptance record. Run the approved installer again.";
        }
        return false;
    }
    (void)disposition;
    const bool saved =
        WriteRegistryString(key, L"EulaVersion", AppLegalVersion::kEulaVersion) &&
        WriteRegistryString(key, L"EulaSha256", AppLegalVersion::kEulaSha256) &&
        WriteRegistryString(key, L"PrivacyVersion", AppLegalVersion::kPrivacyVersion) &&
        WriteRegistryString(key, L"AcceptedAtUtc", UtcTimestampNow()) &&
        WriteRegistryString(key, L"AcceptanceSource", "installed-application");
    RegCloseKey(key);
    if (!saved && errorMessage != nullptr) {
        *errorMessage = "Stack could not save the installed EULA acceptance record.";
    }
    return saved;
#else
    if (errorMessage != nullptr) {
        *errorMessage = "Installed legal acceptance is supported only on Windows.";
    }
    return false;
#endif
}

std::filesystem::path Manager::GetDocumentPath(Document document) const {
    const std::filesystem::path legalDir = AppPaths::GetLegalDirectory();
    switch (document) {
    case Document::Eula: return legalDir / "EULA.txt";
    case Document::Privacy: return legalDir / "PRIVACY.md";
    case Document::SourceLicense: return legalDir / "LICENSE";
    case Document::ThirdPartyNotices: return legalDir / "THIRD_PARTY_NOTICES.md";
    case Document::ThirdPartyDirectory: return legalDir / "ThirdParty";
    case Document::LegalDirectory: return legalDir;
    }
    return legalDir;
}

bool Manager::OpenDocument(Document document, std::string* errorMessage) const {
    return PlatformHelpers::OpenPath(GetDocumentPath(document), errorMessage);
}

} // namespace AppLegal
