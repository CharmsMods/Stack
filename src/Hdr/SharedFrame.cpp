#ifdef _WIN32
#include "SharedFrame.h"
#include "Renderer/GLHelpers.h"
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstring>
#include <utility>

namespace Stack::Hdr {
namespace {
using Microsoft::WRL::ComPtr;
constexpr GLenum D3d11Image = 0x958B, DedicatedMemory = 0x9581, DeviceLuid = 0x9599;
constexpr GLuint64 GlKey = 0, D3dKey = 1;
template<class T> T Proc(const char* name) {
    return reinterpret_cast<T>(glfwGetProcAddress(name));
}
void ClearErrors() { while(glGetError()!=GL_NO_ERROR) {} }
bool SameAdapter(ID3D11Device* device) {
    if(!glfwExtensionSupported("GL_EXT_memory_object_win32")) return true;
    const auto getLuid=Proc<void (APIENTRY*)(GLenum,GLubyte*)>("glGetUnsignedBytevEXT");
    if(!getLuid) return false;
    ComPtr<IDXGIDevice> dxgi; ComPtr<IDXGIAdapter> adapter; DXGI_ADAPTER_DESC desc{};
    if(FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi))) || FAILED(dxgi->GetAdapter(&adapter)) ||
        FAILED(adapter->GetDesc(&desc))) return false;
    GLubyte luid[8]{};
    ClearErrors(); getLuid(DeviceLuid,luid);
    return glGetError()==GL_NO_ERROR && std::memcmp(luid,&desc.AdapterLuid,sizeof(luid))==0;
}
}
struct SharedFrame::State {
    ComPtr<ID3D11Texture2D> shared;
    GLuint texture=0,fbo=0,memory=0;
    ComPtr<IDXGIKeyedMutex> keyed;
    GLboolean (APIENTRY *acquireKey)(GLuint,GLuint64,GLuint)=nullptr;
    GLboolean (APIENTRY *releaseKey)(GLuint,GLuint64)=nullptr;
    void (APIENTRY *deleteMemory)(GLsizei,const GLuint*)=nullptr;
    HANDLE (WINAPI *openNv)(void*)=nullptr;
    BOOL (WINAPI *closeNv)(HANDLE)=nullptr;
    HANDLE (WINAPI *registerNv)(HANDLE,void*,GLuint,GLenum,GLenum)=nullptr;
    BOOL (WINAPI *unregisterNv)(HANDLE,HANDLE)=nullptr;
    BOOL (WINAPI *lockNv)(HANDLE,GLint,HANDLE*)=nullptr;
    BOOL (WINAPI *unlockNv)(HANDLE,GLint,HANDLE*)=nullptr;
    HANDLE nvDevice=nullptr,nvObject=nullptr;
    bool glOwns=false,pendingCopy=false;
    ULONGLONG stalledAt=0;

    Result Busy() {
        const auto now=GetTickCount64();
        if(!stalledAt) stalledAt=now;
        return now-stalledAt<1000 ? Result::Pending : Result::Failed;
    }
    ~State() {
        if(glOwns && nvObject) unlockNv(nvDevice,1,&nvObject);
        if(glOwns && keyed && memory) releaseKey(memory,GlKey);
        if(fbo) glDeleteFramebuffers(1,&fbo);
        if(nvObject) unregisterNv(nvDevice,nvObject);
        if(texture) glDeleteTextures(1,&texture);
        if(memory) deleteMemory(1,&memory);
        if(nvDevice) closeNv(nvDevice);
    }
    bool OpenNv(ID3D11Device* device,D3D11_TEXTURE2D_DESC desc) {
        if(!glfwExtensionSupported("WGL_NV_DX_interop2")) return false;
        openNv=Proc<decltype(openNv)>("wglDXOpenDeviceNV");
        closeNv=Proc<decltype(closeNv)>("wglDXCloseDeviceNV");
        registerNv=Proc<decltype(registerNv)>("wglDXRegisterObjectNV");
        unregisterNv=Proc<decltype(unregisterNv)>("wglDXUnregisterObjectNV");
        lockNv=Proc<decltype(lockNv)>("wglDXLockObjectsNV");
        unlockNv=Proc<decltype(unlockNv)>("wglDXUnlockObjectsNV");
        if(!openNv || !closeNv || !registerNv || !unregisterNv || !lockNv || !unlockNv) return false;
        nvDevice=openNv(device);
        if(!nvDevice || FAILED(device->CreateTexture2D(&desc,nullptr,&shared))) return false;
        glGenTextures(1,&texture);
        nvObject=registerNv(nvDevice,shared.Get(),texture,GL_TEXTURE_2D,2 /* WGL_ACCESS_WRITE_DISCARD_NV */);
        if(!nvObject || !lockNv(nvDevice,1,&nvObject)) return false;
        glOwns=true; fbo=GLHelpers::CreateFBO(texture);
        if(!unlockNv(nvDevice,1,&nvObject)) return false;
        glOwns=false;
        return fbo!=0;
    }
    bool OpenExt(ID3D11Device* device,D3D11_TEXTURE2D_DESC desc) {
        if(!glfwExtensionSupported("GL_EXT_memory_object") || !glfwExtensionSupported("GL_EXT_memory_object_win32") ||
            !glfwExtensionSupported("GL_EXT_win32_keyed_mutex")) return false;
        auto create=Proc<void (APIENTRY*)(GLsizei,GLuint*)>("glCreateMemoryObjectsEXT");
        auto setParam=Proc<void (APIENTRY*)(GLuint,GLenum,const GLint*)>("glMemoryObjectParameterivEXT");
        auto import=Proc<void (APIENTRY*)(GLuint,GLuint64,GLenum,void*)>("glImportMemoryWin32HandleEXT");
        auto storage=Proc<void (APIENTRY*)(GLenum,GLsizei,GLenum,GLsizei,GLsizei,GLuint,GLuint64)>("glTexStorageMem2DEXT");
        deleteMemory=Proc<decltype(deleteMemory)>("glDeleteMemoryObjectsEXT");
        acquireKey=Proc<decltype(acquireKey)>("glAcquireKeyedMutexWin32EXT");
        releaseKey=Proc<decltype(releaseKey)>("glReleaseKeyedMutexWin32EXT");
        if(!create || !setParam || !import || !storage || !deleteMemory || !acquireKey || !releaseKey) return false;
        desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&shared)) || FAILED(shared.As(&keyed))) return false;
        ComPtr<IDXGIResource1> resource;
        HANDLE ntHandle=nullptr;
        if(FAILED(shared.As(&resource)) || FAILED(resource->CreateSharedHandle(nullptr,
            DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,nullptr,&ntHandle))) return false;
        ClearErrors();
        create(1,&memory);
        const GLint dedicated=GL_TRUE;
        setParam(memory,DedicatedMemory,&dedicated);
        // A D3D11 image owns the entire memory object. The Win32 spec ignores
        // size for this handle type and recommends zero for older drivers.
        import(memory,0,D3d11Image,ntHandle);
        CloseHandle(ntHandle); // GL imports a reference, not ownership of the NT handle.
        if(!memory || glGetError()!=GL_NO_ERROR) return false;
        GLint prior=0; glGetIntegerv(GL_TEXTURE_BINDING_2D,&prior);
        glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
        storage(GL_TEXTURE_2D,1,GL_RGBA16F,desc.Width,desc.Height,memory,0);
        glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(prior));
        if(!texture || glGetError()!=GL_NO_ERROR || !acquireKey(memory,GlKey,0)) return false;
        glOwns=true; fbo=GLHelpers::CreateFBO(texture);
        if(!releaseKey(memory,GlKey)) return false;
        glOwns=false; glFlush();
        return fbo!=0;
    }
};

SharedFrame::SharedFrame(std::unique_ptr<State> state):state_(std::move(state)) {}
SharedFrame::~SharedFrame()=default;
std::unique_ptr<SharedFrame> SharedFrame::Open(ID3D11Device* device,ID3D11Texture2D* destination) {
    if(!SameAdapter(device)) return {};
    D3D11_TEXTURE2D_DESC desc{}; destination->GetDesc(&desc);
    desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE; desc.MiscFlags=0;
    auto state=std::make_unique<State>();
    if(!state->OpenNv(device,desc)) {
        state=std::make_unique<State>();
        if(!state->OpenExt(device,desc)) return {};
    }
    return std::unique_ptr<SharedFrame>(new SharedFrame(std::move(state)));
}
SharedFrame::Result SharedFrame::Begin(unsigned& framebuffer) {
    auto& s=*state_;
    framebuffer=0;
    if(s.pendingCopy) return Result::Ready;
    if(s.nvObject) {
        if(!s.lockNv(s.nvDevice,1,&s.nvObject)) return Result::Failed;
    } else if(!s.acquireKey(s.memory,GlKey,0)) {
        return glGetError()==GL_NO_ERROR ? s.Busy() : Result::Failed;
    }
    s.glOwns=true; s.stalledAt=0; framebuffer=s.fbo;
    return Result::Ready;
}
SharedFrame::Result SharedFrame::CopyTo(ID3D11DeviceContext* context,ID3D11Texture2D* dst) {
    auto& s=*state_;
    if(s.nvObject) {
        if(!s.unlockNv(s.nvDevice,1,&s.nvObject)) return Result::Failed;
        s.glOwns=false;
        context->CopyResource(dst,s.shared.Get());
        return Result::Ready;
    }
    if(s.glOwns) {
        if(!s.releaseKey(s.memory,D3dKey)) return Result::Failed;
        s.glOwns=false; s.pendingCopy=true;
        glFlush();
    }
    const HRESULT acquired=s.keyed->AcquireSync(D3dKey,0);
    if(acquired==WAIT_TIMEOUT) return s.Busy();
    if(acquired!=S_OK) return Result::Failed;
    context->CopyResource(dst,s.shared.Get());
    if(FAILED(s.keyed->ReleaseSync(GlKey))) return Result::Failed;
    context->Flush();
    s.pendingCopy=false; s.stalledAt=0;
    return Result::Ready;
}
const char* SharedFrame::Status() const {
    return state_->nvObject ? "HDR active (WGL GPU sharing)" : "HDR active (EXT GPU sharing)";
}
}
#endif
