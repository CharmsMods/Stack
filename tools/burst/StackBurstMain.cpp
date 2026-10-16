#include "BurstEngine/Worker.h"
#include <windows.h>
#include <cstdio>
#include <string>
int wmain(int argc,wchar_t** argv){
    // GUI subsystem prevents a console flashing during Explorer/Start launches.
    // Preserve redirected CLI streams, or attach to an existing terminal.
    if(argc>1&&std::wstring(argv[1])!=L"--worker"&&std::wstring(argv[1])!=L"--selection"&&std::wstring(argv[1])!=L"--files"){
        const auto output=GetStdHandle(STD_OUTPUT_HANDLE);
        if((!output||output==INVALID_HANDLE_VALUE)&&AttachConsole(ATTACH_PARENT_PROCESS)){
            FILE* stream=nullptr;freopen_s(&stream,"CONOUT$","w",stdout);freopen_s(&stream,"CONOUT$","w",stderr);
        }
    }
    return Stack::BurstEngine::WorkerMain(argc,argv);
}
