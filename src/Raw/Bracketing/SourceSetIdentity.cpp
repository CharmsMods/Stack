#include "SourceSetIdentity.h"
#include "NodeMath/ContractTypes.h"
#include <set>

namespace Raw::Bracketing {
std::string SourceSetIdentity(const std::vector<CaptureContentIdentity>& sources) {
    std::set<std::string> identities;
    for (const auto& source : sources) {
        if (source.sha256.empty()) return {};
        identities.insert(source.sha256 + ":" + std::to_string(source.bytes));
    }
    std::string text = "stack.auto-bracket.sources.v1\n";
    for (const auto& identity : identities) text += identity + "\n";
    return identities.size() < 2 ? std::string() : Stack::NodeMath::Sha256ContentIdentity(text);
}
} // namespace Raw::Bracketing
