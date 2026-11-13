// Right-side assistant dock: layout state and provider reasoning notes.
// The dock spans the full height between the ribbon and the status bar and is
// outermost on the right; tool docks (baked motions, exploded views, inertial,
// rubber) sit between it and the viewport. Rail = collapsed strip, Normal =
// user width (drag handle), Expanded = about half the window.
#include "ui_theme.inl"
static bool g_chatDockRail=false;          // collapsed to the rail (persisted)
static double g_chatDockUserWidth=0;       // normal width in 96-dpi units, fractional so a dragged width restores exactly; 0 = default (persisted)
static int g_chatDockDpiOverride=0;        // self-test only: lays the dock out as if at this DPI
static bool g_chatDockResizing=false;
static size_t g_chatDockSeenRows=0;        // conversation rows already shown in an open dock
static bool g_chatReasoningAll=false;      // expand every reasoning section
static int g_chatReasoningAgent=-1;        // latest-reasoning panel: -1 hidden; single 0; team role index
// Readable reasoning attached to the conversation row of the reply it led to.
// Only provider-returned text is stored; `empty` explains why none is shown.
struct ChatReasoningNote {std::string role,row,text,empty;};
static std::map<size_t,ChatReasoningNote> g_chatReasoningNotes;
static u64 g_chatReasoningSerial=0;
static std::string g_assistantReasoningRun; // readable reasoning of the current/last single run
static bool g_assistantRunStarted=false;
static constexpr size_t chatReasoningLimit=256*1024;
static constexpr const char* chatReasoningHidden="Reasoning not exposed by this provider";

static int chatDockDpi(){
    if(g_chatDockDpiOverride)return g_chatDockDpiOverride;
    using DpiForWindow=UINT(WINAPI*)(HWND);static const auto query=reinterpret_cast<DpiForWindow>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
    const UINT dpi=query&&g_window?query(g_window):96;return (int)std::clamp<UINT>(dpi?dpi:96,96,288);
}
static int chatDockPx(int px){return MulDiv(px,chatDockDpi(),96);}
static int chatDockRailWidth(){return chatDockPx(44);}
static int chatDockToolDocks(){return bakedMotionsDockWidth()+explodeDockWidth()+inertialDockWidth056()+rubberDockWidth059();}
// Preferred widths (rail 44, minimum 340, normal 400 at 96 dpi) scale with the
// DPI. The hard limits are physical pixels, like the things they protect: the
// OpenGL sidebar and viewport controls are not DPI-scaled, and neither are the
// native header buttons. The viewport keeps a 460 px reserve (its display bar
// needs 440, the appearance strip 405) beside the sidebar and open tool docks.
static constexpr int chatDockViewportReserve=460,chatDockFloor=340;
static int chatDockRoom(){const int sidebar=std::clamp((int)(winW*.21),280,352);return winW-sidebar-chatDockToolDocks()-chatDockViewportReserve;}
// The scaled minimum yields to that reserve, but never below the 340 px floor
// the dock's own fixed-size header controls need.
static int chatDockMinWidth(){return std::clamp(chatDockPx(chatDockFloor),chatDockFloor,std::max(chatDockFloor,chatDockRoom()));}
static int chatDockMaxWidth(){return std::max(chatDockMinWidth(),chatDockRoom());}
static int chatDockNormalWidth(){return std::clamp(g_chatDockUserWidth>0?(int)std::lround(g_chatDockUserWidth*chatDockDpi()/96.):chatDockPx(400),chatDockMinWidth(),chatDockMaxWidth());}
static int chatDockExpandedWidth(){return std::clamp((int)(winW*.48),chatDockNormalWidth(),chatDockMaxWidth());}
// Expand is offered only when it would actually widen the dock.
static bool chatDockCanExpand(){return chatDockExpandedWidth()>chatDockNormalWidth();}
static int assistantDockWidth(){if(!g_assistantDockOpen)return 0;if(g_chatDockRail)return chatDockRailWidth();return g_chatExpanded?chatDockExpandedWidth():chatDockNormalWidth();}
// Full dock height between the ribbon and the status bar (0 when closed).
static int assistantDockHeight(){return g_assistantDockOpen?std::max(1,winH-viewY-32):0;}
// 0 rail, 1 normal, 2 expanded. The "expanded" chat state is the wide dock.
static void setChatDockMode(int mode){g_chatDockRail=mode==0;g_chatExpanded=mode==2;}
static int chatDockMode(){return g_chatDockRail?0:g_chatExpanded?2:1;}
// Entry points that need the composer (object questions, image references,
// CAD import prompts) open the dock and leave the rail; the width is kept.
static void showAssistantDock(){g_assistantDockOpen=true;g_chatDockRail=false;}
static const char* chatDockModeName(){return g_chatDockRail?"rail":g_chatExpanded?"expanded":"normal";}
// Drag handle on the dock's left edge sets the normal width from the pointer.
static void resizeChatDock(int pointerX){const int width=std::clamp(winW-pointerX,chatDockMinWidth(),chatDockMaxWidth());g_chatDockUserWidth=width*96./chatDockDpi();setChatDockMode(1);}
static int assistantImageComposerHeight();
struct ChatDockGeometry {
    bool open=false,rail=false;int x=0,y=0,w=0,h=0,pad=12,inner=1;
    int rowA=0,rowB=0,chatTop=0,chatBottom=0,imageTop=0,imageH=0,labelY=0,inputY=0,inputH=62,actionsY=0,footerY=0;
};
// Single source for the GL dock, native conversation, composer EDIT and tests.
static ChatDockGeometry chatDockGeometry(){
    ChatDockGeometry g;g.open=g_assistantDockOpen;g.rail=g_chatDockRail;g.w=assistantDockWidth();g.x=winW-g.w;g.y=viewY;g.h=std::max(1,winH-32-viewY);g.inner=std::max(1,g.w-2*g.pad);
    const int bottom=g.y+g.h;g.rowA=g.y+10;g.rowB=g.rowA+38;g.chatTop=g.rowB+28;
    g.actionsY=bottom-42;g.inputY=g.actionsY-8-g.inputH;g.labelY=g.inputY-26;g.footerY=g.labelY;
    g.imageH=assistantImageComposerHeight()>40?104:0;g.imageTop=g.labelY-(g.imageH?g.imageH+8:0);g.chatBottom=std::max(g.chatTop+1,g.imageTop-8);
    return g;
}
static bool chatDockHandleAt(POINT p){if(!g_assistantDockOpen||g_chatDockRail)return false;const int x=winW-assistantDockWidth();return p.x>=x-3&&p.x<x+5&&p.y>=viewY&&p.y<winH-32;}
static bool chatDockRunning(){return g_chatBusy||(g_teamStarted&&g_teamSnapshot.running);}
static size_t chatDockUnread(){return g_chatLog.size()>g_chatDockSeenRows?g_chatLog.size()-g_chatDockSeenRows:0;}
// Short role names for reasoning chips; the panel caption shows id and label.
static std::string chatAgentName(size_t index){
    if(!g_teamMode)return "ARK";const size_t slot=teamRoleSlot(index);if(slot>=teamCapacity)return {};const std::string id=g_teamRoleIds[slot];
    if(id=="engineer")return "Engineer";if(id=="reviewer")return "Reviewer";if(id=="parametric")return "Parametric";if(id=="designer")return "Designer 1";
    if(id.rfind("designer",0)==0)return "Designer "+id.substr(8);return id;
}
// Keeps the newest chatReasoningLimit bytes, starting on a UTF-8 code-point
// boundary so a multi-byte character is never split.
static std::string chatReasoningTail(std::string text){
    if(text.size()<=chatReasoningLimit)return text;size_t start=text.size()-chatReasoningLimit;
    while(start<text.size()&&((unsigned char)text[start]&0xC0)==0x80)++start;return "[earlier reasoning truncated] "+text.substr(start);
}
static void attachChatReasoning(size_t row,std::string text,std::string empty){
    if(row>=g_chatLog.size())return;text=chatReasoningTail(std::move(text));
    g_chatReasoningNotes[row]={g_chatLog[row].first,g_chatLog[row].second,std::move(text),std::move(empty)};++g_chatReasoningSerial;
}
// A replaced document starts without the previous conversation's reasoning;
// `rows` also drops the notes of rows the caller has just cleared.
static void clearChatReasoning(bool rows){if(rows)g_chatReasoningNotes.clear();g_assistantReasoningRun.clear();g_assistantRunStarted=false;++g_chatReasoningSerial;}
// A note belongs to the exact row it was attached to; cleared or rewritten
// conversations never inherit a stale note by index.
static const ChatReasoningNote* chatReasoningNote(size_t row){
    auto found=g_chatReasoningNotes.find(row);if(found==g_chatReasoningNotes.end()||row>=g_chatLog.size())return nullptr;
    const auto& note=found->second;return g_chatLog[row].first==note.role&&g_chatLog[row].second==note.row?&note:nullptr;
}
static void appendAssistantReasoning(const std::string& text){
    if(text.empty())return;if(!g_assistantReasoningRun.empty())g_assistantReasoningRun+="\n\n";g_assistantReasoningRun+=text;
    g_assistantReasoningRun=chatReasoningTail(std::move(g_assistantReasoningRun));
    ++g_chatReasoningSerial;
}
// A role's Text event carries the reasoning its provider returned since that
// role's previous reply in the turn, captured when the reply was emitted. A
// poll that reads several replies at once cannot move it to an earlier row.
static void attachTeamReasoning(size_t row,const AgentTeamEvent& event){
    attachChatReasoning(row,event.reasoning,event.reasoningEarlier?"No new reasoning since the previous message":chatReasoningHidden);
}
