#include "ark/native_backend.hpp"
#include <iostream>
#include <stdexcept>
#include <cstdlib>
using namespace ark;
using V=arkjson::Value;
static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(){try{
    const int old=setNativeThreadBudget(1);struct Restore {int value;~Restore(){setNativeThreadBudget(value);}} restore{old};
    const auto first=nativeKernelInfo();check(first.find("native_threads")->num==1,"serial allowance ignored");
    setNativeThreadBudget(4);const auto four=nativeKernelInfo();check(four.find("native_threads")->num==4,"parallel allowance ignored");
    setNativeThreadBudget(1);const auto last=nativeKernelInfo();check(last.find("native_threads")->num==1,"serial allowance not restored");
#ifdef _WIN32
    const char* mode=std::getenv("ARK_NATIVE_ONESHOT");if(!mode||std::string(mode)!="1")check(last.find("worker_process_starts")->num==first.find("worker_process_starts")->num,"budget switch restarted resident process");
#endif
    V model=V::makeObj(),request=V::makeObj();model.set("op",V::makeStr("box"));model.set("width",V::makeNum(20));model.set("depth",V::makeNum(15));model.set("height",V::makeNum(10));request.set("operation",V::makeStr("model_solid"));request.set("request",model);
    auto serial=nativeWorkerRequest(request);setNativeThreadBudget(4);auto parallel=nativeWorkerRequest(request);
    for(const auto* report:{serial.find("report"),parallel.find("report")}){check(report&&report->find("brep_valid")->b,"native solid invalid after budget switch");check(std::abs(report->find("volume_mm3")->num-3000)<1e-8,"thread allowance changed solid volume");}
    std::cout<<"Native request allowance 1/4/1: exact volume preserved, resident process retained\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
