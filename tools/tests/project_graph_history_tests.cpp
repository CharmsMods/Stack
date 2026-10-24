#include "Project/ProjectGraphHistory.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace Stack::Project;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
ProjectGraphState State(int value){ProjectGraphState s;Stack::Timeline::TimelineTrack t;t.target.nodeId=value;s.animation.tracks.push_back(t);return s;}
int Value(const ProjectGraphState* s){Require(s && !s->animation.tracks.empty(),"missing state");return s->animation.tracks.front().target.nodeId;}
void Undo(ProjectGraphHistory& h,int expected){Require(Value(h.Candidate(false))==expected,"wrong undo state");h.AcceptRestore(false);}
void Redo(ProjectGraphHistory& h,int expected){Require(h.CanRedo(),"redo unavailable");Require(Value(h.Candidate(true))==expected,"wrong redo state");h.AcceptRestore(true);}
ProjectGraphHistory WithRedo(){ProjectGraphHistory h;h.Reset(State(0));h.Observe(State(1),false);h.Observe(State(2),false);Undo(h,1);return h;}
}
int main() try {
 auto h=WithRedo();h.Observe(State(3),true);h.Observe(State(4),true);
 Require(!h.CanRedo(),"redo enabled during provisional gesture");
 Require(h.Candidate(true)==nullptr && h.GestureActive(),"redo committed a provisional gesture");
 Require(Value(h.CancelCandidate())==1,"gesture baseline changed");h.AcceptCancel();
 Redo(h,2);Undo(h,1);Undo(h,0);
 h=WithRedo();h.Observe(State(3),true);h.EndGesture();Require(!h.CanRedo(),"committed edit retained obsolete redo");Undo(h,1);Redo(h,3);
 h=WithRedo();h.Observe(State(3),true);h.Observe(State(4),false);Require(!h.CanRedo(),"release retained obsolete redo");Undo(h,1);Redo(h,4);
 h=WithRedo();h.Observe(State(3),false);Require(!h.CanRedo(),"discrete edit retained obsolete redo");Undo(h,1);Redo(h,3);
 h=WithRedo();h.Observe(State(3),true);Undo(h,1);Redo(h,3);Require(!h.CanRedo(),"obsolete prior branch survived gesture undo");
 h=WithRedo();h.AcceptCancel();Require(h.HasBaseline(),"cancel without gesture destroyed baseline");Redo(h,2);
 h=WithRedo();h.CompleteAsyncEdit(State(5));Require(!h.CanRedo(),"accepted async edit changed existing branch invalidation contract");
 h.Reset(State(0));h.Observe(State(1),true,false);h.EndGesture();Require(Value(h.Candidate(false))==0 && !h.CandidateAffectsPixels(false),"annotation gesture lost history kind");
 std::cout<<"Project graph gesture cancel/commit/undo/redo checks passed.\n";
 return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
