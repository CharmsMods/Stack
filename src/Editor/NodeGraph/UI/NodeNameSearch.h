#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Stack::Editor {

struct NodeNameSearch {
    explicit NodeNameSearch(std::string_view text);
    std::vector<std::string> words;
    std::string joined;
};

// Lower is better. A negative result means the name did not match.
int RankNodeName(const NodeNameSearch& query, std::string_view label,
    std::string_view aliases = {}, std::string_view category = {});

}
