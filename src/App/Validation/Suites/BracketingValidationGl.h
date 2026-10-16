#pragma once
#include "Renderer/GLLoader.h"
#include "Raw/OpenGlTask.h"
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace Stack::Validation {
// CLI validation owns a hidden context. Production always uses its supplied
// render service instead; no window creation belongs in the depth engine.
struct BracketingValidationGl {
    GLFWwindow* window=nullptr;
    BracketingValidationGl(){
        if(!glfwInit())throw std::runtime_error("Could not initialize validation OpenGL.");
        glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
        window=glfwCreateWindow(64,64,"Bracket compute validation",nullptr,nullptr);
        if(!window){glfwTerminate();throw std::runtime_error("Validation requires OpenGL 4.3.");}
        glfwMakeContextCurrent(window);
        if(!LoadGLFunctions()){glfwDestroyWindow(window);glfwTerminate();throw std::runtime_error("Could not load validation OpenGL.");}
    }
    ~BracketingValidationGl(){glfwDestroyWindow(window);glfwTerminate();}
    Raw::OpenGlTaskExecutor Executor()const{return [](Raw::OpenGlTask task,std::string& error){return task(error);};}
};
}
