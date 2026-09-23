// ARK MCP stdio host. All modeling operations live in AgentSession.
#include "ark/agent.hpp"
#include "ark/live_session.hpp"
#include <iostream>
using V=arkjson::Value;
int main(int argc,char** argv){
    std::string endpoint;
    if(argc==3&&std::string(argv[1])=="--connect"&&argv[2][0])endpoint=argv[2];
    else if(argc!=1){std::cerr<<"Usage: ark-mcp [--connect <Studio pipe name>]\n";return 2;}
    std::ios::sync_with_stdio(false);ark::cpu_detect();ark::AgentSession session;
    std::string line;
    while(std::getline(std::cin,line)){
        V req,response=V::makeObj();response.set("jsonrpc",V::makeStr("2.0"));
        auto error=[&](int code,const char* message){V e=V::makeObj();e.set("code",V::makeNum(code));e.set("message",V::makeStr(message));response.set("error",e);};
        if(line.size()>4*1024*1024||!arkjson::parse(line,req)){response.set("id",V{});error(-32700,"Parse error or request exceeds 4 MiB");}
        else if(req.t!=V::Obj||!req.find("jsonrpc")||req.find("jsonrpc")->asStr()!="2.0"||!req.find("method")||req.find("method")->t!=V::Str){response.set("id",V{});error(-32600,"Invalid Request");}
        else {
            if(!req.find("id"))continue; // JSON-RPC notifications never receive responses.
            response.set("id",*req.find("id"));auto method=req.find("method")->str;
            if(method=="initialize"){
                V r=V::makeObj(),caps=V::makeObj(),info=V::makeObj();
                std::string protocol="2025-11-25";
                if(auto p=req.find("params"))if(auto v=p->find("protocolVersion"))
                    if(v->asStr()=="2025-06-18"||v->asStr()=="2024-11-05")protocol=v->str;
                r.set("protocolVersion",V::makeStr(protocol));caps.set("tools",V::makeObj());r.set("capabilities",caps);
                info.set("name",V::makeStr("ark"));info.set("version",V::makeStr("0.8.0"));r.set("serverInfo",info);
                r.set("instructions",V::makeStr("Call agent_onboard first to discover ARK capabilities, current document/revision, modeling contracts and validation limits."));
                response.set("result",r);
            }else if(method=="ping")response.set("result",V::makeObj());
            else if(method=="tools/list"){V r=V::makeObj();r.set("tools",ark::AgentSession::tools());response.set("result",r);}
            else if(method=="tools/call"){
                auto p=req.find("params");auto name=p?p->find("name"):nullptr;
                if(!name||name->t!=V::Str)error(-32602,"Missing tool name");
                else {
                    auto a=p->find("arguments");
                    try {response.set("result",endpoint.empty()?session.call(name->str,a?*a:V::makeObj()):ark::callLiveSession(endpoint,name->str,a?*a:V::makeObj()));}
                    catch(const std::exception& e){error(-32001,e.what());}
                }
            }else error(-32601,"Method not found");
        }
        std::cout<<response.dump()<<'\n'<<std::flush;
    }
}
