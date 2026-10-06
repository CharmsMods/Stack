#pragma once
#include "Persistence/ProjectStore.h"
#include "Raw/Bracketing/Processor.h"

namespace Stack::Project {
struct BracketingJobRequest {
    Project::ProjectStoreHandle store;
    std::filesystem::path cacheRoot;
    Project::RawProjectSnapshot snapshot;
    std::string setId;
    Raw::Bracketing::BracketingRecipe recipe;
    std::shared_ptr<const Raw::Bracketing::BracketingAnalysis> analysis;
    Raw::OpenGlTaskExecutor executeOpenGlTask;
    std::function<bool()> shouldCancel;
    std::function<void(double,const std::string&)> reportProgress;
    std::function<void(const Raw::Bracketing::ProcessingProgress&)> reportPresentation;
    bool restoreSavedResult = true;
    bool preparationOnly = false;
    std::function<void()> didRestoreResult;
};
// Worker-only execution. No editor state, selection, or document adoption.
Raw::Bracketing::BracketingResult RunBracketingJob(const BracketingJobRequest&);

}
