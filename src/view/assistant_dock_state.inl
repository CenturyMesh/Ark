// Right-side assistant dock: layout state and provider reasoning notes.
// The dock spans the full height between the ribbon and the status bar and is
// outermost on the right; tool docks (baked motions, exploded views, inertial,
// rubber) sit between it and the viewport. Rail = collapsed strip, Normal =
// user width (drag handle), Expanded = about half the window.
#include "ui_theme.inl"
static bool g_chatDockRail=false;          // collapsed to the rail (persisted)
static int g_chatDockUserWidth=0;          // normal width at 96 dpi; 0 = default (persisted)
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
    using DpiForWindow=UINT(WINAPI*)(HWND);static const auto query=reinterpret_cast<DpiForWindow>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow"));
    const UINT dpi=query&&g_window?query(g_window):96;return (int)std::clamp<UINT>(dpi?dpi:96,96,288);
}
static int chatDockPx(int px){return MulDiv(px,chatDockDpi(),96);}
static int chatDockRailWidth(){return chatDockPx(44);}
static int chatDockToolDocks(){return bakedMotionsDockWidth()+explodeDockWidth()+inertialDockWidth056()+rubberDockWidth059();}
static int chatDockMinWidth(){return chatDockPx(340);}
// Keep at least 460 px of viewport beside the sidebar and any open tool dock:
// the viewport display bar needs 440 px and the appearance strip 405 px.
static int chatDockMaxWidth(){const int sidebar=std::clamp((int)(winW*.21),280,352);return std::max(chatDockMinWidth(),winW-sidebar-chatDockToolDocks()-460);}
static int chatDockNormalWidth(){return std::clamp(chatDockPx(g_chatDockUserWidth>0?g_chatDockUserWidth:400),chatDockMinWidth(),chatDockMaxWidth());}
static int chatDockExpandedWidth(){return std::clamp((int)(winW*.48),chatDockNormalWidth(),chatDockMaxWidth());}
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
static void resizeChatDock(int pointerX){const int width=std::clamp(winW-pointerX,chatDockMinWidth(),chatDockMaxWidth());g_chatDockUserWidth=std::max(1,MulDiv(width,96,chatDockDpi()));setChatDockMode(1);}
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
static void attachChatReasoning(size_t row,std::string text,std::string empty){
    if(row>=g_chatLog.size())return;if(text.size()>chatReasoningLimit)text="[earlier reasoning truncated] "+text.substr(text.size()-chatReasoningLimit);
    g_chatReasoningNotes[row]={g_chatLog[row].first,g_chatLog[row].second,std::move(text),std::move(empty)};++g_chatReasoningSerial;
}
// A note belongs to the exact row it was attached to; cleared or rewritten
// conversations never inherit a stale note by index.
static const ChatReasoningNote* chatReasoningNote(size_t row){
    auto found=g_chatReasoningNotes.find(row);if(found==g_chatReasoningNotes.end()||row>=g_chatLog.size())return nullptr;
    const auto& note=found->second;return g_chatLog[row].first==note.role&&g_chatLog[row].second==note.row?&note:nullptr;
}
static void appendAssistantReasoning(const std::string& text){
    if(text.empty())return;if(!g_assistantReasoningRun.empty())g_assistantReasoningRun+="\n\n";g_assistantReasoningRun+=text;
    if(g_assistantReasoningRun.size()>chatReasoningLimit)g_assistantReasoningRun="[earlier reasoning truncated] "+g_assistantReasoningRun.substr(g_assistantReasoningRun.size()-chatReasoningLimit);
    ++g_chatReasoningSerial;
}
// Attach the role's reasoning returned since its previous message in this turn.
static void attachTeamReasoning(size_t row,size_t role){
    if(role>=teamCapacity)return;const AgentTeamRoleSnapshot* view=role<g_teamSnapshot.roles.size()?&g_teamSnapshot.roles[role]:nullptr;
    const bool current=view&&!view->reasoning.empty()&&view->reasoningTurn==view->turns;auto& shown=g_teamReasoningAttached[role];
    if(!current){attachChatReasoning(row,{},chatReasoningHidden);return;}
    if(shown.first!=view->reasoningTurn||shown.second>view->reasoning.size())shown={view->reasoningTurn,0};
    std::string delta=view->reasoning.substr(shown.second);while(!delta.empty()&&(delta.front()=='\n'||delta.front()==' '))delta.erase(delta.begin());
    const bool earlier=shown.second>0;shown.second=view->reasoning.size();
    attachChatReasoning(row,std::move(delta),earlier?"No new reasoning since the previous message":chatReasoningHidden);
}
