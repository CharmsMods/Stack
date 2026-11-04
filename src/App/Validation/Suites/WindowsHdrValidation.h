#pragma once
#include "Hdr/WindowPresenter.h"
#include "Hdr/Color.h"
#include <chrono>
#include <memory>
#include <thread>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
namespace Stack::Validation {
inline void CheckNativeHdrPresentation(GLFWwindow* window) {
    const auto paint=[](unsigned target,float value) {
        glBindFramebuffer(GL_FRAMEBUFFER,target);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(value,value,value,1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
    };
    const auto waitForFrame=[](Hdr::WindowPresenter& presenter,unsigned long long count) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(presenter.SubmittedFrames()<count && std::chrono::steady_clock::now()<deadline) {
            if(!presenter.Present()) throw std::runtime_error(presenter.Status());
            glfwPollEvents();std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        if(presenter.SubmittedFrames()<count) throw std::runtime_error("Windows HDR frame did not present.");
    };
    for(bool compatible:{false,true}) {
        Hdr::WindowPresenter presenter(glfwGetWin32Window(window),compatible);
        auto target=presenter.Begin(32,32,false);
        if(!target) {
            if(!presenter.Display().hdr) {
                std::cout<<"No active HDR output: native presentation skipped.\n";return;
            }
            throw std::runtime_error(presenter.Status());
        }
        paint(target,1.5f);waitForFrame(presenter,1);
        if(compatible) {
            for(int i=0;i<8;++i) {presenter.Present();glFinish();}
            if(presenter.SubmittedFrames()!=1) throw std::runtime_error("An unchanged HDR frame was transferred again.");
            paint(target,2.f);waitForFrame(presenter,2);
        }
        const auto next=presenter.SubmittedFrames()+1;
        target=presenter.Begin(40,24,false);
        if(!target) throw std::runtime_error(presenter.Status());
        paint(target,1.75f);waitForFrame(presenter,next);
        std::cout<<presenter.Status()<<", white "<<presenter.Display().white<<" nits, peak "<<presenter.Display().peak
            <<" nits, native frames "<<presenter.SubmittedFrames()<<"\n";
        if(compatible) {
            // Match the detached viewport's actual separate, shared GL context.
            // Keep that presenter alive while the first window switches to SDR.
            std::unique_ptr<GLFWwindow,decltype(&glfwDestroyWindow)> other(
                glfwCreateWindow(32,32,"HDR secondary window validation",nullptr,window),glfwDestroyWindow);
            if(!other) throw std::runtime_error("Cannot create the second HDR window.");
            int x=0,y=0;
            glfwGetWindowPos(window,&x,&y);
            glfwSetWindowPos(other.get(),x,y);
            struct RestoreContext {
                GLFWwindow* original;
                ~RestoreContext() {glfwMakeContextCurrent(original);}
            } restore{window};
            glfwMakeContextCurrent(other.get());
            Hdr::WindowPresenter detached(glfwGetWin32Window(other.get()),true);
            const auto otherTarget=detached.Begin(32,32,false);
            if(!otherTarget) throw std::runtime_error(detached.Status());
            paint(otherTarget,1.25f);waitForFrame(detached,1);
            unsigned sdrTarget=0;
            {
                RestoreContext returnToDetached{other.get()};
                glfwMakeContextCurrent(window);
                sdrTarget=presenter.Begin(40,24,true);
            }
            if(sdrTarget || !detached.Active() || detached.Begin(32,32,false)!=otherTarget)
                throw std::runtime_error("A window mode change disturbed the other HDR surface.");
            paint(otherTarget,1.75f);waitForFrame(detached,2);
            std::cout<<"Two independent native windows and live HDR resize passed.\n";
        } else if(presenter.Begin(40,24,true)) {
            throw std::runtime_error("SDR comparison retained the HDR target.");
        }
    }
}
}
#endif
