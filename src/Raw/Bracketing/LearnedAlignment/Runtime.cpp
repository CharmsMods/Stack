#include "Internal.h"
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Raw::Bracketing::LearnedAlignment {
#ifdef _WIN32
namespace {
struct Handle {
    HANDLE value=nullptr;
    ~Handle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
std::wstring Quote(const std::wstring& value) {
    std::wstring out=L"\"";unsigned slashes=0;
    for(const auto c:value) {
        if(c==L'\\'){++slashes;continue;}
        out.append(slashes*(c==L'"'?2:1)+(c==L'"'?1:0),L'\\');slashes=0;out+=c;
    }
    out.append(slashes*2,L'\\');return out+L'"';
}
}
#endif
bool RunModel(const ProcessingRequest& request,const std::filesystem::path& jobPath,std::string& error) {
#ifdef _WIN32
    std::wstring module(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,module.data(),DWORD(module.size()));
    if(!length||length>=module.size()){error="Could not locate the AI alignment runtime.";return false;}
    module.resize(length);
    const auto root=std::filesystem::path(module).parent_path()/"Stack"/"Models"/"SeaRaft";
    const auto manifest=root/"runtime.json";
    if(!std::filesystem::exists(manifest)||std::filesystem::file_size(manifest)>16384){
        error="The optional SEA-RAFT model package is not installed for this copy of Stack Burst. Install the package or turn AI-assisted alignment off.";return false;
    }
    std::ifstream input(manifest);const auto config=nlohmann::json::parse(input);
    if(config.value("version",0)!=1||config.value("profile",std::string())!=Profile){error="The SEA-RAFT runtime package is incompatible. Rebuild the optional package for the CUDA-only runtime or turn AI-assisted alignment off.";return false;}
    auto python=std::filesystem::u8path(config.value("python",std::string("python/python.exe")));
    if(python.is_relative())python=root/python;
    const auto script=root/"worker.py";
    if(!std::filesystem::is_regular_file(python)||!std::filesystem::is_regular_file(script)){
        error="The optional SEA-RAFT runtime is incomplete. Repair its package or turn AI-assisted alignment off.";return false;
    }
    if(request.shouldCancel&&request.shouldCancel()){error="Canceled";return false;}
    // A caller may reuse its owned cache directory after a failed request.
    // Never accept a preceding process's response as this invocation's result.
    const auto response=jobPath.parent_path()/"result.json";
    std::filesystem::remove(response);
    Handle job;job.value=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    // The engine's small prepared metadata/proxy working set remains outside
    // this process. The model exits before native search/merge allocation.
    limits.ProcessMemoryLimit=SIZE_T(request.memoryBudgetBytes*3/4);
    if(!job.value||!SetInformationJobObject(job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){
        error="Could not bound the AI alignment process.";return false;
    }
    auto command=Quote(python.wstring())+L" -I "+Quote(script.wstring())+L" "+Quote(jobPath.wstring());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(python.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,
        nullptr,root.c_str(),&startup,&process)){error="Could not start the optional SEA-RAFT runtime.";return false;}
    Handle child,thread;child.value=process.hProcess;thread.value=process.hThread;
    if(!AssignProcessToJobObject(job.value,child.value)||ResumeThread(thread.value)==DWORD(-1)){
        TerminateProcess(child.value,1);WaitForSingleObject(child.value,INFINITE);
        error="Could not attach the AI alignment process to this job.";return false;
    }
    for(;;) {
        const auto wait=WaitForSingleObject(child.value,100);
        if(wait==WAIT_OBJECT_0)break;
        if(wait==WAIT_FAILED||(request.shouldCancel&&request.shouldCancel())) {
            TerminateJobObject(job.value,1);WaitForSingleObject(child.value,INFINITE);
            error=wait==WAIT_FAILED?"Could not wait for AI alignment.":"Canceled";return false;
        }
    }
    DWORD code=1;GetExitCodeProcess(child.value,&code);
    // The adapter writes a bounded error response for ordinary model failures.
    if(code){
        error="SEA-RAFT failed. Its runtime may be missing a dependency or have exceeded the job's memory allowance. Turn AI alignment off or repair its package.";
        if(std::filesystem::exists(response)&&std::filesystem::file_size(response)<=1024*1024) {
            std::ifstream failure(response);const auto value=nlohmann::json::parse(failure,nullptr,false);
            if(value.is_object()&&value.contains("error")&&value["error"].is_string())error="AI alignment: "+value["error"].get<std::string>();
        }
        return false;
    }
    return true;
#else
    error="The optional SEA-RAFT runtime is available on Windows.";return false;
#endif
}
}
