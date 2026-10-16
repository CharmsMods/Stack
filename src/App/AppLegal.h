#pragma once

#include <filesystem>
#include <string>

namespace AppLegal {

enum class Document {
    ThirdPartyNotices,
    ThirdPartyDirectory,
    LegalDirectory
};

// Opens the attribution documents shipped with the application on user request.
class Manager {
public:
    bool OpenDocument(Document document, std::string* errorMessage = nullptr) const;

private:
    std::filesystem::path GetDocumentPath(Document document) const;
};

} // namespace AppLegal
