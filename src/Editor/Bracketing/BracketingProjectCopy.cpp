#include "Editor/EditorModule.h"
#include "BracketingSession.h"
#include "Persistence/BracketingProject.h"
#include "Project/BracketingCopy.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

bool EditorModule::CaptureBracketingDraftForCopy(
    const Stack::Project::ProjectStoreHandle& store,
    const Stack::Project::ProjectStoreTransaction& transaction,
    Stack::Project::RawProjectSnapshot& snapshot, std::string& error) const {
    if (!m_Bracketing || m_Bracketing->newProject || !m_Bracketing->processRequired) return true;
    const auto& ui = *m_Bracketing;
    Stack::Project::BracketingCopy copy;
    copy.sources = ui.sources;
    copy.recipe = ui.recipe;
    copy.setId = ui.setId;
    copy.projectName = ui.projectName;
    copy.metadataPending = static_cast<bool>(ui.metadataJob);
    return Stack::Project::StageBracketingCopy(copy, store, transaction, snapshot, error);
}
