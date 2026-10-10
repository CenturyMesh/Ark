#include "ark/agent.hpp"
#include <cmath>
#include <iostream>

using namespace ark;
static int checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';++failures;}}while(0)
static arkjson::Value parse(const char* source){arkjson::Value v;CHECK(arkjson::parse(source,v));return v;}
static bool bad(const arkjson::Value& result){auto* field=result.find("isError");return !field||field->b;}

static Document basic(){
    Document d;Feature part;part.type=FeatureType::Box;part.p={{"dx","10"},{"dy","10"},{"dz","10"}};
    d.addFeature(part);d.recompute();
    d.assembly.push_back({1,0,0,"Group",{},true});
    d.assembly.push_back({2,1,1,"Anchor",{{"tx","10"}},true,true});
    d.assembly.push_back({3,1,1,"Moving",{{"tx","-20"},{"ty","8"},{"tz","4"}},true});
    return d;
}
static Vec3 placed(const std::vector<AssemblyPlacement>& p,u64 node){
    for(const auto& e:p)if(e.node==node)return e.world.transformPoint({0,0,0});
    return {};
}
static AssemblyMate mate(u64 id,const char* type,const char* ra="origin",const char* rb="origin",const char* value="0"){
    AssemblyMate m;m.id=id;m.a=2;m.b=3;m.type=type;m.reference_a=ra;m.reference_b=rb;m.value=value;return m;
}
int main(){
    std::vector<AssemblyPlacement> placements;std::string error;AssemblyDiagnostics diag;
    {
        auto d=basic();CHECK(resolveAssembly(d,placements,error,&diag));CHECK(placements.size()==2);
        CHECK(placed(placements,2).approx({10,0,0},1e-9));
        CHECK(placed(placements,3).approx({-20,8,4},1e-9));
        CHECK(diag.freeDofs==6&&diag.unconstrainedNodes.size()==1&&diag.unconstrainedNodes[0]==3);
    }
    {
        auto d=basic();d.mates.push_back(mate(1,"coincident"));
        CHECK(resolveAssembly(d,placements,error,&diag));if(!error.empty())std::cerr<<error<<'\n';
        CHECK(placed(placements,3).approx({10,0,0},1e-5));
        CHECK(diag.rank==5&&diag.freeDofs==1);
        CHECK(diag.mates.size()==1&&diag.maxPositionMm<1e-5&&diag.maxAngleDeg<1e-4);
        CHECK(d.assembly[2].p.at("tx")=="-20");
    }
    {
        auto d=basic();d.assembly[2].p["rz"]="137";d.mates.push_back(mate(1,"revolute"));
        CHECK(resolveAssembly(d,placements,error,&diag));
        CHECK(diag.rank==5&&diag.freeDofs==1);
        CHECK(placed(placements,3).approx({10,0,0},1e-5));
        for(const auto& row:placements)if(row.node==3)CHECK(row.world.transformDir({1,0,0}).approx({std::cos(deg2rad(137)),std::sin(deg2rad(137)),0},1e-7));
    }
    {
        auto d=basic();d.assembly[2].p["rz"]="180";d.mates.push_back(mate(1,"rigid"));
        CHECK(resolveAssembly(d,placements,error,&diag));
        CHECK(diag.rank==6&&diag.freeDofs==0);
        CHECK(placed(placements,3).approx({10,0,0},1e-5));
        for(const auto& row:placements)if(row.node==3)CHECK(row.world.transformDir({1,0,0}).approx({1,0,0},1e-7));
        CHECK(diag.maxPositionMm<1e-5&&diag.maxAngleDeg<1e-4);
    }
    {
        auto d=basic();d.mates.push_back(mate(1,"concentric","X","X"));
        d.mates.push_back(mate(2,"distance","X","X","gap"));
        d.setParam("gap","40");
        CHECK(resolveAssembly(d,placements,error,&diag));if(!error.empty())std::cerr<<error<<'\n';
        CHECK(placed(placements,3).approx({50,0,0},1e-5));
        CHECK(diag.rank==5&&diag.freeDofs==1);
        d.setParam("gap","55");d.recompute();
        CHECK(resolveAssembly(d,placements,error,&diag));if(!error.empty())std::cerr<<error<<'\n';
        CHECK(placed(placements,3).approx({65,0,0},1e-5));
    }
    {
        auto d=basic();d.assembly[2].p={{"tx","10"},{"tz","4"},{"rx","180"}};
        d.mates.push_back(mate(1,"contact"));
        CHECK(resolveAssembly(d,placements,error,&diag));if(!error.empty())std::cerr<<error<<'\n';
        CHECK(std::fabs(placed(placements,3).z)<1e-5);
        CHECK(diag.rank==3&&diag.freeDofs==3);
    }
    {
        auto d=basic();d.assembly[2].p={{"tx","10"},{"tz","4"}};
        d.mates.push_back(mate(1,"contact"));
        CHECK(resolveAssembly(d,placements,error,&diag));if(!error.empty())std::cerr<<error<<'\n';
        CHECK(std::fabs(placed(placements,3).z)<1e-5);
        CHECK(diag.maxAngleDeg<1e-4);
    }
    {
        auto d=basic();d.assembly[2].p={{"tx","10"},{"rx","180"}};
        d.mates.push_back(mate(1,"concentric"));
        d.mates.push_back(mate(2,"coincident"));
        CHECK(resolveAssembly(d,placements,error,&diag));if(!error.empty())std::cerr<<error<<'\n';
        CHECK(diag.maxAngleDeg<1e-4&&diag.maxPositionMm<1e-5);
    }
    {
        auto d=basic();d.assembly[2].p={{"rx","70"}};
        d.mates.push_back(mate(1,"angle","Z","Z","90"));
        CHECK(resolveAssembly(d,placements,error,&diag));if(!error.empty())std::cerr<<error<<'\n';
        CHECK(diag.rank==1&&diag.freeDofs==5);
        CHECK(diag.maxAngleDeg<1e-4);
    }
    {
        auto d=basic();d.assembly[2].fixed=true;d.mates.push_back(mate(1,"coincident"));
        CHECK(!resolveAssembly(d,placements,error,&diag));CHECK(error.find("Unsatisfied")!=std::string::npos);
        CHECK(placements.empty());
    }
    {
        auto d=basic();d.assembly[1].fixed=false;d.mates.push_back(mate(1,"coincident"));
        CHECK(!resolveAssembly(d,placements,error));CHECK(error.find("fixed anchor")!=std::string::npos);
    }
    {
        auto d=basic();d.mates.push_back(mate(1,"distance"));d.mates[0].b=999;
        CHECK(!resolveAssembly(d,placements,error));CHECK(error.find("invalid assembly instance")!=std::string::npos);
    }
    {
        auto d=basic();
        d.assembly.push_back({4,0,0,"Arm",{},true});
        d.assembly[2].parent=4;
        d.assembly.push_back({5,4,1,"Sibling",{{"tx","10"}},true});
        d.mates.push_back(mate(1,"coincident"));
        CHECK(resolveAssembly(d,placements,error,&diag));if(!error.empty())std::cerr<<error<<'\n';
        CHECK(placed(placements,3).approx({10,0,0},1e-5));
        CHECK(placed(placements,5).approx({40,-8,-4},1e-5));
        CHECK(placed(placements,2).approx({10,0,0},1e-9));
        CHECK(diag.rank==5&&diag.freeDofs==1);
        arkjson::Value saved;CHECK(arkjson::parse(documentToJson(d),saved));
        CHECK(saved.find("version")&&saved.find("version")->num==14);
    }
    {
        auto d=basic();d.mates.push_back(mate(1,"distance","Z","Z","invalid_parameter"));
        CHECK(!resolveAssembly(d,placements,error));CHECK(error.find("Invalid assembly mate value")!=std::string::npos);
    }
    {
        AgentSession session;
        const auto create=parse(R"({"commands":[
            {"op":"feature.add","type":"box","name":"Shared","p":{"dx":"10","dy":"10","dz":"10"}},
            {"op":"assembly.add","name":"Assembly"},
            {"op":"assembly.add","name":"Anchor","parent":1,"feature":1,"fixed":true,"p":{"tx":"10"}},
            {"op":"assembly.add","name":"Follower","parent":1,"feature":1,"p":{"tx":"-20"}},
            {"op":"mate.add","a":2,"b":3,"type":"coincident"}
        ]})");
        CHECK(!bad(session.call("doc_apply",create)));
        auto info=session.call("assembly_info",arkjson::Value::makeObj());
        CHECK(!bad(info));
        auto* payload=info.find("structuredContent");
        CHECK(payload&&payload->find("bom")->arr[0].find("quantity")->num==2);
        CHECK(payload&&payload->find("diagnostics"));
        auto saved=documentToJson(session.document);
        Document loaded;CHECK(documentFromJson(saved,loaded,&error));
        CHECK(loaded.mates.size()==1&&loaded.assembly[1].fixed);
        CHECK(resolveAssembly(loaded,placements,error,&diag));
        CHECK(placed(placements,3).approx({10,0,0},1e-5));
        auto reject=session.call("doc_apply",parse(R"({"commands":[{"op":"mate.add","a":2,"b":3,"type":"distance","value":"10"}]})"));
        CHECK(bad(reject));CHECK(documentToJson(session.document)==saved);
    }
#ifdef ARK_ASSEMBLY_EXACT_TESTS
    {
        Document d;d.setParam("height","30");
        Feature cylinder;cylinder.type=FeatureType::Cylinder;
        cylinder.p={{"r","5"},{"h","height"}};d.addFeature(cylinder);d.recompute();
        d.assembly.push_back({1,0,0,"Group",{},true});
        d.assembly.push_back({2,1,1,"Axis anchor",{{"tx","10"}},true,true});
        d.assembly.push_back({3,1,1,"Regenerating follower",{{"tx","-20"},{"ty","-10"},{"tz","7"}},true});
        d.mates.push_back(mate(1,"concentric","axis","axis"));
        d.mates.push_back(mate(2,"contact","cap.end","cap.start"));
        CHECK(resolveAssembly(d,placements,error,&diag));if(!error.empty())std::cerr<<error<<'\n';
        CHECK(placed(placements,3).approx({10,30,0},1e-4));
        d.setParam("height","50");d.recompute();
        CHECK(resolveAssembly(d,placements,error,&diag));if(!error.empty())std::cerr<<error<<'\n';
        CHECK(placed(placements,3).approx({10,50,0},1e-4));
        d.mates[0].reference_a="axis.deleted";
        CHECK(!resolveAssembly(d,placements,error));CHECK(error.find("reference")!=std::string::npos);
    }
#endif
    std::cout<<checks<<" checks / "<<failures<<" failures\n";
    return failures?1:0;
}
