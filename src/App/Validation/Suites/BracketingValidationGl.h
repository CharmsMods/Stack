#pragma once
#include "Renderer/GLLoader.h"
#include "Raw/OpenGlTask.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <mutex>

namespace Stack::Validation {
// CLI validation owns a hidden context. Production always uses its supplied
// render service instead; no window creation belongs in the depth engine.
struct BracketingValidationGl {
    GLFWwindow* window=nullptr;
    mutable std::recursive_mutex contextMutex;
    BracketingValidationGl(const BracketingValidationGl&)=delete;
    BracketingValidationGl& operator=(const BracketingValidationGl&)=delete;
    BracketingValidationGl(){
        if(!glfwInit())throw std::runtime_error("Could not initialize validation OpenGL.");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        window=glfwCreateWindow(64,64,"Bracket compute validation",nullptr,nullptr);
        if(!window){glfwTerminate();throw std::runtime_error("Validation requires OpenGL 4.3.");}
        const auto previous=glfwGetCurrentContext();
        glfwMakeContextCurrent(window);
        if(!LoadGLFunctions()){glfwMakeContextCurrent(previous);glfwDestroyWindow(window);glfwTerminate();throw std::runtime_error("Could not load validation OpenGL.");}
        // Worker callbacks may arrive on different threads. Leave this context
        // detached until the serialized executor explicitly acquires it.
        glfwMakeContextCurrent(previous);
    }
    ~BracketingValidationGl(){glfwDestroyWindow(window);glfwTerminate();}
    Raw::OpenGlTaskExecutor Executor()const {
        // The host outlives every request/result and their GPU cleanup callbacks.
        // Recursive acquisition also permits synchronous nested GPU tasks.
        return [this](Raw::OpenGlTask task,std::string& error) {
            if(!task) {error="Validation received an empty OpenGL task.";return false;}
            std::lock_guard<std::recursive_mutex> lock(contextMutex);
            const auto previous=glfwGetCurrentContext();
            struct RestoreContext {
                GLFWwindow* previous;
                ~RestoreContext(){glfwMakeContextCurrent(previous);}
            } restore{previous};
            glfwMakeContextCurrent(window);
            if(glfwGetCurrentContext()!=window) {
                error="Could not acquire the validation OpenGL context.";return false;
            }
            return task(error);
        };
    }
};
}
