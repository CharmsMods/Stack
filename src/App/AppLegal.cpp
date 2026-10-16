#include "AppLegal.h"

#include "AppPaths.h"
#include "PlatformHelpers.h"

namespace AppLegal {

std::filesystem::path Manager::GetDocumentPath(Document document) const {
    const std::filesystem::path legalDir = AppPaths::GetLegalDirectory();
    switch (document) {
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
