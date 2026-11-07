#include <algorithm>
#include <cstdlib>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
static bool counting=false;
static std::size_t allocations=0;
void* operator new(std::size_t n) {
    if(counting)++allocations;
    if(void* p=std::malloc(n))return p;
    throw std::bad_alloc();
}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
namespace Raw {struct RawMetadata;}
namespace Stack::RawWorkspace {enum class ThumbnailStatus {Unknown};}
namespace Async {enum class TaskState {Idle};}
namespace ImGui {void* GetCurrentContext(){return reinterpret_cast<void*>(1);} int frame=100;int GetFrameCount(){return frame;}}
// PRODUCTION_CONSTANTS
struct EditorModule {
// PRODUCTION_RECORD
    bool m_PermanentGalleryWorkspace=true,m_RawWorkspaceThumbnailTextureTrimNeeded=true;
    std::unordered_map<std::string,RawWorkspaceThumbnailTexture> m_RawWorkspaceThumbnailTextures;
    struct Layout {
        std::unordered_set<std::string> protectedKeys;
        bool Protects(const std::string& key)const{return protectedKeys.count(key)!=0;}
    } m_RawWorkspaceGalleryLayoutAnimation;
    std::vector<unsigned> deleted, m_RetiredGalleryThumbnails;
    std::size_t m_RetiredGalleryThumbnailBytes=0;
    std::unordered_map<unsigned,std::weak_ptr<int>> m_GalleryThumbnailLeases;
    void ReleaseGalleryHdrThumbnail(RawWorkspaceThumbnailTexture& entry) {
        if(entry.hdrTexture) QueueRawWorkspaceThumbnailTextureDelete(entry.hdrTexture);
        entry.hdrTexture=0;
    }
    void QueueRawWorkspaceThumbnailTextureDelete(unsigned texture){deleted.push_back(texture);}
    void TrimRawWorkspaceThumbnailTextureCache();
};
// PRODUCTION_METHOD
static std::string Key(int i){return std::string(100,'x')+std::to_string(i);}
static EditorModule Owner(bool visible=false) {
    EditorModule owner;owner.deleted.reserve(300);
    for(int i=0;i<300;++i) {
        auto& entry=owner.m_RawWorkspaceThumbnailTextures[Key(i)];
        entry.texture=unsigned(i+1);entry.width=512;entry.height=341;entry.lastUseSerial=i;
        entry.lastVisibleFrame=visible?100:-1;
    }
    return owner;
}
static bool Check(bool value,const char* message){if(!value)std::cerr<<message<<'\n';return value;}
static std::size_t Trim(EditorModule& owner) {
    allocations=0;counting=true;owner.TrimRawWorkspaceThumbnailTextureCache();counting=false;return allocations;
}
int main() {
    bool passed=true;
    auto visible=Owner(true),untouched=Owner();
    for(int frame:{100,101}) {
        ImGui::frame=frame;
        passed=Check(Trim(visible)==(frame==100 ? 1 : 0),"Protected overbudget trim should allocate only its candidate vector")&&passed;
        passed=Check(visible.deleted.empty()&&visible.m_RawWorkspaceThumbnailTextures.size()==300&&
            !visible.m_RawWorkspaceThumbnailTextureTrimNeeded,"Visible retention changed")&&passed;
    }
    ImGui::frame=102;visible.m_RawWorkspaceThumbnailTextureTrimNeeded=true;Trim(visible);
    passed=Check(visible.deleted.size()==12&&!visible.m_RawWorkspaceThumbnailTextureTrimNeeded,"Released visible entries did not trim to budget")&&passed;
    for(unsigned i=0;i<12&&i<visible.deleted.size();++i)passed=Check(visible.deleted[i]==i+1,"LRU eviction order changed")&&passed;
    for(int i=0;i<300;++i)passed=Check(visible.m_RawWorkspaceThumbnailTextures.count(Key(i))==(i>=12?1u:0u),"Wrong survivor identity")&&passed;
    passed=Check(Trim(visible)==0,"Settled cache should not allocate or retrim")&&passed;
    passed=Check(untouched.deleted.empty()&&untouched.m_RawWorkspaceThumbnailTextures.size()==300,"Trim crossed owners")&&passed;

    auto partial=Owner();
    for(int i=0;i<10;++i)partial.m_RawWorkspaceGalleryLayoutAnimation.protectedKeys.insert(Key(i));
    for(int i=10;i<20;++i)partial.m_RawWorkspaceThumbnailTextures.at(Key(i)).uploadPending=true;
    Trim(partial);
    passed=Check(partial.deleted.size()==12,"Partial protection changed budget accounting")&&passed;
    for(unsigned i=0;i<12&&i<partial.deleted.size();++i)passed=Check(partial.deleted[i]==i+21,"Protected/uploading entry was evicted")&&passed;
    auto moving=Owner();
    for(int i=0;i<300;++i)moving.m_RawWorkspaceGalleryLayoutAnimation.protectedKeys.insert(Key(i));
    passed=Check(Trim(moving)==1&&Trim(moving)==0&&moving.deleted.empty(),"Motion-protected repeated trim changed")&&passed;
    moving.m_RawWorkspaceGalleryLayoutAnimation.protectedKeys.clear();moving.m_RawWorkspaceThumbnailTextureTrimNeeded=true;Trim(moving);
    passed=Check(moving.deleted==visible.deleted,"Releasing motion changed eviction ordering")&&passed;
    auto hdr=Owner();
    auto lease=std::make_shared<int>();
    for(auto& [key,entry]:hdr.m_RawWorkspaceThumbnailTextures) {
        entry.hdrTexture=1000+entry.texture;entry.hdrWidth=entry.width;entry.hdrHeight=entry.height;
    }
    hdr.m_GalleryThumbnailLeases[1001]=lease;
    Trim(hdr);
    const auto perTile=RawWorkspaceThumbnailResidentBytes(512,341)+RawWorkspaceThumbnailResidentBytes(512,341,8);
    passed=Check(hdr.m_RawWorkspaceThumbnailTextures.size()==kGalleryThumbnailMaxResidentBytes/perTile &&
        hdr.m_RawWorkspaceThumbnailTextures.count(Key(0))==1 &&
        hdr.deleted.size()==2*(300-hdr.m_RawWorkspaceThumbnailTextures.size()) && Trim(hdr)==0,
        "HDR pairs must count both textures, preserve navigation leases, and settle")&&passed;
    auto tied=Owner();
    std::vector<std::pair<std::uint64_t,unsigned>> expectedOrder;
    for(auto& [key,entry]:tied.m_RawWorkspaceThumbnailTextures) {
        entry.lastUseSerial/=3;expectedOrder.emplace_back(entry.lastUseSerial,entry.texture);
    }
    std::sort(expectedOrder.begin(),expectedOrder.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    Trim(tied);
    for(std::size_t i=0;i<tied.deleted.size();++i)
        passed=Check(tied.deleted[i]==expectedOrder[i].second,"Equal-serial sort ordering changed")&&passed;
    if(!passed)return 1;
    std::cout<<"Gallery thumbnail trim ownership, protected/released LRU and allocation checks passed.\n";
}
