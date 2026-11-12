#pragma once
#include <string>
#include <vector>
namespace Stack::Search::Detail {
struct PreparedName { std::wstring compact; std::vector<std::wstring> words; };
PreparedName PrepareName(const std::wstring& folded);
// -1 means no match; lower scores rank ahead of typo matches.
int MatchName(const std::wstring& name,const PreparedName& prepared,
    const std::wstring& query,const PreparedName& queryPrepared);
}
