#pragma once
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawGraphOperation.h"
#include <string_view>

namespace Stack::RawRecipe {
// Current-format projects only. Scene tone and spatial detail deliberately
// do not preserve the appearance or representation of earlier recipes.
inline nlohmann::json CanonicalRawRecipeForComparison(const nlohmann::json& value) {
    const auto recipe=DeserializeRecipe(value);
    auto canonical=value.contains("rawRecipeRole")
        ? SerializeWorkspaceSourceRecipe(recipe) : SerializeRecipe(recipe);
    const auto storedView=value.find("viewTransform");
    auto canonicalView=canonical.find("viewTransform");
    if(storedView!=value.end() && storedView->is_object() &&
        canonicalView!=canonical.end() && canonicalView->is_object()) {
        // These additive v29 fields already decode to a neutral SDR finish
        // when absent. Missing fields must not reject that supported document;
        // explicit values and every other field keep exact validation.
        for(const auto* field:{"sdrBrightness","sdrHighlightDetail","sdrSaturation"}) {
            const float neutral=std::string_view(field)=="sdrSaturation"?1.f:0.f;
            if(!storedView->contains(field) && canonicalView->at(field)==neutral)
                canonicalView->erase(field);
        }
    }
    return canonical;
}
inline bool IsCanonicalWorkspaceSourceRecipeDocument(const nlohmann::json& value) {
    if (!value.is_object() || value.value("rawRecipeRole", std::string{}) != "workspace-source") return false;
    const auto version = value.find("rawRecipeVersion");
    return version != value.end() && version->is_number_integer() &&
        version->get<int>() == kRawDevelopmentRecipeVersion &&
        CanonicalRawRecipeForComparison(value).dump() == value.dump();
}
inline bool IsCanonicalRawRecipeDocument(const nlohmann::json& value) {
    if (!value.is_object()) return false;
    if (value.contains("rawRecipeRole")) return IsCanonicalWorkspaceSourceRecipeDocument(value);
    const auto version = value.find("rawRecipeVersion");
    return version != value.end() && version->is_number_integer() &&
        version->get<int>() == kRawDevelopmentRecipeVersion &&
        CanonicalRawRecipeForComparison(value).dump() == value.dump();
}
}
