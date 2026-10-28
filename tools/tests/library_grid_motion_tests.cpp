#include "Library/Internal/LibraryModuleUIHelpers.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>

static bool track = false;
static std::size_t allocations = 0;
void* operator new(std::size_t size) {
    if (track) ++allocations;
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
namespace ImGuiExtras {
// PRODUCTION_ANIMATE
}
namespace Stack::Library::ModuleUI {
// PRODUCTION_SIZE
}
using namespace Stack::Library::ModuleUI;
struct Fixture {
    std::vector<std::shared_ptr<ProjectEntry>> projects;
    std::vector<std::shared_ptr<AssetEntry>> assets;
    std::vector<LibraryCachedPackedCard> m_CachedPackedCards, animatedCards;
    std::unordered_map<std::string, LibraryCardMotionState> m_ProjectCardMotion, m_AssetCardMotion;
    std::unordered_set<std::string> m_IntroducedProjectCards, m_IntroducedAssetCards;
    bool m_ShowAssets = false, reducedMotion = false, searchMovementReady = true;
    int frameCount = 1;
    double now = 1, m_SearchLayoutQueryChangedAt = 0;
    float dt = 1.f / 60, m_LibraryViewScale = 1, searchResultScale = 1;
    static constexpr float kLibraryViewScaleMin = .55f, kLibraryViewScaleMax = 1.8f;
    void run() { animatedCards.clear();
        // PRODUCTION_LOOP
    }
    void reference() { animatedCards.clear();
        // OWNED_KEY_LOOP
    }
};
bool Check(bool value, const char* message) { if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
bool Equal(const Fixture& a, const Fixture& b) {
    if (a.animatedCards.size() != b.animatedCards.size() ||
        a.m_IntroducedProjectCards != b.m_IntroducedProjectCards || a.m_IntroducedAssetCards != b.m_IntroducedAssetCards) return false;
    for (std::size_t i=0;i<a.animatedCards.size();++i) {
        const auto& x=a.animatedCards[i]; const auto& y=b.animatedCards[i];
        if (x.index!=y.index || x.x!=y.x || x.y!=y.y || x.width!=y.width || x.height!=y.height || x.scaleMultiplier!=y.scaleMultiplier) return false;
    }
    const auto& x=a.m_ShowAssets?a.m_AssetCardMotion:a.m_ProjectCardMotion;
    const auto& y=b.m_ShowAssets?b.m_AssetCardMotion:b.m_ProjectCardMotion;
    if(x.size()!=y.size())return false;
    for(const auto& pair:x) {
        const auto it=y.find(pair.first);if(it==y.end())return false;
        const auto& p=pair.second;const auto& q=it->second;
        if(p.layoutX!=q.layoutX || p.layoutY!=q.layoutY || p.layoutScale!=q.layoutScale ||
           p.layoutTargetX!=q.layoutTargetX || p.layoutTargetY!=q.layoutTargetY || p.layoutTargetScale!=q.layoutTargetScale ||
           p.layoutStartX!=q.layoutStartX || p.layoutStartY!=q.layoutStartY || p.layoutStartScale!=q.layoutStartScale ||
           p.layoutStartedAt!=q.layoutStartedAt || p.layoutMoving!=q.layoutMoving || p.layoutInitialized!=q.layoutInitialized ||
           p.entrance!=q.entrance || p.entranceInitialized!=q.entranceInitialized || p.entranceStartTime!=q.entranceStartTime ||
           p.reveal!=q.reveal || p.lastSeenFrame!=q.lastSeenFrame)return false;
    }
    return true;
}
// PACKING_HELPERS
namespace CurrentPacking { int passes=0;
// PRODUCTION_PACKING
}
namespace OriginalPacking { int passes=0;
// ORIGINAL_PACKING
}
bool EqualPacked(const std::vector<LibraryPackedCard>& a, const std::vector<LibraryPackedCard>& b) {
    if(a.size()!=b.size())return false;
    for(std::size_t i=0;i<a.size();++i) {
        const auto& x=a[i];const auto& y=b[i];
        if(x.index!=y.index || x.pos.x!=y.pos.x || x.pos.y!=y.pos.y ||
           x.size.x!=y.size.x || x.size.y!=y.size.y || x.scaleMultiplier!=y.scaleMultiplier)return false;
    }
    return true;
}
bool PackingTests() {
    bool passed=true;
    const auto compare=[&](const std::vector<LibraryPackedCard>& cards,float width,float gap,float scale) {
        const auto before=cards;CurrentPacking::passes=OriginalPacking::passes=0;
        const auto actual=CurrentPacking::PackLibraryCards(cards,width,gap,scale);
        const auto expected=OriginalPacking::PackLibraryCards(cards,width,gap,scale);
        passed=Check(EqualPacked(actual,expected),"packing exact order/position/size/growth parity")&&passed;
        passed=Check(EqualPacked(cards,before),"packing leaves input unchanged")&&passed;
    };
    for(int count:{0,1,80,100}) {
        std::vector<LibraryPackedCard> cards;
        for(int i=0;i<count;++i){LibraryPackedCard c;c.index=i;c.size=ComputeLibraryCardSize(4000,3000,1);cards.push_back(c);}
        compare(cards,1100,22,1.08f);
        passed=Check(CurrentPacking::passes==1,"identical-aspect orders pack once")&&passed;
        passed=Check(OriginalPacking::passes==(count<=80?4:3),"oracle retains all original orders")&&passed;
        std::reverse(cards.begin(),cards.end());compare(cards,720,0,1);
    }
    // Orders A,B,A,B: these exact nonadjacent repeats must not be deduplicated.
    std::vector<LibraryPackedCard> alternating(2);alternating[0].index=7;alternating[0].size={100,300};
    alternating[1].index=2;alternating[1].size={400,200};
    compare(alternating,900,22,1.08f);
    passed=Check(CurrentPacking::passes==4,"nonadjacent order repeats preserve comparison sequence")&&passed;
    // Bounded deterministic adversarial mixed shapes, ties, metadata, narrow
    // fallback widths and caller scale/gap ranges (not a full-frame benchmark).
    std::uint32_t seed=17;
    for(int trial=0;trial<16;++trial) {
        std::vector<LibraryPackedCard> cards;
        for(int i=0;i<12+trial;++i) {
            seed=seed*1664525u+1013904223u;LibraryPackedCard c;
            c.index=(i*7)%31;c.pos={float(i%3),float(-i)};
            c.size=ComputeLibraryCardSize(float(1+seed%5),float(1+(seed>>8)%5),.55f+.25f*(trial%6));
            c.scaleMultiplier=.7f+.1f*(i%4);cards.push_back(c);
        }
        if(trial%2)std::reverse(cards.begin(),cards.end());
        compare(cards,trial%4==0?1.f:float(600+trial*41),trial%3==0?-2.f:22.f,trial%2?1.f:1.08f);
    }
    return passed;
}
int main() {
    bool passed=PackingTests();
    for(bool assets:{false,true}) {
        Fixture current;current.m_ShowAssets=assets;
        for(int i=0;i<1000;++i) {
            const std::string path="C:/Users/Photographer/Stack/Projects/long_project_filename_"+std::to_string(i)+".stack";
            auto project=std::make_shared<ProjectEntry>();project->fileName=path;project->sourceWidth=600;project->sourceHeight=400;
            auto asset=std::make_shared<AssetEntry>();asset->fileName=path;asset->width=600;asset->height=400;
            current.projects.push_back(project);current.assets.push_back(asset);
            LibraryCachedPackedCard card;card.index=i;card.x=float(i%4)*250;card.y=float(i/4)*180;
            current.m_CachedPackedCards.push_back(card);
        }
        current.animatedCards.reserve(1032);
        Fixture owned=current;owned.animatedCards.reserve(1032);
        auto compare=[&](const char* message){current.run();owned.reference();passed=Check(Equal(current,owned),message)&&passed;};
        compare("cold motion outputs and stored identities match owned keys");
        ++current.frameCount;++owned.frameCount;current.now=owned.now=1.1;
        allocations=0;track=true;current.run();track=false;owned.reference();
        passed=Check(allocations==0,"warm card motion must not copy every path")&&passed;
        passed=Check(Equal(current,owned),"warm motion matches owned keys")&&passed;
        std::reverse(current.m_CachedPackedCards.begin(),current.m_CachedPackedCards.end());
        owned.m_CachedPackedCards=current.m_CachedPackedCards;
        for(auto& card:current.m_CachedPackedCards)card.x+=50;
        owned.m_CachedPackedCards=current.m_CachedPackedCards;
        current.now=owned.now=1.15;compare("reordered interrupted targets retain identity and poses");
        current.now=owned.now=1.25;compare("moving poses match owned keys");
        current.projects[4].reset();owned.projects[4].reset();current.assets[4].reset();owned.assets[4].reset();
        current.m_CachedPackedCards.push_back({2000});owned.m_CachedPackedCards=current.m_CachedPackedCards;
        current.reducedMotion=owned.reducedMotion=true;compare("null and out-of-range slots remain skipped");
        const auto oldName=current.projects[0]->fileName;
        current.projects[0]->fileName="C:/replacement/project.stack";current.assets[0]->fileName=current.projects[0]->fileName;
        compare("entry replacement leaves previously stored motion keys owned");
        const auto& states=assets?current.m_AssetCardMotion:current.m_ProjectCardMotion;
        passed=Check(states.count(oldName)&&states.count("C:/replacement/project.stack"),"motion maps own old and new identities")&&passed;
    }
    if(!passed)return 1;
    std::cout<<"Library packing and grid motion keys passed (cold, warm allocation, reorder, interrupted motion, null slots, ownership).\n";
}
