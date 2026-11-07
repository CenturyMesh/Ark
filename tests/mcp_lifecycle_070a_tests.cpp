#include "ark/live_session.hpp"
#include "native_process.hpp"
#include <atomic>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>

using namespace std::chrono_literals;
using V=arkjson::Value;
static int checks=0;
static void check(bool value,const std::string& message){++checks;native_test::check(value,message);}
static V json(const std::string& text){V result;check(arkjson::parse(text,result),"Invalid fixture JSON");return result;}
static std::vector<V> replies(const native_test::Process& child){std::vector<V> result;std::istringstream in(child.output());std::string line;while(std::getline(in,line))if(!line.empty())result.push_back(json(line));return result;}
static V success(){V result=V::makeObj(),data=V::makeObj();data.set("fixture",V::makeStr("owned-live-bridge"));result.set("isError",V::makeBool(false));result.set("structuredContent",std::move(data));return result;}

int main(int argc,char** argv){
    try{
        check(argc==2,"Expected ark-mcp executable path");
#ifdef _WIN32
        const auto exe=std::filesystem::absolute(argv[1]);
        native_test::TempDirectory temp("ark-mcp-lifecycle-");
        const auto work=temp.path();
        const auto missing="ark-lifecycle-missing-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
        // Parent stdin intentionally stays open in every lifecycle check.
        native_test::Process standalone(exe,{},work),memory(exe,{"--memory-project","synthetic-lifecycle-070a"},work),absent(exe,{"--connect",missing},work);
        standalone.write(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"doc_apply","arguments":{"expected_revision":0,"commands":[{"op":"param.set","name":"retained","expr":"42"}]}}})" "\n");
        check(absent.wait(6000ms)==0,"An absent endpoint did not end its idle proxy");
        check(absent.error().find("endpoint remained absent")!=std::string::npos,"Absent endpoint shutdown omitted actionable diagnostics");
        check(standalone.running()&&memory.running(),"Idle standalone or memory host was terminated");
        standalone.write(R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"eval_expr","arguments":{"expr":"retained"}}})" "\n");
        standalone.closeInput();memory.closeInput();check(standalone.wait(5000ms)==0&&memory.wait(5000ms)==0,"Standalone host failed normal stdin shutdown");
        const auto authored=replies(standalone);check(authored.size()==2&&!authored[0].find("result")->find("isError")->b,"Standalone authored document did not survive idle time");
        check(authored[1].find("result")->find("structuredContent")->find("value")->num==42,"Standalone state was discarded");

        ark::LiveSessionBridge bridge;
        check(bridge.start([](auto&,auto&,auto done,std::stop_token){done(success());}),"Cannot start owned bridge");
        const auto endpoint=bridge.endpoint();
        native_test::Process partial(exe,{"--connect",endpoint},work);
        partial.write("{\"jsonrpc\":\"2.0\",\"id\":1,");
        std::this_thread::sleep_for(3500ms);
        check(partial.running(),"An active endpoint with fragmented stdin was terminated");
        partial.write("\"method\":\"ping\"}\r\n{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}\n");
        partial.closeInput();check(partial.wait(5000ms)==0,"Fragmented CRLF requests failed");
        check(replies(partial).size()==2,"Buffered CRLF/multiple requests changed JSON-RPC framing");

        native_test::Process transient(exe,{"--connect",endpoint},work);
        std::this_thread::sleep_for(300ms);bridge.stop();std::this_thread::sleep_for(700ms);
        check(bridge.start([](auto&,auto&,auto done,std::stop_token){done(success());},endpoint),"Cannot restore the same owned endpoint");
        std::this_thread::sleep_for(3500ms);check(transient.running(),"A transient endpoint disappearance killed the connection");
        bridge.stop();check(transient.wait(6000ms)==0,"Closed bridge left idle proxy with open parent stdin");

        // All four native instances can be busy while a fifth proxy is idle.
        std::mutex mutex;std::condition_variable changed;std::vector<ark::LiveSessionBridge::Completion> held;
        check(bridge.start([&](auto&,auto&,auto done,std::stop_token){std::lock_guard lock(mutex);held.push_back(done);changed.notify_all();}),"Cannot start busy fixture");
        const auto busyEndpoint=bridge.endpoint();std::vector<std::future<V>> calls;
        for(int i=0;i<4;++i)calls.push_back(std::async(std::launch::async,[&]{return ark::callLiveSession(busyEndpoint,"fixture",V::makeObj(),10000);}));
        {std::unique_lock lock(mutex);check(changed.wait_for(lock,5s,[&]{return held.size()==4;}),"Busy fixture did not occupy all native instances");}
        native_test::Process busy(exe,{"--connect",busyEndpoint},work);
        std::this_thread::sleep_for(3500ms);check(busy.running(),"Busy endpoint was treated as absent");
        {std::lock_guard lock(mutex);for(auto& complete:held)complete(success());}
        for(auto& call:calls)check(!call.get().find("isError")->b,"Busy native call failed");
        busy.closeInput();check(busy.wait(5000ms)==0,"Busy proxy failed normal EOF");bridge.stop();

        std::promise<void> entered;std::jthread completing;std::atomic<int> dispatches=0;
        check(bridge.start([&](auto&,auto&,auto done,std::stop_token){++dispatches;completing=std::jthread([done]{std::this_thread::sleep_for(4500ms);done(success());});entered.set_value();}),"Cannot start in-flight fixture");
        native_test::Process inflight(exe,{"--connect",bridge.endpoint()},work);
        inflight.write(R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"fixture","arguments":{}}})" "\n");
        check(entered.get_future().wait_for(5s)==std::future_status::ready,"Forwarded request was not dispatched");
        std::this_thread::sleep_for(3500ms);check(inflight.running(),"In-flight call was interrupted by the idle lifecycle guard");
        completing.join();inflight.closeInput();check(inflight.wait(5000ms)==0,"In-flight result was not drained before EOF");
        const auto result=replies(inflight);check(dispatches==1&&result.size()==1&&result[0].find("result")->find("structuredContent")->find("fixture")->str=="owned-live-bridge","Forwarded call lost its one exact result or was replayed");bridge.stop();
#endif
        std::cout<<"MCP lifecycle 070A: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"MCP lifecycle 070A: "<<e.what()<<'\n';return 1;}
}
