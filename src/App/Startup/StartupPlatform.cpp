#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "StartupPlatform.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif
namespace Stack::Startup {
DesktopImage WindowPresentation::Prepare(GLFWwindow* window) {
    DesktopImage image;
    window_=window;
#if defined(_WIN32)
    HWND hwnd=glfwGetWin32Window(window);
    HMONITOR monitor=MonitorFromWindow(hwnd,MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)};
    if(!GetMonitorInfoW(monitor,&info)) return image;
    monitor_=reinterpret_cast<std::intptr_t>(monitor);
    left_=info.rcMonitor.left; top_=info.rcMonitor.top;
    width_=info.rcMonitor.right-left_; height_=info.rcMonitor.bottom-top_;
    RECT rect{}; GetWindowRect(hwnd,&rect);
    restoreX_=rect.left; restoreY_=rect.top;
    restoreW_=rect.right-rect.left; restoreH_=rect.bottom-rect.top;
    style_=GetWindowLongPtrW(hwnd,GWL_STYLE);
    // Capture before making the window visible. A memory DIB never goes to disk.
    const float scale=std::min(1.f,3840.f/std::max(width_,height_));
    image.width=std::max(1,static_cast<int>(width_*scale));
    image.height=std::max(1,static_cast<int>(height_*scale));
    HDC screen=GetDC(nullptr), memory=screen?CreateCompatibleDC(screen):nullptr;
    BITMAPINFO bitmap{}; bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth=image.width; bitmap.bmiHeader.biHeight=-image.height;
    bitmap.bmiHeader.biPlanes=1; bitmap.bmiHeader.biBitCount=32; bitmap.bmiHeader.biCompression=BI_RGB;
    void* pixels=nullptr;
    HBITMAP dib=memory?CreateDIBSection(screen,&bitmap,DIB_RGB_COLORS,&pixels,nullptr,0):nullptr;
    if(dib) {
        auto previous=SelectObject(memory,dib);
        SetStretchBltMode(memory,HALFTONE);
        if(StretchBlt(memory,0,0,image.width,image.height,screen,left_,top_,width_,height_,SRCCOPY|CAPTUREBLT)) {
            GdiFlush();
            const auto* source=static_cast<const unsigned char*>(pixels);
            image.rgba.resize(static_cast<size_t>(image.width)*image.height*4);
            for(size_t i=0;i<image.rgba.size();i+=4) {
                image.rgba[i]=source[i+2]; image.rgba[i+1]=source[i+1];
                image.rgba[i+2]=source[i]; image.rgba[i+3]=255;
            }
        }
        SelectObject(memory,previous); DeleteObject(dib);
    }
    if(memory) DeleteDC(memory);
    if(screen) ReleaseDC(nullptr,screen);
    SetWindowLongPtrW(hwnd,GWL_STYLE,style_ & ~(WS_CAPTION|WS_THICKFRAME|WS_MAXIMIZE));
    SetWindowPos(hwnd,nullptr,left_,top_,width_,height_,SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    active_=true;
#endif
    return image;
}
void WindowPresentation::Show() { if(window_) glfwShowWindow(window_); }
void WindowPresentation::Finish() {
    if(!active_) return;
#if defined(_WIN32)
    HWND hwnd=glfwGetWin32Window(window_);
    const bool visible=IsWindowVisible(hwnd)!=FALSE;
    if(DisplayChanged()) {
        MONITORINFO info{sizeof(info)};
        if(GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&info)) {
            restoreW_=std::min(restoreW_,int(info.rcWork.right-info.rcWork.left));
            restoreH_=std::min(restoreH_,int(info.rcWork.bottom-info.rcWork.top));
            restoreX_=info.rcWork.left;restoreY_=info.rcWork.top;
        }
    }
    // Commit restored chrome and maximized placement without showing a normal-sized intermediate.
    SendMessageW(hwnd,WM_SETREDRAW,FALSE,0);
    SetWindowLongPtrW(hwnd,GWL_STYLE,style_);
    WINDOWPLACEMENT placement{sizeof(placement)};
    GetWindowPlacement(hwnd,&placement);
    placement.rcNormalPosition={restoreX_,restoreY_,restoreX_+restoreW_,restoreY_+restoreH_};
    placement.showCmd=visible?SW_SHOWMAXIMIZED:SW_HIDE;
    SetWindowPlacement(hwnd,&placement);
    SetWindowPos(hwnd,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    SendMessageW(hwnd,WM_SETREDRAW,TRUE,0);
    if(!visible) ShowWindow(hwnd,SW_HIDE);
#endif
    active_=false;
}
bool WindowPresentation::DisplayChanged() const {
#if defined(_WIN32)
    if(active_) {
        MONITORINFO info{sizeof(info)};
        return !GetMonitorInfoW(reinterpret_cast<HMONITOR>(monitor_),&info) ||
            info.rcMonitor.left!=left_ || info.rcMonitor.top!=top_ ||
            info.rcMonitor.right-info.rcMonitor.left!=width_ || info.rcMonitor.bottom-info.rcMonitor.top!=height_;
    }
#endif
    return false;
}
}
