#include "ark/snapshot_032d.hpp"
#include "ark/agent.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace ark;using V=arkjson::Value;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
Mesh quad(float z=0){Mesh m;m.pos={-1,-1,z,1,-1,z,1,1,z,-1,1,z};m.idx={0,1,2,0,2,3};return m;}
SnapCam camera(){SnapCam c;c.useDirection=true;c.direction={0,0,1};c.dist=6;return c;}
Vec3 center(const Image& i){size_t n=((size_t)i.h/2*i.w+i.w/2)*3;return {(double)i.px[n],(double)i.px[n+1],(double)i.px[n+2]};}
V json(const char* text){V v;check(arkjson::parse(text,v),"fixture JSON");return v;}
void raster(){
    const auto m=quad();SnapshotMaterial red;red.color={1,0,0};std::vector<SnapshotMaterial> styles(2,red);
    const auto legacy=renderSnapshot(m,192,128,camera()),colored=renderSnapshot(m,192,128,camera(),styles);const auto p=center(colored);
    check(p.x>100&&p.y==0&&p.z==0,"Authored red material affects RGB pixels");check(colored.px!=legacy.px,"Colored snapshot differs from legacy clay");
    SnapshotMaterial blue;blue.color={0,0,0};blue.emission={0,0,1};blue.emissionIntensity=2;blue.bloom=.6;blue.glow=10;
    const auto lit=renderSnapshot(m,192,128,camera(),std::vector<SnapshotMaterial>(2,blue));check(center(lit).z>240&&center(lit).x==0,"Emission remains visible on a dark base");
    Mesh layered=quad(-.1f);layered.append(quad(.1f));styles.assign(2,red);SnapshotMaterial clear;clear.color={0,0,1};clear.opacity=.5;styles.insert(styles.end(),2,clear);
    const auto mixed=center(renderSnapshot(layered,192,128,camera(),styles));check(mixed.x>50&&mixed.z>50,"Translucent foreground blends with opaque background");
    bool rejected=false;try{renderSnapshot(m,192,128,camera(),{});}catch(...){rejected=true;}check(rejected,"Material triangle count mismatch rejects");
}
void materials(){
    Document d;Feature f;f.type=FeatureType::Box;f.material="Red";const auto id=d.addFeature(f);Material material;material.name="Red";material.appearance.baseColor={1,0,0};d.materials.push_back(material);Body b;b.id=id;
    auto styles=snapshotBodyMaterials(d,b,quad());check(styles.size()==2&&styles[0].color.x==1&&styles[0].color.z==0,"Named material resolves for each triangle");
    d.find(id)->appearance=json(R"({"base_color":[0,1,0],"emission_intensity":2,"emission_color":[0,0,1]})");styles=snapshotBodyMaterials(d,b,quad());check(styles[0].color.y==1&&styles[0].emissionIntensity==2&&styles[0].emission.z==1,"Feature color and emission override the named material");
}
#ifdef ARK_CONTRACTS_NATIVE_TESTS
void nativeFaces(){
    AgentSession s;auto reply=s.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"native_solid","native":{"op":"box"},"p":{"width":2,"depth":3,"height":4},"appearance":{"base_color":[1,0,0]}}]})"));
    check(!reply.find("isError")->b,"Exact appearance fixture evaluates");
    check(!s.document.evalBodies.empty()&&s.document.evalBodies.front().nativeData.find("topology"),"Exact fixture supplies native topology");
    auto& body=s.document.evalBodies.front();auto* feature=s.document.find(body.id);const auto& topology=*body.nativeData.find("topology");
    const auto* refs=topology.find("references");const V* selected=nullptr;for(const auto& row:refs->arr)if(row.find("kind")->asStr()=="face"&&row.find("indices")->arr.size()==1){selected=&row;break;}
    check(selected,"Exact producer supplies a bound face");FaceAppearance patch;patch.face=selected->find("name")->str;patch.sourceTopology=topology.find("brep_sha256")->str;patch.appearance=json(R"({"base_color":[0,0,1],"emission_intensity":1,"emission_color":[0,0,1]})");feature->faceAppearances.push_back(patch);
    const auto mesh=body.tessellate(s.document.tessOpt);const auto styles=snapshotBodyMaterials(s.document,body,mesh);size_t blue=0,red=0;for(const auto& style:styles){if(style.color.z==1&&style.emissionIntensity==1)++blue;if(style.color.x==1)++red;}
    check(blue>0&&red>0,"Exact semantic face override colors its spans without recoloring other faces");
    reply=s.call("snapshot",json(R"({"id":1,"view":"iso"})"));check(!reply.find("isError")->b,"Material-aware snapshot tool succeeds");const auto* receipt=reply.find("structuredContent");check(receipt&&receipt->dump().find("authored_body_and_face_color_opacity_emission")!=std::string::npos,"Snapshot receipt reports material scope");
}
#endif
}
int main(){try{raster();materials();
#ifdef ARK_CONTRACTS_NATIVE_TESTS
nativeFaces();
#endif
std::cout<<"032D snapshot color, emission and opacity checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
