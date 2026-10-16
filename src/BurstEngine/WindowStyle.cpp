#include "WindowStyle.h"

namespace Stack::BurstEngine::WindowStyle {
bool HighContrast(){HIGHCONTRASTW value{sizeof(value)};return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(value),&value,0)&&(value.dwFlags&HCF_HIGHCONTRASTON);}
COLORREF Background(){return HighContrast()?GetSysColor(COLOR_WINDOW):RGB(248,249,251);}
COLORREF Foreground(bool secondary){return HighContrast()?GetSysColor(COLOR_WINDOWTEXT):(secondary?RGB(89,99,116):RGB(28,37,51));}
LRESULT Button(NMCUSTOMDRAW& draw,bool primary,unsigned dpi){
    if(HighContrast()||draw.dwDrawStage!=CDDS_PREPAINT)return CDRF_DODEFAULT;
    const HWND button=draw.hdr.hwndFrom;
    const bool checked=SendMessageW(button,BM_GETCHECK,0,0)==BST_CHECKED;
    const bool disabled=!IsWindowEnabled(button);
    const bool hot=(draw.uItemState&CDIS_HOT)!=0;
    const bool pressed=(draw.uItemState&CDIS_SELECTED)!=0;
    const bool solid=primary&&!disabled;
    COLORREF fill=solid?RGB(36,91,181):checked?RGB(225,237,255):hot&&!disabled?RGB(236,240,246):RGB(255,255,255);
    if(pressed&&!disabled)fill=solid?RGB(27,72,148):RGB(213,226,245);
    const auto brush=CreateSolidBrush(fill);
    const auto pen=CreatePen(PS_SOLID,1,solid?fill:checked?RGB(83,129,196):RGB(212,218,227));
    auto oldBrush=SelectObject(draw.hdc,brush);auto oldPen=SelectObject(draw.hdc,pen);
    RECT r=draw.rc;const int radius=MulDiv(12,dpi,96);
    RoundRect(draw.hdc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(draw.hdc,oldBrush);SelectObject(draw.hdc,oldPen);DeleteObject(brush);DeleteObject(pen);
    SetBkMode(draw.hdc,TRANSPARENT);
    SetTextColor(draw.hdc,solid?RGB(255,255,255):disabled&&!checked?RGB(115,123,137):checked?RGB(28,75,146):Foreground());
    wchar_t text[128]{};GetWindowTextW(button,text,128);
    auto oldFont=SelectObject(draw.hdc,reinterpret_cast<HFONT>(SendMessageW(button,WM_GETFONT,0,0)));
    DrawTextW(draw.hdc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(checked){
        // A persistent mark distinguishes selection without relying only on color.
        RECT mark=r;mark.left+=MulDiv(10,dpi,96);mark.right=mark.left+MulDiv(12,dpi,96);
        DrawTextW(draw.hdc,L"\u2713",1,&mark,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    SelectObject(draw.hdc,oldFont);
    if((draw.uItemState&CDIS_FOCUS)&&!(SendMessageW(button,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){
        InflateRect(&r,-MulDiv(4,dpi,96),-MulDiv(4,dpi,96));DrawFocusRect(draw.hdc,&r);
    }
    return CDRF_SKIPDEFAULT;
}
}
