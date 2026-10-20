// 046B background exact reads: allow-list, ordering after writes, revision
// labels, cancellation, explicit limits and live-document integrity.
#include "ark/exact_jobs_046b.hpp"
#include "ark/agent_activity.hpp"
#include "ark/native_backend.hpp"
#include "ark/serialize.hpp"
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace ark;
using V=arkjson::Value;
using Clock=std::chrono::steady_clock;
static void require(bool pass,const std::string& message){if(!pass)throw std::runtime_error(message);}
static V parse(const std::string& text){V value;require(arkjson::parse(text,value),"fixture JSON");return value;}
static V data(const V& result){require(result.find("structuredContent")!=nullptr,"result without structuredContent");return *result.find("structuredContent");}
static V applyCommands(AgentSession& session,const std::string& commands){V args=V::makeObj();args.set("commands",parse(commands));args.set("expected_revision",V::makeNum((double)session.revision));auto result=session.call("doc_apply",args);require(!result.find("isError")->b,"fixture apply rejected: "+data(result).dump());return data(result);}
static double ms(Clock::time_point from){return std::chrono::duration<double,std::milli>(Clock::now()-from).count();}
struct Waiter {std::promise<ExactJobScheduler046B::Completion> promise;std::future<ExactJobScheduler046B::Completion> future=promise.get_future();
    ExactJobScheduler046B::Completion get(int seconds=60){require(future.wait_for(std::chrono::seconds(seconds))==std::future_status::ready,"exact job did not complete");return future.get();}};
static ExactJobScheduler046B::Request job(const AgentSession& live,const std::string& tool,const std::string& args,Waiter& waiter,std::function<bool()> cancelled={}){
    ExactJobScheduler046B::Request request;request.tool=tool;request.args=parse(args);request.session=captureExactSession046B(live.document,live.revision);request.revision=live.revision;request.epoch=7;request.cancelled=std::move(cancelled);
    request.done=[&waiter](ExactJobScheduler046B::Completion done){waiter.promise.set_value(std::move(done));};return request;
}
static std::string code(const V& result){const auto payload=data(result);return payload.find("error_code")?payload.find("error_code")->asStr():std::string();}
// Two lofted freeform hulls through 32-anchor splines, placed so they interfere.
static void freeformFixture(AgentSession& session){
    V commands=V::makeArr();auto add=[&](const V& command){commands.arr.push_back(command);};
    for(int body=0;body<2;++body){V sections=V::makeArr();
        for(int s=0;s<5;++s){const std::string tag=std::string(body?"B":"A")+std::to_string(s);
            add(parse(R"({"op":"plane.set","name":"P)"+tag+R"(","p":{"oz":")"+std::to_string(18*s)+R"("}})"));
            V dof=V::makeArr();const double scale=20+8*std::sin(s*1.1),phase=s*.7+body;
            for(int i=0;i<32;++i){const double t=2*3.141592653589793*i/32,r=scale*(1+.18*std::sin(5*t+phase)+.07*std::cos(9*t));dof.arr.push_back(V::makeNum(std::round(r*std::cos(t)*1.6*1e4)/1e4));dof.arr.push_back(V::makeNum(std::round(r*std::sin(t)*1e4)/1e4));}
            V entity=parse(R"({"name":"skin","kind":"spline","closed":true})");entity.set("dof",dof);V entities=V::makeArr();entities.arr.push_back(entity);
            V sketch=V::makeObj();sketch.set("plane",V::makeStr("P"+tag));sketch.set("entities",entities);V command=parse(R"({"op":"sketch.set"})");command.set("name",V::makeStr("S"+tag));command.set("sketch",sketch);add(command);sections.arr.push_back(V::makeStr("S"+tag));}
        V loft=parse(R"({"op":"feature.add","type":"loft","p":{"solid":"1","ruled":"0","smooth":"1"}})");loft.set("name",V::makeStr(body?"Hull B":"Hull A"));V native=V::makeObj();native.set("sections",sections);loft.set("native",native);add(loft);}
    add(parse(R"({"op":"assembly.add","name":"Hull A","feature":1})"));add(parse(R"({"op":"assembly.add","name":"Hull B","feature":2,"p":{"tx":"72","ty":"6","tz":"4","rz":"7"}})"));
    applyCommands(session,commands.dump());
}
int main(){try{
    // Allow-list: derived from the reviewed read-only handlers; writes and
    // anything persisting into the live session stay on the owner thread.
    for(const char* tool:{"native_inspect","geometry_query","reference_inspect","assembly_info","collision_inspect","physics_inspect","mesh_inspect","motion_info","step_inspect"})require(exactJobAllowed046B(tool,V::makeObj()),std::string("allowed read rejected: ")+tool);
    for(const char* tool:{"doc_apply","doc_preview","doc_new","doc_load","doc_save","doc_undo","doc_redo","iteration_restore","import_step","export_step","export_mesh","snapshot","selection_set","context_graph","bom_inspect","bom_review","memory_record","tool_feedback","design_containers","library_insert","sketch_repair","placement_plan","manufacturing_propose","failure_replay","doc_info","object_context","agent_onboard","usage_record","plane_authoring","drawing_export"})require(!exactJobAllowed046B(tool,V::makeObj()),std::string("write or session-state tool allowed: ")+tool);
    require(!exactJobAllowed046B("native_inspect",V::makeArr()),"non-object arguments allowed");
    require(exactJobAllowed046B("query_batch",parse(R"({"queries":[{"tool":"native_inspect","args":{"id":1}},{"tool":"assembly_info","args":{"detail":"result","clearance":{"a":1,"b":2}}}]})")),"exact query batch rejected");
    require(!exactJobAllowed046B("query_batch",parse(R"({"queries":[{"tool":"native_inspect","args":{"id":1}},{"tool":"doc_info","args":{}}]})")),"batch with a non-exact row allowed");
    require(!exactJobAllowed046B("query_batch",parse(R"({"queries":[{"tool":"query_batch","args":{"queries":[]}}]})")),"nested batch allowed");
    require(!exactJobAllowed046B("query_batch",parse(R"({"queries":[]})")),"empty batch allowed");

    AgentSession live;applyCommands(live,R"([{"op":"feature.add","type":"box","name":"Probe","p":{"dx":"10","dy":"10","dz":"10"}}])");
    recordAgentActivity(live.document,"doc_info",V::makeObj(),parse(R"({"kind":"client","id":"live_mcp"})"),parse(R"({"isError":false,"structuredContent":{}})"),live.revision,live.revision,1);require(live.document.agentActivity!=nullptr,"fixture ledger");
    ExactJobScheduler046B scheduler(1);
    {bool rejected=false;Waiter waiter;try{scheduler.submit(job(live,"doc_apply",R"({"commands":[]})",waiter));}catch(const std::exception& error){rejected=std::string(error.what()).find("EXACT_JOB_NOT_ALLOWED")!=std::string::npos;}require(rejected,"scheduler accepted a write tool");}
    {auto copy=captureExactSession046B(live.document,live.revision);require(!copy->document.agentActivity&&!copy->document.pendingNativeGeometry&&copy->revision==live.revision,"snapshot shares the live activity ledger or staged geometry");
        Document expected=live.document;expected.agentActivity.reset();require(documentToJson(copy->document)==documentToJson(expected),"snapshot authoring differs from the live document");}

    // Ordering: owner-thread FIFO [write, read] -> the read sees the write;
    // [read, write] -> the read keeps its own revision label.
    const auto ledgerSerial=live.document.agentActivity->serial;
    Waiter before;scheduler.setTestDelayMs(300);const auto r1=live.revision;scheduler.submit(job(live,"mesh_inspect",R"({"id":1})",before));
    applyCommands(live,R"([{"op":"feature.edit","id":1,"p":{"dz":"30"}}])");const auto r2=live.revision;const auto liveAfterWrite=documentToJson(live.document);
    Waiter after;scheduler.submit(job(live,"mesh_inspect",R"({"id":1,"actor":{"kind":"client","id":"live_mcp"}})",after));scheduler.setTestDelayMs(0);
    auto first=before.get(),second=after.get();
    auto box=[](const V& result){return data(result).find("bounds_mm")?data(result).find("bounds_mm")->dump():data(result).dump();};
    require(!first.cancelled&&!first.result.find("isError")->b&&!second.result.find("isError")->b,"exact reads failed: "+first.result.dump()+second.result.dump());
    require(first.revision==r1&&data(first.result).find("observed_revision")->num==(double)r1&&data(first.result).find("exact_job")->find("observed_revision")->num==(double)r1,"read issued before the write lost its revision label");
    require(second.revision==r2&&data(second.result).find("observed_revision")->num==(double)r2,"read issued after the write did not observe it");
    auto volume=[](const V& result){const auto* value=data(result).find("signed_volume_mm3");return value&&value->t==V::Num?value->num:-1.;};
    require(std::abs(volume(first.result)-1000)<1e-3&&std::abs(volume(second.result)-3000)<1e-3,"reads did not see their own revision geometry: "+box(first.result)+" / "+box(second.result));
    const auto label=*data(second.result).find("exact_job");require(label.find("execution")->asStr()=="background_snapshot"&&label.find("native_request_limit_ms")->num==120000&&label.find("job_limit_ms")->num==600000,"exact job label incomplete");
    require(second.result.find("content")->arr.front().find("text")->asStr()==data(second.result).dump(),"text content differs from the labelled structured result");
    require(!data(second.result).find("activity_id")&&live.document.agentActivity->serial==ledgerSerial,"job wrote activity through a copied ledger");
    require(documentToJson(live.document)==liveAfterWrite&&live.revision==r2,"background read changed the live document");

    // UI-thread copy cost indicator: 128 evaluated analytic bodies in one document.
    {AgentSession big;V commands=V::makeArr();for(int i=0;i<128;++i)commands.arr.push_back(parse(R"({"op":"feature.add","type":"box","name":"Copy probe )"+std::to_string(i)+R"(","p":{"dx":"10","dy":"12","dz":"14"}})"));applyCommands(big,commands.dump());
        const auto copied=Clock::now();for(int i=0;i<10;++i)require(captureExactSession046B(big.document,big.revision)->document.evalBodies.size()==128,"copied bodies");const double each=ms(copied)/10;
        std::cout<<"snapshot copy, 128 evaluated boxes: "<<each<<" ms"<<std::endl;require(each<1000,"snapshot copy exceeds one second");}

    // Cancellation: queued, running, external predicate. No partial results.
    {Waiter running,queued;scheduler.setTestDelayMs(5000);const auto started=Clock::now();const auto id=scheduler.submit(job(live,"mesh_inspect",R"({"id":1})",running));const auto waiting=scheduler.submit(job(live,"mesh_inspect",R"({"id":1})",queued));
        require(scheduler.active()==2,"jobs not tracked");std::this_thread::sleep_for(std::chrono::milliseconds(50));auto rows=scheduler.status();require(rows.size()==2&&rows.front().running&&!rows.back().running,"status does not distinguish running and queued jobs");
        require(scheduler.cancel(waiting),"queued cancel");const auto requested=Clock::now();require(scheduler.cancel(id),"running cancel");auto stopped=running.get(5),skipped=queued.get(5);const double latency=ms(requested);
        require(stopped.cancelled&&code(stopped.result)=="CANCELLED"&&skipped.cancelled&&code(skipped.result)=="CANCELLED","cancelled jobs returned results");
        require(latency<500&&ms(started)<2000,"cancel latency "+std::to_string(latency)+" ms");std::cout<<"test-hook cancel latency "<<latency<<" ms\n";
        require(!scheduler.cancel(id)&&scheduler.active()==0,"finished job still cancellable");}
    {Waiter stopped;std::atomic_bool flag{false};scheduler.setTestDelayMs(5000);scheduler.submit(job(live,"mesh_inspect",R"({"id":1})",stopped,[&flag]{return flag.load();}));std::this_thread::sleep_for(std::chrono::milliseconds(30));flag=true;auto done=stopped.get(5);require(done.cancelled&&code(done.result)=="CANCELLED","external stop predicate ignored");}
    scheduler.setTestDelayMs(0);

    // Native exact reads: the worker channel is per job thread; cancel stops it.
    bool native=false;try{native=!live.call("kernel_info",V::makeObj()).find("isError")->b;}catch(...){}
    if(native){
        AgentSession hulls;const auto fixtureStarted=Clock::now();freeformFixture(hulls);std::cout<<"freeform fixture "<<ms(fixtureStarted)<<" ms\n";
        const std::string clearance=R"({"detail":"result","clearance":{"a":1,"b":2}})";
        Waiter exact;auto started=Clock::now();ExactJobScheduler046B pool(2);pool.submit(job(hulls,"assembly_info",clearance,exact));auto measured=exact.get(120);const double exactMs=ms(started);
        require(!measured.result.find("isError")->b&&data(measured.result).find("clearance_checked")->b&&data(measured.result).find("clearance_authority")->asStr()=="exact_brep","background clearance failed: "+measured.result.dump());
        std::cout<<"background exact clearance "<<exactMs<<" ms (run "<<measured.runMs<<" ms)\n";
        // Cancel inside one native request: the isolated worker process is stopped.
        Waiter cancelled;started=Clock::now();const auto id=pool.submit(job(hulls,"query_batch",R"({"queries":[{"tool":"assembly_info","args":)"+clearance+R"(},{"tool":"assembly_info","args":)"+clearance+R"(},{"tool":"assembly_info","args":)"+clearance+R"(}]})",cancelled));
        std::this_thread::sleep_for(std::chrono::milliseconds(700));const auto requested=Clock::now();pool.cancel(id);auto stopped=cancelled.get(30);const double latency=ms(requested);
        require(stopped.cancelled&&code(stopped.result)=="CANCELLED"&&latency<1500,"native cancel latency "+std::to_string(latency)+" ms: "+stopped.result.dump());std::cout<<"native cancel latency "<<latency<<" ms after "<<ms(started)-latency<<" ms\n";
        Waiter again;pool.submit(job(hulls,"assembly_info",clearance,again));auto retried=again.get(120);require(!retried.result.find("isError")->b&&data(retried.result).find("clearance_mm")->num==data(measured.result).find("clearance_mm")->num,"worker did not restart identically after a cancelled request");
        // Explicit per-request limit; the timeout keeps RESOURCE_LIMIT wording.
        {NativeRequestControlScope bounded({},1000);require(nativeRequestLimitMs()==1000,"scoped limit not applied");auto copy=captureExactSession046B(hulls.document,hulls.revision);auto limited=copy->call("assembly_info",parse(clearance));
            require(limited.find("isError")->b&&code(limited)=="RESOURCE_LIMIT"&&data(limited).find("error")->asStr().find("exceeded 1-second worker limit")!=std::string::npos,"scoped native limit did not report RESOURCE_LIMIT: "+limited.dump());}
        require(nativeRequestLimitMs()==30000,"default 30-second limit not restored");
        // Concurrent writes on the owner while two exact reads run.
        Waiter a,b;pool.submit(job(hulls,"assembly_info",clearance,a));pool.submit(job(hulls,"native_inspect",R"({"id":1,"detail":"properties"})",b));const auto observed=hulls.revision;
        applyCommands(hulls,R"([{"op":"assembly.edit","id":2,"p":{"tx":"140"}}])");applyCommands(hulls,R"([{"op":"param.set","name":"probe","expr":"3"}])");const auto expected=documentToJson(hulls.document);
        auto ra=a.get(120),rb=b.get(120);require(!ra.result.find("isError")->b&&!rb.result.find("isError")->b&&ra.revision==observed&&rb.revision==observed,"concurrent reads failed");
        require(data(ra.result).find("clearance_mm")->num==data(measured.result).find("clearance_mm")->num,"read observed a later write");
        require(documentToJson(hulls.document)==expected&&hulls.revision==observed+2,"concurrent read changed the live document");
        Waiter fresh;pool.submit(job(hulls,"assembly_info",clearance,fresh));auto moved=fresh.get(120);require(data(moved.result).find("clearance_mm")->num>0&&moved.revision==hulls.revision,"read after writes did not observe them: "+moved.result.dump());
    }else std::cout<<"native exact cases not run: no OCCT worker in this build\n";

    // Shutdown completes queued work as cancelled and rejects later submissions.
    {Waiter one,two;scheduler.setTestDelayMs(5000);scheduler.submit(job(live,"mesh_inspect",R"({"id":1})",one));scheduler.submit(job(live,"mesh_inspect",R"({"id":1})",two));scheduler.shutdown();
        require(one.get(5).cancelled&&two.get(5).cancelled,"shutdown left jobs incomplete");bool rejected=false;Waiter late;try{scheduler.submit(job(live,"mesh_inspect",R"({"id":1})",late));}catch(const std::exception&){rejected=true;}require(rejected,"submission after shutdown");}
    std::cout<<"async exact 046B: allow-list, ordering, labels, cancellation, limits and integrity passed\n";return 0;
}catch(const std::exception& error){std::cerr<<"async exact 046B failed: "<<error.what()<<'\n';return 1;}}
