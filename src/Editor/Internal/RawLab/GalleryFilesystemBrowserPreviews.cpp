#include "GalleryFilesystemBrowserState.h"
#include "GalleryBrowserImageTypes.h"
#include "Async/TaskSystem.h"
#include "Renderer/GLHelpers.h"
#include "Raw/RawCameraThumbnail.h"
#include <algorithm>
#include <cwctype>
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

namespace Stack::Editor::RawLabInternal {
namespace {
struct Bitmap {
    HBITMAP value=nullptr;
    ~Bitmap(){if(value)DeleteObject(value);}
};
struct Apartment {
    HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
};
bool ShellPreview(const std::filesystem::path& path,std::vector<unsigned char>& pixels,int& width,int& height) {
    Microsoft::WRL::ComPtr<IShellItemImageFactory> factory;
    if(FAILED(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&factory))))return false;
    Bitmap bitmap;
    if(FAILED(factory->GetImage({384,384},SIIGBF_THUMBNAILONLY,&bitmap.value)))return false;
    BITMAP details{};
    if(!GetObjectW(bitmap.value,sizeof(details),&details) || details.bmWidth<=0 || details.bmHeight<=0 ||
        details.bmWidth>512 || details.bmHeight>512)return false;
    width=details.bmWidth;height=details.bmHeight;
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    pixels.resize(static_cast<std::size_t>(width)*height*4);
    HDC dc=CreateCompatibleDC(nullptr);
    if(!dc){pixels.clear();return false;}
    const int lines=GetDIBits(dc,bitmap.value,0,height,pixels.data(),&info,DIB_RGB_COLORS);
    DeleteDC(dc);
    if(lines!=height){pixels.clear();return false;}
    bool hasAlpha=false;
    for(std::size_t i=3;i<pixels.size();i+=4)hasAlpha|=pixels[i]!=0;
    for(std::size_t i=0;i<pixels.size();i+=4) {
        std::swap(pixels[i],pixels[i+2]);
        if(!hasAlpha)pixels[i+3]=255;
        else if(pixels[i+3]>0 && pixels[i+3]<255)
            for(int c=0;c<3;++c)pixels[i+c]=static_cast<unsigned char>(std::min(255u,unsigned(pixels[i+c])*255/pixels[i+3]));
    }
    return true;
}

}
void GalleryFilesystemBrowser::State::RequestPreview(const std::shared_ptr<State>& self,const std::shared_ptr<Node>& node) {
    if(closed.load() || node->previewRequested || previewPending>=2)return;
    // At most 96 small textures; never evict a texture referenced by this or the previous frame.
    if(previewNodes.size()+previewPending>=96) {
        if(motion.Active())return; // frozen outgoing surface retains these texture handles
        auto oldest=std::min_element(previewNodes.begin(),previewNodes.end(),[](const auto& a,const auto& b){return a->previewFrame<b->previewFrame;});
        if((*oldest)->previewFrame>=ImGui::GetFrameCount()-1)return;
        auto& retired=**oldest;
        if(retired.texture)glDeleteTextures(1,&retired.texture);
        retired.texture=0;retired.previewRequested=false;
        previewNodes.erase(oldest);
    }
    node->previewRequested=true;
    const auto path=node->path;const bool image=node->kind==Kind::Image;
    Async::ActivityMetadata activity;activity.label="Folder thumbnail";
    if(!Async::TaskSystem::Get().SubmitThumbnail(std::move(activity),
            std::shared_ptr<std::atomic<bool>>(self,&self->closed),[self,node,path,image] {
        Preview result;result.node=node;
        try {
            Apartment apartment;
            if(!self->closed.load() && (SUCCEEDED(apartment.result) || apartment.result==RPC_E_CHANGED_MODE)) {
                if(!ShellPreview(path,result.pixels,result.width,result.height) && image) {
                    std::error_code error;const auto bytes=std::filesystem::file_size(path,error);
                    if(!error) {
                        const auto canceled=[self]{return self->closed.load();};
                        const auto camera=RawWorkspace::FindCameraThumbnail(path,bytes,384,canceled);
                        RawWorkspace::DecodeCameraThumbnail(path,camera,384,result.pixels,result.width,result.height,canceled);
                    }
                } else if(result.pixels.empty() && !image) {
                    std::error_code error;int inspected=0,attempted=0;
                    std::filesystem::directory_iterator it(path,error),end;
                    for(;!error && it!=end && inspected<256 && attempted<4 && !self->closed.load();it.increment(error),++inspected) {
                        if(BrowserImagePath(it->path())) {
                            ++attempted;
                            const auto& source=it->path();
                            if(ShellPreview(source,result.pixels,result.width,result.height))break;
                            // Camera previews do not depend on an installed Windows RAW codec.
                            // Reuse the bounded embedded-JPEG reader, never develop sensor samples.
                            const auto canceled=[self]{return self->closed.load();};
                            std::error_code sizeError;
                            const auto bytes=it->file_size(sizeError);
                            if(!sizeError) {
                                const auto camera=RawWorkspace::FindCameraThumbnail(source,bytes,192,canceled);
                                if(RawWorkspace::DecodeCameraThumbnail(source,camera,192,result.pixels,result.width,result.height,canceled))break;
                            }
                        }
                    }
                }
            }
        } catch(const std::exception&) {result.pixels.clear();}
        if(self->closed.load())return;
        {std::lock_guard lock(self->mutex);self->previews.push_back(std::move(result));}
        self->previewReady.store(true);
    })) {node->previewRequested=false;return;}
    ++previewPending;
}
void GalleryFilesystemBrowser::State::PumpPreviews() {
    if(!previewReady.exchange(false))return;
    std::vector<Preview> completed;
    {std::lock_guard lock(mutex);completed.swap(previews);}
    for(auto& result:completed) {
        --previewPending;
        if(closed.load() || result.pixels.empty())continue;
        // These are thumbnail publication uploads, never idle-frame uploads.
        result.node->texture=GLHelpers::CreateTextureFromPixels(result.pixels.data(),result.width,result.height,4);
        result.node->previewWidth=result.width;result.node->previewHeight=result.height;
        if(result.node->texture)previewNodes.push_back(result.node);
    }
}
void GalleryFilesystemBrowser::State::ReleasePreviews() {
    // Called only by the browser on its UI/GL owner, never by a worker or State's destructor.
    for(auto& node:previewNodes) {
        if(node->texture)glDeleteTextures(1,&node->texture);
        node->texture=0;node->previewRequested=false;
    }
    previewNodes.clear();
}
}
