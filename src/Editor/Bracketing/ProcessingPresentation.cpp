#include "ProcessingPresentation.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor {
using namespace Raw::Bracketing;
ProcessingPlane ProcessingPlanePose(ProcessingStage stage,std::size_t index,std::size_t,unsigned group,double,bool) {
    ProcessingPlane p;p.x=.10f;p.y=.26f+float(index%5)*.105f;p.width=.135f;p.height=.09f;
    p.yaw=-.28f;p.z=float(index)*.01f;
    if(stage==ProcessingStage::Preparing||stage==ProcessingStage::Assets) {
        p.x=.25f+.24f*float(group%3);p.y=.36f+.12f*float(index%3);p.width=.27f;p.height=.18f;p.yaw=-.18f;
    }
    return p;
}
}
