#pragma once
#include "AutoBracketCoordinator.h"
#include "Editor/Bracketing/ProcessingPresentation.h"
namespace Stack::AutoBracket {
struct Presentation {
    Editor::ProcessingPresentation animation;
    std::shared_ptr<Work> completed;
    std::uint64_t generation=0;
    bool visible=true;
    unsigned texture=0;
    unsigned width=0,height=0;
    ~Presentation();
    void Begin(const std::shared_ptr<Work>&,double now);
    void Complete(const std::shared_ptr<Work>&,double now);
    void Tick(double now);
};
void DrawPresentation(Presentation&,AutoBracketCoordinator&,bool workspace=false);
}
