#include "GpuContext.h"
#include "Renderer/GLLoader.h"
#include <GLFW/glfw3.h>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
namespace Stack::BurstEngine {
// One context owner handles every GPU handoff, including calls originating on
// the processor's parallel CPU workers. No context/object crosses the IPC seam.
class GpuContext::Impl {
public:
    Impl(){
        std::promise<bool> initialized;auto ready=initialized.get_future();
        thread_=std::thread([this,signal=std::move(initialized)]() mutable {
            GLFWwindow* window=nullptr;
            const bool initialized=glfwInit()!=0;
            if(initialized){glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
                window=glfwCreateWindow(64,64,"Stack burst compute",nullptr,nullptr);}
            bool valid=false;
            if(window){glfwMakeContextCurrent(window);valid=LoadGLFunctions();}
            signal.set_value(valid);
            if(valid){
                std::unique_lock<std::mutex> lock(mutex_);
                for(;;){condition_.wait(lock,[&]{return stopping_||!queue_.empty();});
                    if(stopping_&&queue_.empty())break;
                    auto task=std::move(queue_.front());queue_.pop_front();lock.unlock();task();lock.lock();}
            }
            if(window){glfwMakeContextCurrent(nullptr);glfwDestroyWindow(window);}if(initialized)glfwTerminate();
        });available_=ready.get();
    }
    ~Impl(){ {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}condition_.notify_one();if(thread_.joinable())thread_.join(); }
    bool Available()const{return available_;}
    Raw::OpenGlTaskExecutor Executor(){
        if(!available_)return {};
        return [this](Raw::OpenGlTask task,std::string& error){
            auto promise=std::make_shared<std::promise<std::pair<bool,std::string>>>();auto future=promise->get_future();
            {std::lock_guard<std::mutex> lock(mutex_);if(stopping_){error="Burst GPU context is stopping.";return false;}
                queue_.push_back([task=std::move(task),promise]{std::string failure;bool success=false;
                    try{success=task(failure);}catch(const std::exception& e){failure=e.what();}catch(...){failure="Unknown burst GPU error.";}
                    promise->set_value({success,std::move(failure)});});}
            condition_.notify_one();auto result=future.get();error=std::move(result.second);return result.first;
        };
    }
private:
    bool available_=false,stopping_=false;std::thread thread_;std::mutex mutex_;std::condition_variable condition_;std::deque<std::function<void()>> queue_;
};
GpuContext::GpuContext():impl_(std::make_unique<Impl>()){}
GpuContext::~GpuContext()=default;
bool GpuContext::Available()const{return impl_->Available();}
Raw::OpenGlTaskExecutor GpuContext::Executor(){return impl_->Executor();}
}