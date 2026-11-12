#include "Editor/NodeGraph/UI/NodeNameSearch.h"
#include <algorithm>
#include <cctype>

namespace Stack::Editor {
namespace {

bool NearWord(std::string_view query, std::string_view word) {
    if (query.size() < 4 || query.size() + 1 < word.size() || word.size() + 1 < query.size()) return false;
    const auto i = std::size_t(std::mismatch(query.begin(), query.end(), word.begin(), word.end()).first - query.begin());
    if (query.size() < word.size()) return query.substr(i) == word.substr(i + 1);
    if (word.size() < query.size()) return query.substr(i + 1) == word.substr(i);
    return i == query.size() || query.substr(i + 1) == word.substr(i + 1) ||
        (i + 1 < query.size() && query[i] == word[i + 1] && query[i + 1] == word[i] &&
            query.substr(i + 2) == word.substr(i + 2));
}

int Rank(const NodeNameSearch& query, const NodeNameSearch& name) {
    if (name.joined.empty()) return -1;
    if (name.words == query.words) return 0;
    if (name.words.front().compare(0, query.joined.size(), query.joined) == 0) return 1;
    if (name.joined.compare(0, query.joined.size(), query.joined) == 0) return 2;
    const auto matches = [&](bool fuzzy) {
        return std::all_of(query.words.begin(), query.words.end(), [&](const auto& term) {
            return std::any_of(name.words.begin(), name.words.end(), [&](const auto& word) {
                return word.compare(0, term.size(), term) == 0 || (fuzzy && NearWord(term, word));
            });
        });
    };
    if (matches(false) || (query.joined.size() > 1 && name.joined.find(query.joined) != std::string::npos)) return 3;
    if (matches(true) || NearWord(query.joined, name.joined)) return 5;
    return -1;
}
}

NodeNameSearch::NodeNameSearch(std::string_view text) {
    bool start = true;
    for (unsigned char ch : text) {
        if (ch < 128 && !std::isalnum(ch)) {
            start = true;
            continue;
        }
        if (start) words.emplace_back();
        start = false;
        const char letter = ch < 128 ? static_cast<char>(std::tolower(ch)) : static_cast<char>(ch);
        words.back().push_back(letter);
        joined.push_back(letter);
    }
}

int RankNodeName(const NodeNameSearch& query, std::string_view label,
    std::string_view aliases, std::string_view category) {
    if (query.joined.empty()) return 0;
    const int rank = Rank(query, NodeNameSearch(label));
    if (rank >= 0 && rank <= 3) return rank;
    bool fuzzyAlias = false;
    while (!aliases.empty()) {
        const auto end = aliases.find('|');
        const int aliasRank = Rank(query, NodeNameSearch(aliases.substr(0, end)));
        if (aliasRank >= 0 && aliasRank <= 3) return 4;
        fuzzyAlias |= aliasRank == 5;
        if (end == aliases.npos) break;
        aliases.remove_prefix(end + 1);
    }
    const int categoryRank = Rank(query, NodeNameSearch(category));
    return categoryRank >= 0 && categoryRank <= 3 ? 4 : fuzzyAlias ? 5 : rank;
}
}
