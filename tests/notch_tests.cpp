#include "ark/notch.hpp"
#include "ark/core.hpp"
#include <iostream>
using V=arkjson::Value;
int main(){
    int checks=0;
    auto check=[&](bool b){++checks;if(!b)throw std::runtime_error("Notch check "+std::to_string(checks)+" failed");};
    V input;check(arkjson::parse(R"({"width_mm":40,"height_mm":30,"wall_mm":2,"length_mm":400,"hinge_offset_mm":1,"notches":[{"station_mm":200,"angle_deg":90,"hinge_face":"bottom"}]})",input));
    auto p=ark::planNotchedBlank(input);check(!p.find("manufacturing_authoritative")->b);
    check(std::fabs(p.find("notches")->arr[0].find("opening_mm")->num-58)<1e-10);
    check(p.find("validation")->find("cut_face_closure_max_mm")->num<1e-10);
    // Independent numerical cross-section integration of the removed material.
    double integral=0;int steps=100000;
    for(int i=0;i<steps;++i){double y=2+(i+.5)*28/steps;integral+=(y<28?4.:40.)*2*(y-1)*28/steps;}
    check(std::fabs(integral-p.find("validation")->find("removed_volume_mm3")->num)<.1);
    for(auto face:{"bottom","top","left","right"})for(double angle:{15.,45.,90.,135.}){
        V q=input,n=input.find("notches")->arr[0],ns=V::makeArr();n.set("angle_deg",V::makeNum(angle));n.set("hinge_face",V::makeStr(face));ns.arr.push_back(n);q.set("notches",ns);
        auto r=ark::planNotchedBlank(q);check(r.find("validation")->find("cut_face_closure_max_mm")->num<1e-10);check(r.find("validation")->find("cut_face_inverse_max_mm")->num<1e-10);
    }
    auto rejects=[&](V q){try{ark::planNotchedBlank(q);return false;}catch(...){return true;}};
    V q=input;q.set("wall_mm",V::makeNum(20));check(rejects(q));q=input;q.set("hinge_offset_mm",V::makeNum(3));check(rejects(q));
    V repeated=*input.find("notches");repeated.arr.push_back(repeated.arr[0]);q=input;q.set("notches",repeated);check(rejects(q));
    q=input;q.set("length_mm",V::makeNum(201));check(rejects(q));
    std::cout<<checks<<" notch mathematics checks passed\n";
}
