#include "ark/agent.hpp"
#include "ark/agent_activity.hpp"
#include "ark/assistant.hpp"
#include "ark/io.hpp"
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#endif
using namespace ark;using V=arkjson::Value;
namespace {
int checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
V json(const std::string& text){V value;check(arkjson::parse(text,value),"valid fixture JSON");return value;}
void canonical(V& value){if(value.t==V::Obj){for(auto& entry:value.obj)canonical(entry.second);std::sort(value.obj.begin(),value.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});}else if(value.t==V::Arr)for(auto& item:value.arr)canonical(item);}
V good(const V& response){if(response.find("isError")->b)throw std::runtime_error(response.find("structuredContent")->dump());return *response.find("structuredContent");}
V call(AgentSession& s,const char* name,V args=V::makeObj()){return good(s.call(name,args));}
V apply(AgentSession& s,const std::string& text){V args=V::makeObj();args.set("expected_revision",V::makeNum((double)s.revision));args.set("commands",json(text));return call(s,"doc_apply",args);}
V actor(const char* id="engineer"){V out=json(R"({"kind":"agent","model":"offline-fixture"})");out.set("id",V::makeStr(id));return out;}
void cacheChecks(){
    AgentSession s;apply(s,R"([{"op":"plane.set","name":"Wing top","p":{"oz":10}},{"op":"sketch.set","name":"Root profile","sketch":{"plane":"XY","entities":[{"kind":"circle","dof":[0,0,5]}]}},{"op":"sketch.set","name":"Tip profile","sketch":{"plane":"Wing top","entities":[{"kind":"circle","dof":[0,0,3]}]}},{"op":"feature.add","type":"loft","name":"Test loft","p":{"solid":1,"ruled":0},"native":{"sections":["Root profile","Tip profile"]}}])");
    check(s.document.recomputeEvaluated==1,"fixture loft evaluated once");const auto shape=s.document.evalBodies.front().nativeData.dump();
    auto author=s.document.authoringCopy();check(author.evalBodies.empty()&&author.evaluationCache.empty()&&documentToJson(author)==documentToJson(s.document),"authoring copy preserves all persisted state without evaluated geometry");
    apply(s,R"([{"op":"param.set","name":"unrelated","expr":7},{"op":"plane.set","name":"Unrelated plane","p":{"ox":100}},{"op":"sketch.set","name":"Unrelated profile","sketch":{"plane":"Unrelated plane","entities":[{"kind":"circle","dof":[0,0,2]}]}}])");
    check(s.document.recomputeEvaluated==0&&s.document.recomputeReused==1&&s.document.evalBodies.front().nativeData.dump()==shape,"unrelated sketch, plane and parameter reuse exact loft without native re-evaluation");
    apply(s,R"([{"op":"plane.set","name":"Wing top","p":{"oz":12}}])");check(s.document.recomputeEvaluated==1&&s.document.evalBodies.front().nativeData.dump()!=shape,"referenced plane mutation invalidates dependent loft");
    const auto current=s.document.evalBodies.front().nativeData.dump();auto result=apply(s,R"([{"op":"feature.edit","id":1,"appearance":{"opacity":0.35}}])");check(!result.find("geometry_recomputed")->b&&s.document.evalBodies.front().nativeData.dump()==current,"opacity changes no evaluated BRep or mesh");
    const auto before=documentToJson(s.document);V invalid=json(R"({"commands":[{"op":"feature.edit","id":1,"appearance":{"opacity":2}}]})");invalid.set("expected_revision",V::makeNum((double)s.revision));check(s.call("doc_apply",invalid).find("isError")->b&&documentToJson(s.document)==before&&s.document.evalBodies.front().nativeData.dump()==current,"invalid metadata rollback preserves authoring and evaluated geometry");
    V preview=json(R"({"detail":"changes","commands":[{"op":"feature.edit","id":1,"appearance":{"opacity":1}}]})");preview.set("expected_revision",V::makeNum((double)s.revision));auto candidate=call(s,"doc_preview",preview);check(candidate.find("candidate_cached")->b,"metadata candidate retained");call(s,"usage_record",json(R"({"id":"audit-between-preview","input_tokens":10,"output_tokens":2,"total_tokens":12,"model":"offline-fixture"})"));auto applied=call(s,"doc_apply",*candidate.find("apply"));check(applied.find("applied_prepared")->b&&!applied.find("geometry_changed")->b,"provider audit update cannot invalidate unchanged CAD candidate or force GPU rebuild");
}
void activityChecks(){
    AgentSession s;V args=json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"box","name":"Fixture"}]})");args.set("actor",actor());auto result=call(s,"doc_apply",args);const auto id=result.find("activity_id")->str;check(result.find("feedback_required")->b&&s.document.agentActivity&&s.document.agentActivity->entries.size()==1,"actual agent action gets measured project record and feedback ID");
    const auto& measured=s.document.agentActivity->entries.front();check(measured.find("elapsed_ms")->num>=0&&!measured.find("is_error")->b&&measured.find("revision_after")->num==1,"measured outcome and revision remain host-owned");
    const auto authoring=documentToJson(s.document,false,false);const auto revision=s.revision;
    V rating=json(R"({"ratings":[{"utility":4,"difficulty":2,"action":"Created fixture box","wished_options":"A scoped dimensions hint"}]})");V ratings=*rating.find("ratings");ratings.arr.front().set("activity_id",V::makeStr(id));rating.set("ratings",ratings);rating.set("actor",actor("other"));check(s.call("tool_feedback",rating).find("isError")->b&&!s.document.agentActivity->entries.front().find("feedback"),"one role cannot rate another role's actions");
    rating.set("actor",actor());call(s,"tool_feedback",rating);check(s.revision==revision&&documentToJson(s.document,false,false)==authoring&&s.document.agentActivity->entries.front().find("feedback")->find("utility")->num==4,"ratings persist separately from geometry revision and authoring fingerprint");
    check(s.call("tool_feedback",rating).find("isError")->b,"ratings are immutable rather than silently rewritten");
    V failure=json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"wrong","expr":1}]})");failure.set("actor",actor());check(s.call("doc_apply",failure).find("isError")->b&&s.document.agentActivity->entries.size()==2&&s.document.agentActivity->entries.back().find("is_error")->b,"failed attempts are retained without fabricated ratings");
    auto pending=call(s,"activity_info",json(R"({"pending_only":true})"));check(pending.find("pending_feedback")->num==1&&pending.find("entries")->arr.size()==1,"pending feedback is inspectable and paginated");
    const auto path=std::filesystem::temp_directory_path()/("ark-activity-"+s.document.agentActivity->id+".ark");Document loaded;std::string error;check(saveDocument(s.document,path.string().c_str())&&loadDocument(path.string().c_str(),loaded,&error),"project SQLite container roundtrips activity ledger");V loadedAudit=agentActivityJson(loaded.agentActivity),originalAudit=agentActivityJson(s.document.agentActivity);canonical(loadedAudit);canonical(originalAudit);check(loadedAudit.dump()==originalAudit.dump(),"measured entries and subjective scores reopen exactly independent of object key order");std::filesystem::remove(path);
    auto malformed=agentActivityJson(s.document.agentActivity);for(auto& field:malformed.obj)if(field.first=="entries")for(auto& field:field.second.arr.front().obj)if(field.first=="utc")field.second=V{};bool rejected=false;try{readAgentActivity(malformed);}catch(const std::exception&){rejected=true;}check(rejected,"malformed activity timestamp is rejected before display");
    call(s,"doc_undo");check(s.document.agentActivity->entries.size()==2,"geometry undo retains tool attempts and ratings");
}
V response(const V& output){V value=V::makeObj();value.set("status",V::makeStr("completed"));value.set("output",output);return value;}
V toolResponse(const char* id,const char* name,V args){V item=V::makeObj(),out=V::makeArr();item.set("type",V::makeStr("function_call"));item.set("call_id",V::makeStr(id));item.set("name",V::makeStr(name));item.set("arguments",V::makeStr(args.dump()));out.arr.push_back(item);return response(out);}
V done(){return json(R"({"status":"completed","output":[{"type":"message","role":"assistant","content":[{"type":"output_text","text":"Completed fixture"}]}]})");}
void requiredFeedbackChecks(){
    AgentSession session;AssistantConfig config;config.model="offline-feedback";int rounds=0;std::string activity;
    auto run=runAssistant(config,V::makeStr("Inspect and report"),AgentSession::tools(),[&](const std::string& name,V args){args.set("actor",actor());auto result=session.call(name,args);if(const auto* id=result.find("structuredContent")->find("activity_id"))activity=id->str;return result;},{},nullptr,[&](auto&,const V& request,auto*){
        switch(++rounds){case 1:return toolResponse("inspect","doc_info",V::makeObj());case 2:return done();case 3:{check(request.dump().find("Before finishing, submit tool_feedback")!=std::string::npos,"premature completion triggers required feedback");V args=json(R"({"ratings":[{"utility":5,"difficulty":1,"action":"Read the empty fixture inventory"}]})");auto rows=*args.find("ratings");rows.arr.front().set("activity_id",V::makeStr(activity));args.set("ratings",rows);return toolResponse("rate","tool_feedback",args);}default:return done();}
    });check(rounds==4&&session.document.agentActivity->entries.front().find("feedback")&&!run.text.empty(),"single agent cannot complete before rating actual tool use");
}
void pngChecks(){
    Image image;image.w=1024;image.h=768;image.px.resize((size_t)image.w*image.h*3);for(size_t i=0;i<image.px.size();i+=3){image.px[i]=(u8)((i/3)%255);image.px[i+1]=73;image.px[i+2]=201;}
    auto png=encodePNG(image);check(png.size()>24&&png[0]==137&&png[1]=='P',"snapshot remains PNG");
#ifdef _WIN32
    check(png.size()<image.px.size()/4,"lossless encoder removes stored-deflate snapshot bloat");
    HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);IWICImagingFactory* factory=nullptr;IWICStream* stream=nullptr;IWICBitmapDecoder* decoder=nullptr;IWICBitmapFrameDecode* frame=nullptr;IWICFormatConverter* converter=nullptr;
    check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))&&SUCCEEDED(factory->CreateStream(&stream))&&SUCCEEDED(stream->InitializeFromMemory(png.data(),(DWORD)png.size()))&&SUCCEEDED(factory->CreateDecoderFromStream(stream,nullptr,WICDecodeMetadataCacheOnLoad,&decoder))&&SUCCEEDED(decoder->GetFrame(0,&frame))&&SUCCEEDED(factory->CreateFormatConverter(&converter))&&SUCCEEDED(converter->Initialize(frame,GUID_WICPixelFormat24bppRGB,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)),"compressed snapshot decodes through OS PNG codec");
    std::vector<u8> decoded(image.px.size());check(SUCCEEDED(converter->CopyPixels(nullptr,image.w*3,(UINT)decoded.size(),decoded.data()))&&decoded==image.px,"all original RGB pixels survive lossless compression exactly");converter->Release();frame->Release();decoder->Release();stream->Release();factory->Release();if(SUCCEEDED(init))CoUninitialize();
#endif
    std::cout<<"PNG_LOSSLESS raw_rgb_bytes="<<image.px.size()<<" png_bytes="<<png.size()<<"\n";
}
}
int main(){try{cacheChecks();activityChecks();requiredFeedbackChecks();pngChecks();std::cout<<checks<<" reactivity/activity checks passed\n";return 0;}catch(const std::exception& error){std::cerr<<"Reactivity/activity failed: "<<error.what()<<'\n';return 1;}}
