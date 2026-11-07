#include "Storage/ScratchBudget.h"
#include "BurstEngine/ScratchDirectory.h"
#include <fstream>
#include <iostream>
#include <memory>

namespace Stack::Validation {
int BracketScratchChild(const std::filesystem::path& root,HANDLE ready) {
    BurstEngine::ScratchDirectory scratch(root,BurstEngine::ScratchKind::Worker);
    std::ofstream(scratch.Path()/"tile.bin")<<"disposable";
    SetEvent(ready);
    Sleep(INFINITE);return 0;
}
bool ValidateBracketScratch() {
    using namespace BurstEngine;
    const auto root=std::filesystem::temp_directory_path()/("stack-scratch-check-"+UniqueId());
    bool ok=true;
    const auto check=[&](bool value,const char* message){if(!value){ok=false;std::cerr<<message<<'\n';}};
    std::filesystem::create_directories(root/"BurstSessions"/"unknown");
    std::ofstream(root/"BurstSessions"/"unknown"/"keep")<<"not owned";
    const auto unmarked=root/"BurstSessions"/UniqueId();
    std::filesystem::create_directories(unmarked);
    std::ofstream(unmarked/"original.raw")<<"not marked for deletion";
    std::filesystem::path released;
    {
        ScratchDirectory live(root,ScratchKind::Sources);released=live.Path();
        std::ofstream(live.Path()/"source-copy")<<"temporary copy";
        check(RecoverAbandonedScratch(root)==0 && std::filesystem::exists(released/"source-copy"),"Cleanup touched a live source session.");
    }
    check(!std::filesystem::exists(released),"Normal release left its working files.");
    Handle inheritedCopy;
    {
        ScratchDirectory live(root,ScratchKind::Sources);released=live.Path();
        HANDLE copy=nullptr;
        check(DuplicateHandle(GetCurrentProcess(),live.LockHandle(),GetCurrentProcess(),&copy,0,TRUE,DUPLICATE_SAME_ACCESS)!=0,
            "Could not duplicate worker ownership.");
        inheritedCopy.Reset(copy);
    }
    check(std::filesystem::exists(released) && RecoverAbandonedScratch(root)==0,
        "Parent release bypassed the worker's remaining lock reference.");
    inheritedCopy.Reset();
    check(RecoverAbandonedScratch(root)==1,"Final lock release did not permit recovery.");
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    Handle ready(CreateEventW(&security,TRUE,FALSE,nullptr));
    std::wstring executable(32768,L'\0');
    executable.resize(GetModuleFileNameW(nullptr,executable.data(),DWORD(executable.size())));
    std::wstring command=L"\""+executable+L"\" --validate-bracket-scratch-child \""+root.wstring()+L"\" "+
        std::to_wstring(reinterpret_cast<std::uintptr_t>(ready.Get()));
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    auto sharedOwner=std::make_unique<ScratchDirectory>(root,ScratchKind::Worker);
    const auto sharedPath=sharedOwner->Path();
    if(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child)) {
        Handle process(child.hProcess),thread(child.hThread);
        SetHandleInformation(sharedOwner->LockHandle(),HANDLE_FLAG_INHERIT,0);
        sharedOwner.reset(); // Child alone now holds the inherited ownership handle.
        const bool started=WaitForSingleObject(ready.Get(),10000)==WAIT_OBJECT_0;
        check(started,"Scratch child did not become ready.");
        if(started)check(RecoverAbandonedScratch(root)==0,"Cleanup touched another process's live session.");
        if(started)check(std::filesystem::exists(sharedPath),"Parent exit lost inherited worker protection.");
        TerminateProcess(process.Get(),ERROR_CANCELLED);WaitForSingleObject(process.Get(),5000);
        if(started)check(RecoverAbandonedScratch(root)==2,"Interrupted sessions were not recovered.");
    } else check(false,"Could not start scratch validation child.");
    check(std::filesystem::exists(root/"BurstSessions"/"unknown"/"keep"),"Unknown data was removed.");
    check(std::filesystem::exists(unmarked/"original.raw"),"An unmarked session-shaped folder was removed.");
    check(RecoverAbandonedScratch(root)==0,"Repeated recovery was not idle.");
    check(Storage::ScratchBudget::SetLimit(root,4),"Could not set the scratch allowance.");
    {
        ScratchDirectory budgeted(root,ScratchKind::Sources);
        const auto file=budgeted.Path()/"quota-check.bin";
        check(Storage::ScratchBudget::ReserveFile(file,3*Storage::ScratchBudget::GiB),"Initial scratch reservation failed.");
        check(!Storage::ScratchBudget::ReserveFile(file,2*Storage::ScratchBudget::GiB),"Concurrent writes exceeded the total allowance.");
        constexpr char payload[]="small actual write";
        Storage::ScratchBudget::Output output(file,std::ios::binary);output<<payload;output.close();
        check(bool(output)&&Storage::ScratchBudget::Reserved(budgeted.Path())==3*Storage::ScratchBudget::GiB+sizeof(payload)-1,
            "Streaming admission lost an earlier reservation or retained unused chunk headroom.");
        Storage::ScratchBudget::RemoveTransferFile(file);
        check(!std::filesystem::exists(file) &&
            Storage::ScratchBudget::Reserved(budgeted.Path())==3*Storage::ScratchBudget::GiB,
            "Consumed transfer bytes were not returned, or unrelated reservations changed.");
    }
    check(Storage::ScratchBudget::Total(root)==0,"Session release did not release the write allowance.");
    std::error_code ec;std::filesystem::remove_all(root,ec);
    std::cout<<"Bracket scratch ownership, process interruption recovery, unknown-folder protection: "<<(ok?"passed":"failed")<<'\n';
    return ok;
}
}
