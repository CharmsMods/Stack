#pragma once
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawGraphOperation.h"

namespace Stack::RawRecipe {
// Current-format projects only. Scene tone and spatial detail deliberately
// do not preserve the appearance or representation of earlier recipes.
inline bool IsCanonicalWorkspaceSourceRecipeDocument(const nlohmann::json& value) {
    if (!value.is_object() || value.value("rawRecipeRole", std::string{}) != "workspace-source") return false;
    const auto version = value.find("rawRecipeVersion");
    return version != value.end() && version->is_number_integer() &&
        version->get<int>() == kRawDevelopmentRecipeVersion &&
        SerializeWorkspaceSourceRecipe(DeserializeRecipe(value)).dump() == value.dump();
}
inline bool IsCanonicalRawRecipeDocument(const nlohmann::json& value) {
    if (!value.is_object()) return false;
    if (value.contains("rawRecipeRole")) return IsCanonicalWorkspaceSourceRecipeDocument(value);
    const auto version = value.find("rawRecipeVersion");
    return version != value.end() && version->is_number_integer() &&
        version->get<int>() == kRawDevelopmentRecipeVersion &&
        SerializeRecipe(DeserializeRecipe(value)).dump() == value.dump();
}
}
