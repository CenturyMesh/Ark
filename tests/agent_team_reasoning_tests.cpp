// Team reasoning attribution: each Text event carries the readable reasoning
// its provider returned since that role's previous Text event in the same
// turn, captured when the text is emitted. A UI that polls after several
// replies must not move later reasoning onto an earlier reply.
#include "ark/agent_team.hpp"
#include "ark/agent.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using V=arkjson::Value;
using namespace std::chrono_literals;
static int checks=0;
static void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
// One completed provider response: optional readable reasoning summary,
// visible text and an optional read-only tool call (text before a call is
// emitted immediately; final text is emitted when the turn completes).
static V response(const std::string& reasoning,const std::string& text,const std::string& callId={}){
    V output=V::makeArr();
    if(!reasoning.empty()){V item=V::makeObj(),summary=V::makeArr(),part=V::makeObj();item.set("type",V::makeStr("reasoning"));item.set("id",V::makeStr("rs_"+callId));part.set("type",V::makeStr("summary_text"));part.set("text",V::makeStr(reasoning));summary.arr.push_back(part);item.set("summary",summary);output.arr.push_back(item);}
    {V item=V::makeObj(),content=V::makeArr(),part=V::makeObj();item.set("type",V::makeStr("message"));item.set("role",V::makeStr("assistant"));part.set("type",V::makeStr("output_text"));part.set("text",V::makeStr(text));content.arr.push_back(part);item.set("content",content);output.arr.push_back(item);}
    if(!callId.empty()){V item=V::makeObj();item.set("type",V::makeStr("function_call"));item.set("call_id",V::makeStr(callId));item.set("name",V::makeStr("doc_info"));item.set("arguments",V::makeStr("{}"));output.arr.push_back(item);}
    V result=V::makeObj();result.set("status",V::makeStr("completed"));result.set("output",output);return result;
}
static ark::AgentTeamRole role(const std::string& id){ark::AgentTeamRole role;role.id=id;role.label=id;role.instructions="Review measured design evidence.";role.assistant.model="offline-"+id;return role;}
static V revision(){V v;arkjson::parse(R"({"revision":0})",v);return v;}
static std::vector<ark::AgentTeamEvent> texts(const ark::AgentTeamSnapshot& snapshot,const std::string& id){std::vector<ark::AgentTeamEvent> out;for(const auto& event:snapshot.events)if(event.kind==ark::AgentTeamEvent::Text&&event.roleId==id)out.push_back(event);return out;}
static bool validUtf8(const std::string& text){
    for(size_t i=0;i<text.size();){const unsigned char c=(unsigned char)text[i];size_t n=c<0x80?1:(c>>5)==6?2:(c>>4)==14?3:(c>>3)==30?4:0;if(!n||i+n>text.size())return false;
        for(size_t k=1;k<n;++k)if(((unsigned char)text[i+k]&0xC0)!=0x80)return false;i+=n;}
    return true;
}
// Two replies in one turn, each preceded by its own reasoning, read only after
// the turn finished (the late-poll case): each reply keeps its own reasoning.
static void reasoningTravelsWithEachReply(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.roles={role("engineer")};int rounds=0;
    config.roles[0].transport=[&](auto&,auto&,auto*){return ++rounds==1?response("Inspect the revision before choosing a flange.","Reading the current revision first.","inspect"):response("The 4 mm flange keeps the deflection below the stated limit.","Flange set to 4 mm.");};
    team.start(config,"Review the bracket",ark::AgentSession::tools(),[](auto&,auto&){return revision();});check(team.waitIdleFor(5s),"bounded team completion");
    auto snapshot=team.snapshot();auto replies=texts(snapshot,"engineer");
    check(replies.size()==2,"both replies are emitted as Text events");
    check(replies[0].reasoning=="Inspect the revision before choosing a flange."&&!replies[0].reasoningEarlier,"first reply carries only the reasoning returned before it");
    check(replies[1].reasoning=="The 4 mm flange keeps the deflection below the stated limit."&&replies[1].reasoningEarlier,"second reply carries only its own reasoning, not the first reply's");
    check(snapshot.roles[0].reasoning.find("Inspect the revision")!=std::string::npos&&snapshot.roles[0].reasoning.find("4 mm flange")!=std::string::npos,"latest-reasoning view still holds the whole turn");
    for(const auto& event:snapshot.events)if(event.kind!=ark::AgentTeamEvent::Text)check(event.reasoning.empty(),"only Text events carry reasoning");
    // A reply without new reasoning after one that had some says so; a new
    // turn starts without the previous turn's reasoning or earlier flag.
    rounds=0;config.roles[0].transport=[&](auto&,auto&,auto*){return ++rounds==1?response("Check the hole pitch.","Checking the pitch.","pitch"):response("","Pitch confirmed at 40 mm.");};
    ark::AgentTeam second;second.start(config,"Confirm the pitch",ark::AgentSession::tools(),[](auto&,auto&){return revision();});check(second.waitIdleFor(5s),"bounded second team");
    replies=texts(second.snapshot(),"engineer");check(replies.size()==2&&replies[0].reasoning=="Check the hole pitch."&&replies[1].reasoning.empty()&&replies[1].reasoningEarlier,"reply without new reasoning is distinguishable from an unexposed provider");
    rounds=1;check(second.steer("Recheck the pitch","engineer")&&second.waitIdleFor(5s),"second turn completes");
    replies=texts(second.snapshot(),"engineer");check(replies.size()==3&&replies[2].reasoning.empty()&&!replies[2].reasoningEarlier,"new turn does not inherit the earlier flag or reasoning");
}
// Bounded reasoning keeps its newest text and never splits a UTF-8 character.
static void boundedReasoningKeepsUtf8(){
    ark::AgentTeam team;ark::AgentTeamConfig config;config.maxMailboxBytes=1024;config.roles={role("designer")};
    std::string long_;for(int i=0;i<700;++i)long_+=i%2?"\xE2\x82\xAC":"\xC3\xA9";long_+=" newest end";
    config.roles[0].transport=[&](auto&,auto&,auto*){return response(long_,"Done.");};
    team.start(config,"Bound the reasoning",ark::AgentSession::tools(),[](auto&,auto&){return revision();});check(team.waitIdleFor(5s),"bounded team completion");
    auto snapshot=team.snapshot();auto replies=texts(snapshot,"designer");check(replies.size()==1,"one reply");
    const auto& kept=replies[0].reasoning;check(kept.size()<=1024&&kept.size()>900,"event reasoning is bounded by the mailbox limit");
    check(validUtf8(kept)&&kept.rfind("[earlier output truncated] ",0)==0&&kept.size()>=11&&kept.substr(kept.size()-11)==" newest end","truncation keeps the newest text on a UTF-8 boundary");
    check(validUtf8(snapshot.roles[0].reasoning),"role reasoning view stays valid UTF-8");
}
int main(){try{reasoningTravelsWithEachReply();boundedReasoningKeepsUtf8();std::cout<<"Agent team reasoning checks passed: "<<checks<<"\n";return 0;}catch(const std::exception& error){std::cerr<<"Agent team reasoning checks failed: "<<error.what()<<"\n";return 1;}}
