#include "Window.h"
#include "WindowStyle.h"
#include "Batch.h"
#include "Protocol.h"
#include <commctrl.h>
#include <shellapi.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace Stack::BurstEngine {
namespace {
constexpr UINT Updated=WM_APP+1;
enum Control { Title=100, Summary, FormatLabel, Dng, Tiff, Jpeg, Png, LocationLabel,
    Beside, Folder, AiAlignment, Hint, Stage, Progress, Details, Reveal, Start, Close };
enum class Phase { Analyzing, Ready, Processing, Finished };
std::wstring Count(std::size_t n,const wchar_t* singular,const wchar_t* plural){return std::to_wstring(n)+L" "+(n==1?singular:plural);}
std::wstring Wide(const std::string& text){return TextPath(text).wstring();}
struct Snapshot {
    Phase phase=Phase::Analyzing;
    double fraction=0;
    std::wstring detail=L"Reading the selected images...";
    std::shared_ptr<SelectionResult> plan;
    std::shared_ptr<BatchResult> batch;
};
struct Session {
    HWND window=nullptr;
    std::atomic<bool> cancel{false};
    std::mutex mutex;
    Snapshot snapshot;
    bool pending=false;
    std::chrono::steady_clock::time_point lastNotice{};
    // One bounded mailbox; progress replaces the previous snapshot. No timer,
    // retained callback, rendering loop or work is scheduled while idle.
    void Publish(Snapshot value){std::lock_guard<std::mutex> lock(mutex);const bool terminal=value.phase==Phase::Ready||value.phase==Phase::Finished;
        snapshot=std::move(value);const auto now=std::chrono::steady_clock::now();
        if(!pending&&(terminal||now-lastNotice>=std::chrono::milliseconds(50))){pending=true;lastNotice=now;PostMessageW(window,Updated,0,0);}}
    Snapshot Take(){std::lock_guard<std::mutex> lock(mutex);pending=false;return snapshot;}
};
struct Window {
    HWND handle=nullptr;
    std::shared_ptr<Session> session=std::make_shared<Session>();
    Snapshot current;
    std::vector<std::filesystem::path> paths;
    HBRUSH background=CreateSolidBrush(WindowStyle::Background());
    HFONT font=nullptr,heading=nullptr;
    unsigned dpi=96;
    bool busy=true,closing=false;
    int Scale(int value)const{return MulDiv(value,int(dpi),96);}
    HWND Item(int id)const{return GetDlgItem(handle,id);}
    void Text(int id,const std::wstring& text){SetWindowTextW(Item(id),text.c_str());}
    void Place(int id,int x,int y,int width,int height){MoveWindow(Item(id),Scale(x),Scale(y),Scale(width),Scale(height),TRUE);}
    HWND Add(int id,const wchar_t* type,const wchar_t* text,DWORD style=0){
        return CreateWindowExW(0,type,text,
            WS_CHILD|WS_VISIBLE|style,0,0,0,0,handle,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);
    }
    void Layout(){
        if(font)DeleteObject(font);if(heading)DeleteObject(heading);
        font=CreateFontW(-Scale(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        heading=CreateFontW(-Scale(23),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for(int id=Title;id<=Close;++id)SendMessageW(Item(id),WM_SETFONT,reinterpret_cast<WPARAM>(id==Title?heading:font),TRUE);
        Place(Title,24,22,560,34);Place(Summary,24,62,560,24);
        Place(FormatLabel,24,106,560,20);
        Place(Dng,24,134,133,44);Place(Tiff,165,134,133,44);Place(Png,306,134,133,44);Place(Jpeg,447,134,133,44);
        Place(LocationLabel,24,200,560,20);Place(Beside,24,228,274,44);Place(Folder,306,228,274,44);
        Place(AiAlignment,24,282,556,26);
        Place(Hint,24,316,556,42);Place(Stage,24,368,556,38);Place(Progress,24,412,556,8);
        RECT client{};GetClientRect(handle,&client);const int footer=MulDiv(client.bottom,96,int(dpi))-58;
        Place(Details,32,438,540,std::max(40,footer-458));Place(Reveal,24,footer,180,36);Place(Start,286,footer,190,36);Place(Close,484,footer,96,36);
    }
    void Create(){
        dpi=GetDpiForWindow(handle);session->window=handle;
        Add(Title,L"STATIC",L"Burst denoise");Add(Summary,L"STATIC",L"Organizing your selection...");
        Add(FormatLabel,L"STATIC",L"Output format  ·  choose one or more");
        for(const auto& pair:{std::pair{Dng,L"DNG"},std::pair{Tiff,L"TIFF 16-bit"},std::pair{Png,L"PNG 16-bit"},std::pair{Jpeg,L"JPEG"}})
            Add(pair.first,L"BUTTON",pair.second,BS_AUTOCHECKBOX|BS_PUSHLIKE|WS_TABSTOP);
        SendMessageW(Item(Dng),BM_SETCHECK,BST_CHECKED,0);
        Add(LocationLabel,L"STATIC",L"Save outputs");
        Add(Beside,L"BUTTON",L"Beside originals",BS_AUTORADIOBUTTON|BS_PUSHLIKE|WS_TABSTOP|WS_GROUP);
        Add(Folder,L"BUTTON",L"New subfolder",BS_AUTORADIOBUTTON|BS_PUSHLIKE|WS_TABSTOP);
        SendMessageW(Item(Folder),BM_SETCHECK,BST_CHECKED,0);
        Add(AiAlignment,L"BUTTON",L"AI-assisted alignment (experimental)",BS_AUTOCHECKBOX|WS_TABSTOP);
        SendMessageW(Item(AiAlignment),BM_SETCHECK,BST_UNCHECKED,0);
        Add(Hint,L"STATIC",L"Full-resolution outputs in a new Stack Burst folder.\r\nDNG stays editable; TIFF, PNG and JPEG are finished images.");
        Add(Stage,L"STATIC",L"Reading selected images...");Add(Progress,PROGRESS_CLASSW,L"");
        SendMessageW(Item(Progress),PBM_SETRANGE32,0,1000);
        Add(Details,L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP);
        Add(Reveal,L"BUTTON",L"Show output folder",BS_PUSHBUTTON|WS_TABSTOP|WS_GROUP);
        Add(Start,L"BUTTON",L"Denoise sets",BS_DEFPUSHBUTTON|WS_TABSTOP);
        Add(Close,L"BUTTON",L"Cancel",BS_PUSHBUTTON|WS_TABSTOP);
        Layout();
        Text(Summary,std::to_wstring(paths.size())+L" selected files  ·  organizing sets...");
        EnableWindow(Item(Start),FALSE);ShowWindow(Item(Reveal),SW_HIDE);
        const auto worker=session;auto inputs=std::move(paths);
        std::thread([worker,inputs=std::move(inputs)]{
            Snapshot value;
            try{
                SelectionRequest request;request.paths=inputs;request.shouldCancel=[worker]{return worker->cancel.load();};
                request.reportProgress=[worker](std::size_t done,std::size_t total,const std::string& detail){
                    Snapshot update;update.fraction=total?double(done)/double(total):0;update.detail=Wide(detail);worker->Publish(std::move(update));};
                value.plan=std::make_shared<SelectionResult>(AnalyzeSelection(request));
                value.phase=Phase::Ready;value.fraction=0;
            }catch(const std::exception& error){value.plan=std::make_shared<SelectionResult>();value.plan->error=error.what();value.phase=Phase::Ready;}
            worker->Publish(std::move(value));
        }).detach();
    }
    std::vector<Output::Format> Formats(){std::vector<Output::Format> formats;
        for(const auto& pair:{std::pair{Dng,Output::Format::Dng},std::pair{Tiff,Output::Format::Tiff16},std::pair{Jpeg,Output::Format::Jpeg},std::pair{Png,Output::Format::Png}})
            if(SendMessageW(Item(pair.first),BM_GETCHECK,0,0)==BST_CHECKED)formats.push_back(pair.second);
        return formats;
    }
    void UpdateStart(){EnableWindow(Item(Start),!busy&&current.phase==Phase::Ready&&current.plan&&
        !current.plan->bursts.empty()&&!current.plan->canceled&&current.plan->error.empty()&&!Formats().empty());}
    void Refresh(){
        auto next=session->Take();const bool transition=next.phase!=current.phase;current=std::move(next);
        busy=current.phase==Phase::Analyzing||current.phase==Phase::Processing;
        if(closing&&!busy){DestroyWindow(handle);return;}
        SendMessageW(Item(Progress),PBM_SETPOS,WPARAM(std::clamp(current.fraction,0.0,1.0)*1000),0);
        if(busy){Text(Stage,session->cancel.load()?L"Canceling and cleaning up temporary work...":current.detail);return;}
        Text(Close,L"Close");EnableWindow(Item(Close),TRUE);
        if(transition&&current.phase==Phase::Ready){
            const auto& plan=*current.plan;
            Text(Summary,std::to_wstring(plan.bursts.size())+L" sets ready  ·  "+std::to_wstring(plan.skipped.size())+L" files skipped");
            Text(Start,L"Denoise "+Count(plan.bursts.size(),L"set",L"sets"));
            Text(Stage,plan.canceled?L"Selection canceled.":!plan.error.empty()?Wide(plan.error):plan.bursts.empty()?L"No compatible burst sets found. Select two or more related RAW images.":L"Ready to denoise");
            std::wostringstream details;
            for(const auto& set:plan.bursts)details<<Wide(set.name)<<L" — "<<set.sources.size()<<L" images\r\n";
            for(const auto& skip:plan.skipped)details<<skip.path.filename().wstring()<<L": "<<Wide(skip.message)<<L"\r\n";
            Text(Details,details.str());UpdateStart();
        }else if(transition&&current.phase==Phase::Finished){
            const auto& batch=*current.batch;
            Text(Summary,std::to_wstring(batch.completedSets)+L" sets completed  ·  "+std::to_wstring(batch.outputs.size())+L" files saved");
            Text(Stage,batch.canceled?L"Canceled. Completed files have been kept.":!batch.failures.empty()?L"Finished with some errors. See the details below.":L"Your burst outputs are ready.");
            std::wostringstream details;
            for(const auto& output:batch.outputs)details<<output.wstring()<<L"\r\n";
            for(const auto& failure:batch.failures)details<<Wide(failure.set)<<L": "<<Wide(failure.message)<<L"\r\n";
            Text(Details,details.str());ShowWindow(Item(Reveal),batch.outputs.empty()?SW_HIDE:SW_SHOW);Text(Start,L"Finished");
        }
    }
    void Begin(){
        if(busy||!current.plan||current.plan->bursts.empty())return;
        BatchOptions options;options.formats=Formats();if(options.formats.empty())return;
        options.location=SendMessageW(Item(Beside),BM_GETCHECK,0,0)==BST_CHECKED?OutputLocation::BesideOriginals:OutputLocation::NewFolder;
        options.aiAlignment=SendMessageW(Item(AiAlignment),BM_GETCHECK,0,0)==BST_CHECKED;
        auto plan=current.plan;auto worker=session;worker->cancel=false;busy=true;current.phase=Phase::Processing;
        for(int id:{Dng,Tiff,Jpeg,Png,Beside,Folder,AiAlignment,Start})EnableWindow(Item(id),FALSE);
        Text(Close,L"Cancel");Text(Stage,L"Starting burst processing...");
        std::thread([worker,plan,options]{
            auto batch=std::make_shared<BatchResult>();
            try{*batch=RunBatch(*plan,options,[worker]{return worker->cancel.load();},[worker](const BatchProgress& progress){
                Snapshot update;update.phase=Phase::Processing;update.fraction=progress.fraction;
                update.detail=L"Set "+std::to_wstring(progress.set)+L" of "+std::to_wstring(progress.total)+L"  ·  "+Wide(progress.detail);worker->Publish(std::move(update));});
            }catch(const std::exception& error){batch->failures.push_back({"Batch",error.what()});}
            Snapshot value;value.phase=Phase::Finished;value.fraction=batch->canceled?0:1;value.batch=std::move(batch);worker->Publish(std::move(value));
        }).detach();
    }
    void Cancel(bool close){
        if(!busy){DestroyWindow(handle);return;}
        closing=closing||close;session->cancel=true;Text(Stage,L"Canceling and cleaning up temporary work...");EnableWindow(Item(Close),FALSE);
        if(closing)ShowWindow(handle,SW_HIDE);
    }
    ~Window(){DeleteObject(background);if(font)DeleteObject(font);if(heading)DeleteObject(heading);}
};
LRESULT CALLBACK Procedure(HWND handle,UINT message,WPARAM wparam,LPARAM lparam){
    auto window=reinterpret_cast<Window*>(GetWindowLongPtrW(handle,GWLP_USERDATA));
    if(message==WM_NCCREATE){window=static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);window->handle=handle;SetWindowLongPtrW(handle,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(window));}
    if(!window)return DefWindowProcW(handle,message,wparam,lparam);
    switch(message){
    case WM_CREATE:window->Create();return 0;
    case Updated:window->Refresh();return 0;
    case WM_COMMAND:
        switch(LOWORD(wparam)){
        case IDOK:case Start:window->Begin();break;
        case IDCANCEL:case Close:window->Cancel(false);break;
        case Dng:case Tiff:case Jpeg:case Png:window->UpdateStart();break;
        case AiAlignment:
            window->Text(Hint,SendMessageW(window->Item(AiAlignment),BM_GETCHECK,0,0)==BST_CHECKED?
                L"SEA-RAFT suggests motion; RAW verification still decides.\r\nRequires the optional model package and may take longer.":
                L"Standard alignment. DNG stays editable;\r\nTIFF, PNG and JPEG are finished images.");break;
        case Beside:case Folder:
            window->Text(Hint,LOWORD(wparam)==Beside?L"Full-resolution outputs beside each set's originals.\r\nExisting files are kept.":L"Full-resolution outputs in a new Stack Burst folder.\r\nDNG stays editable; TIFF, PNG and JPEG are finished images.");break;
        case Reveal:
            if(window->current.batch&&!window->current.batch->outputs.empty())ShellExecuteW(handle,L"open",window->current.batch->outputs.front().parent_path().c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;
        }return 0;
    case WM_CLOSE:window->Cancel(true);return 0;
    case WM_DPICHANGED:{window->dpi=HIWORD(wparam);const auto rectangle=reinterpret_cast<RECT*>(lparam);
        SetWindowPos(handle,nullptr,rectangle->left,rectangle->top,rectangle->right-rectangle->left,rectangle->bottom-rectangle->top,SWP_NOZORDER|SWP_NOACTIVATE);window->Layout();return 0;}
    case WM_NOTIFY:{
        auto notification=reinterpret_cast<NMHDR*>(lparam);
        if(notification->code==NM_CUSTOMDRAW&&(notification->idFrom==Dng||notification->idFrom==Tiff||notification->idFrom==Png||notification->idFrom==Jpeg||notification->idFrom==Beside||notification->idFrom==Folder||notification->idFrom==Start||notification->idFrom==Close||notification->idFrom==Reveal))return WindowStyle::Button(*reinterpret_cast<NMCUSTOMDRAW*>(lparam),notification->idFrom==Start,window->dpi);
        break;
    }
    case WM_ERASEBKGND:{RECT r{};GetClientRect(handle,&r);FillRect(reinterpret_cast<HDC>(wparam),&r,window->background);return 1;}
    case WM_SETTINGCHANGE:
        DeleteObject(window->background);window->background=CreateSolidBrush(WindowStyle::Background());
        RedrawWindow(handle,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);break;
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:{
        const int id=GetDlgCtrlID(reinterpret_cast<HWND>(lparam));
        SetTextColor(reinterpret_cast<HDC>(wparam),WindowStyle::Foreground(id==Summary||id==Hint));
        SetBkColor(reinterpret_cast<HDC>(wparam),WindowStyle::Background());
        return reinterpret_cast<LRESULT>(window->background);
    }
    case WM_DESTROY:PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(handle,message,wparam,lparam);
}
}
int RunWindow(std::vector<std::filesystem::path> paths){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS|ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    const auto instance=GetModuleHandleW(nullptr);WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc=Procedure;type.hInstance=instance;type.hCursor=LoadCursor(nullptr,IDC_ARROW);
    type.hbrBackground=nullptr;type.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));type.lpszClassName=L"StackBurstWindow";
    RegisterClassExW(&type);Window window;window.paths=std::move(paths);
    const DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    const auto dpi=GetDpiForSystem();RECT rectangle{0,0,MulDiv(604,dpi,96),MulDiv(604,dpi,96)};
    AdjustWindowRectExForDpi(&rectangle,style,FALSE,0,dpi);
    RECT workArea{};SystemParametersInfoW(SPI_GETWORKAREA,0,&workArea,0);
    rectangle.bottom=std::min(rectangle.bottom,rectangle.top+workArea.bottom-workArea.top-MulDiv(24,dpi,96));
    const auto handle=CreateWindowExW(0,type.lpszClassName,L"Stack Burst",style,CW_USEDEFAULT,CW_USEDEFAULT,
        rectangle.right-rectangle.left,rectangle.bottom-rectangle.top,nullptr,nullptr,instance,&window);
    if(!handle)throw std::runtime_error("Could not open the Stack Burst window.");
    ShowWindow(handle,SW_SHOWNORMAL);MSG message{};
    while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(handle,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    return 0;
}
}
