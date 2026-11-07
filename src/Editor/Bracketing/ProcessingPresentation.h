#pragma once
#include "Project/BracketingProgress.h"
namespace Stack::Editor {
using Project::ProcessingSnapshot;
using Project::ProcessingMailbox;
struct ProcessingPresentation {
    std::shared_ptr<ProcessingMailbox> mailbox;
    std::string recipeIdentity,projectId;
    std::uint64_t generation=0,expectedHash=0;
    bool active=false,canceling=false,waitingForRaw=false;
    double began=0,finishedAt=-1;
};
}
