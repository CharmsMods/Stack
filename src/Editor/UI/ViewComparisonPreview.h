#pragma once

#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/Bracketing/Processor.h"
#include <memory>
#include <string>
#include <vector>

namespace Stack::Editor {

// One editor's optional, immutable before-image. Jobs capture their inputs and
// never publish through the main image worker or retain an EditorModule pointer.
class ViewComparisonPreview {
public:
    struct Capture { std::string id, label, unavailable; };
    ViewComparisonPreview() = default;
    ~ViewComparisonPreview();
    void Reset();
    void Suspend();
    void UpdateRaw(const std::shared_ptr<const Raw::RawImageData>& source,
        const RawRecipe::RawDevelopmentRecipe& recipe, std::uint64_t revision, int edge);
    void UpdateBracket(const std::shared_ptr<const Raw::Bracketing::BracketingResult>& result,
        const Raw::Bracketing::BracketingRecipe& recipe,
        const std::vector<Capture>& captures, const RawRecipe::RawDevelopmentRecipe& development,
        std::uint64_t revision, int edge);
    bool NeedsBracketCatalog(const Raw::Bracketing::BracketingResult* result) const;
    void SetUnavailable(const char* reason);
    void DrawControls();
    unsigned Texture() const { return m_Texture; }
    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    const std::string& Status() const { return m_Status; }
    bool HasCaptureControls() const { return bool(m_Bracket); }
private:
    struct Job;
    void Invalidate();
    void Poll();
    void RetireTexture();
    void CollectRetiredTextures();
    void Start(std::shared_ptr<const Raw::RawImageData> raw,
        RawRecipe::RawDevelopmentRecipe recipe,
        std::shared_ptr<const Raw::Bracketing::BracketingResult> bracket = {});
    std::shared_ptr<Job> m_Job;
    std::shared_ptr<const Raw::RawImageData> m_Source;
    std::shared_ptr<const Raw::Bracketing::BracketingResult> m_Bracket;
    Raw::Bracketing::BracketingRecipe m_BracketRecipe;
    RawRecipe::RawDevelopmentRecipe m_Recipe;
    std::vector<Capture> m_Captures;
    std::string m_Selected, m_SelectedLabel, m_RecipeKey, m_Status;
    std::uint64_t m_Revision = ~std::uint64_t(0);
    int m_Edge = 0, m_Width = 0, m_Height = 0;
    unsigned m_Texture = 0;
    struct RetiredTexture { unsigned texture; int frame; };
    std::vector<RetiredTexture> m_RetiredTextures;
    bool m_Matched = false, m_Dirty = true;
    bool m_ControlChanged = false;
};
}
