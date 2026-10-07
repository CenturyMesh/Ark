#include "ark/stock_preview.hpp"
#include "ark/tube.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using arkjson::Value;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
Value parse(const char* text){Value value;if(!arkjson::parse(text,value))throw std::runtime_error("Bad stock preview test JSON");return value;}
bool fails(const ark::Feature& feature,const ark::ExprEnv& env,const Value& data){try{(void)ark::buildStockPreview(feature,env,data);return false;}catch(const std::exception&){return true;}}
}
int main(){
    try{
        ark::Feature tube;tube.type=ark::FeatureType::Tube;tube.id=1;tube.name="Bent round tube";tube.p={{"section","round"},{"od","30"},{"wall","2"},{"tail","50"}};tube.bends.push_back({"100","60","90","0"});
        const auto original=ark::buildTube(tube,{});check(original.error.empty(),"authored tube evaluates");
        auto straight=ark::buildStockPreview(tube,{},Value{});
        check(!straight.mesh.pos.empty()&&straight.mesh.triCount()>0,"straight tube display mesh built");
        check(std::abs(straight.report.find("total_developed_length_mm")->num-original.centerlineLength)<1e-7,"tube blank uses developed centerline length");
        check(straight.report.find("authority")->str=="display_only"&&!straight.report.find("manufacturing_qualified")->b&&!straight.report.find("cope_trim_preserved")->b,"stock preview states authority and trim limits");
        check(tube.bends.size()==1&&tube.p.at("tail")=="50","tube authoring is unchanged");
        auto box=straight.mesh.computeBBox();check(std::abs(box.center().x)<1e-3&&std::abs(box.extent().x-original.centerlineLength)<1e-3,"straight blank is centered along X");

        ark::Feature frame;frame.type=ark::FeatureType::ConstructionFrame;frame.id=2;frame.name="Chassis";frame.nativeSpec=parse(R"({"profiles":[{"name":"round","section":"round","od":"40","wall":"2"},{"name":"rect","section":"rect","width":"60","height":"30","wall":"3"}]})");
        Value evaluated=parse(R"({"frame_report":{"members":[{"name":"left rail","profile":"round","developed_length_mm":120},{"name":"cross beam","profile":"rect","developed_length_mm":200}],"total_developed_length_mm":320}})");
        auto rows=ark::buildStockPreview(frame,{},evaluated);check(rows.report.find("member_count")->num==2&&rows.report.find("total_developed_length_mm")->num==320,"frame stock uses evaluated member cut lengths");
        const auto& members=rows.report.find("members")->arr;check(members[0].find("source_name")->str=="left rail"&&members[1].find("source_name")->str=="cross beam","stock rows preserve source names");
        check(members[1].find("row_y_mm")->num>=130,"frame rows use profile width plus 80 mm gap");
        check(rows.mesh.triCount()>straight.mesh.triCount(),"all frame members appear in nominal mesh");
        check(frame.nativeSpec.find("profiles")->arr.size()==2,"frame source authoring unchanged");

        Value missing=parse(R"({"frame_report":{"members":[{"name":"bad","profile":"missing","developed_length_mm":30}]}})");check(fails(frame,{},missing),"missing profile rejects preview");
        Value negative=parse(R"({"frame_report":{"members":[{"name":"bad","profile":"round","developed_length_mm":-1}]}})");check(fails(frame,{},negative),"nonpositive evaluated length rejects preview");
        ark::Feature unrelated;unrelated.type=ark::FeatureType::Box;check(fails(unrelated,{},Value{}),"unrelated feature rejects stock preview");
        std::cout<<"stock preview tests passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
