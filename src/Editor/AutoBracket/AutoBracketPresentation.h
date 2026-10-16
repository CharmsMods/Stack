#pragma once
#include "AutoBracketCoordinator.h"
#include "Editor/Bracketing/ProcessingPresentation.h"
namespace Stack::AutoBracket {
struct Presentation {
    Editor::ProcessingPresentation animation;
    std::shared_ptr<Work> completed;
    std::uint64_t generation=0;
    bool visible=true,foreground=true;
    double cardsPublication=-1;
    std::uint64_t cardsSequence=0;
    unsigned texture=0;
    unsigned width=0,height=0;
    ~Presentation();
    void Begin(const std::shared_ptr<Work>&,double now);
    void Complete(const std::shared_ptr<Work>&,double now);
    void Tick(double now);
    void SetForeground(bool);
};
void DrawPresentation(Presentation&,AutoBracketCoordinator&,bool workspace=false);
}
