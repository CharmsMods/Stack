#pragma once

#include <filesystem>
#include <string>

namespace AppLegal {

enum class Document {
    Eula,
    Privacy,
    SourceLicense,
    ThirdPartyNotices,
    ThirdPartyDirectory,
    LegalDirectory
};

class Manager {
public:
    void Initialize();

    bool IsAccepted() const;
    bool IsEulaFileValid() const;
    bool Accept(std::string* errorMessage = nullptr);

    const std::string& GetEulaText() const;
    const std::string& GetStatusMessage() const;
    std::string GetAcceptanceLocationLabel() const;

    bool OpenDocument(Document document, std::string* errorMessage = nullptr) const;

private:
    bool LoadAcceptance();
    bool SavePortableAcceptance(std::string* errorMessage) const;
    bool SaveInstalledAcceptance(std::string* errorMessage) const;
    std::filesystem::path GetPortableAcceptancePath() const;
    std::filesystem::path GetDocumentPath(Document document) const;

    bool m_Accepted = false;
    bool m_EulaFileValid = false;
    bool m_InstalledBuild = false;
    bool m_LocalTestBuild = false;
    std::string m_EulaText;
    std::string m_StatusMessage;
};

} // namespace AppLegal
