#include "ark/snapshot_032d.hpp"
#include "ark/agent.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/media_044b.hpp"
#include <algorithm>
#include <cmath>
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
// RGB-sum steps between adjacent columns across the middle 70% of the surface
// on the middle row: the largest step, and how many reach 5 (rounding noise
// stays below that). Flat shading jumps at every facet boundary.
struct ColumnSteps{int largest=0,jumps=0;};
ColumnSteps columnSteps(const unsigned char* px,int w,int h,int channels){
    const size_t row=(size_t)(h/2)*w;auto at=[&](int x){const auto* p=px+(row+x)*channels;return (int)p[0]+p[1]+p[2];};
    int left=0,right=w-1;while(left<w&&at(left)==at(0))++left;while(right>left&&at(right)==at(w-1))--right;check(right-left>w/8,"Shading fixture covers the row");
    const int inset=(right-left)*15/100;ColumnSteps steps;for(int x=left+inset;x<right-inset;++x){const int step=std::abs(at(x+1)-at(x));steps.largest=std::max(steps.largest,step);steps.jumps+=step>=5;}return steps;
}
Mesh lateralCylinder(int sides){const double pi=std::acos(-1.);Mesh m;for(int i=0;i<sides;++i){const double a=2*pi*i/sides;m.addVertex({std::cos(a),-1,std::sin(a)});m.addVertex({std::cos(a),1,std::sin(a)});}
    for(int i=0;i<sides;++i){const u32 b0=2*i,t0=b0+1,b1=2*((i+1)%sides),t1=b1+1;m.addTri(b0,t0,b1);m.addTri(b1,t0,t1);}m.computeNormals();return m;}
Mesh cube(bool welded){const int quads[6][4]={{1,3,7,5},{0,4,6,2},{2,6,7,3},{0,1,5,4},{4,5,7,6},{0,2,3,1}};auto corner=[](int i){return Vec3{(i&1)?1.:-1.,(i&2)?1.:-1.,(i&4)?1.:-1.};};Mesh m;if(welded)for(int i=0;i<8;++i)m.addVertex(corner(i));
    for(const auto& q:quads){u32 v[4];for(int k=0;k<4;++k){if(welded)v[k]=(u32)q[k];else{v[k]=(u32)m.vertCount();m.addVertex(corner(q[k]));}}m.addTri(v[0],v[1],v[2]);m.addTri(v[0],v[2],v[3]);}m.computeNormals();return m;}
void smoothShading(){
    // Display only: interpolated vertex normals remove facet bands from a
    // coarse curved mesh, while planar faces keep their exact flat pixels.
    auto styles=[](const Mesh& m,bool smooth){SnapshotMaterial s;s.smoothShading=smooth;return std::vector<SnapshotMaterial>(m.triCount(),s);};
    const auto cylinder=lateralCylinder(24);const auto flat=renderSnapshot(cylinder,640,400,camera(),styles(cylinder,false)),smooth=renderSnapshot(cylinder,640,400,camera(),styles(cylinder,true));
    const auto flatSteps=columnSteps(flat.px.data(),flat.w,flat.h,3),smoothSteps=columnSteps(smooth.px.data(),smooth.w,smooth.h,3);
    check(flatSteps.largest>=4*std::max(smoothSteps.largest,1)&&smoothSteps.jumps==0&&flatSteps.jumps>=3,"Smooth snapshot shading removes facet steps from a 24-sided cylinder");
    SnapCam iso;iso.useDirection=true;iso.direction={1,.8,.6};iso.dist=8;
    for(bool welded:{false,true}){const auto box=cube(welded);const auto a=renderSnapshot(box,320,200,iso,styles(box,false)),b=renderSnapshot(box,320,200,iso,styles(box,true));
        check(a.px==b.px&&center(a).x!=a.px[0],"Planar faces and sharp welded corners keep exact flat-shaded pixels");}
}
const char* const kShading="smooth_vertex_normals_except_legacy_extrude_sketch_cut_notched_tube";
struct ToolImage{std::vector<unsigned char> rgba;int w=0,h=0;};
ToolImage toolImage(const V& reply){const V* image=nullptr;for(const auto& item:reply.find("content")->arr)if(item.find("type")->str=="image")image=&item;check(image,"Snapshot returns pixels");
    V asset=V::makeObj();asset.set("name",V::makeStr("shading"));asset.set("mime_type",V::makeStr("image/png"));asset.set("data_base64",*image->find("data"));const auto decoded=imageAssetFromJson(asset);return {decodeImageAssetRGBA(decoded),(int)decoded.width,(int)decoded.height};}
// A notched tube is a welded planar mesh: its area-averaged normals would bend
// flat faces, so the tool keeps it hard, as Studio draws it. The receipt states
// lighting only in shading; appearance_scope no longer claims flat lighting.
void hardSurfaceReceipt(){
    AgentSession s;auto reply=s.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"feature.add","type":"notched_tube","name":"Notched","p":{"width_mm":40,"height_mm":30,"wall_mm":2,"length_mm":600,"hinge_offset_mm":1},"notches":[{"station":200,"angle":90,"face":"bottom"}]}]})"));
    check(!reply.find("isError")->b&&s.document.evalBodies.size()==1,"Notched tube fixture evaluates");
    reply=s.call("snapshot",json(R"({"view":"iso"})"));check(!reply.find("isError")->b,"Notched tube snapshot succeeds");const auto& evidence=*reply.find("structuredContent")->find("visual_evidence");
    check(evidence.find("shading")->str==kShading&&evidence.find("appearance_scope")->str=="triplanar_textures_procedural_patterns_surface_stamps_no_bump_no_plane_stamps","Snapshot receipt states lighting once, in shading");
    const auto tool=toolImage(reply);const auto& body=s.document.evalBodies.front();const auto mesh=body.tessellate(s.document.tessOpt);const auto box=mesh.computeBBox();
    SnapCam iso;iso.useTarget=iso.useDirection=true;iso.target=box.center();iso.dist=std::max(box.diagonal()*1.7,1.);iso.direction=Vec3{1,-1,.8}.normalized();iso.up={0,0,1};
    SnapshotAppearanceOptions044B options;auto styles=snapshotBodyMaterials(s.document,body,mesh,&options);const auto flat=renderSnapshot(mesh,tool.w,tool.h,iso,styles);for(auto& style:styles)style.smoothShading=true;const auto smooth=renderSnapshot(mesh,tool.w,tool.h,iso,styles);
    size_t same=0,bent=0;for(size_t p=0;p<(size_t)tool.w*tool.h;++p){same+=tool.rgba[p*4]==flat.px[p*3]&&tool.rgba[p*4+1]==flat.px[p*3+1]&&tool.rgba[p*4+2]==flat.px[p*3+2];bent+=flat.px[p*3]!=smooth.px[p*3]||flat.px[p*3+1]!=smooth.px[p*3+1]||flat.px[p*3+2]!=smooth.px[p*3+2];}
    check(mesh.nrm.size()==mesh.pos.size()&&bent>100,"Averaged normals would bend the notched tube's flat faces");
    check(same==(size_t)tool.w*tool.h,"Notched tube snapshot keeps Studio's hard-surface pixels");
}
#ifdef ARK_CONTRACTS_NATIVE_TESTS
// The snapshot tool lights exact bodies smoothly and keeps legacy sampled
// extrusions faceted, as Studio draws them; the receipt states which. The
// baseline is a flat-lit render of each body's own display mesh.
void nativeSmoothShading(){
    AgentSession s;auto reply=s.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"sketch.set","name":"Ring","sketch":{"plane":"XY","entities":[{"kind":"circle","name":"rim","dof":[0,0,10]}]}},{"op":"feature.add","type":"extrude","name":"Sampled","p":{"sketch":"Ring","height":1000}},{"op":"feature.add","type":"native_solid","name":"Exact","native":{"op":"extrude"},"p":{"sketch":"Ring","depth":1000}}]})"));
    check(!reply.find("isError")->b&&s.document.evalBodies.size()==2,"Shading fixture evaluates");ColumnSteps tool[2],flat[2];
    // A 1 m rod uses the 043A 0.25 rad display tessellation; view a close-up.
    SnapCam front;front.useTarget=front.useDirection=true;front.target={0,0,500};front.direction={0,-1,0};front.up={0,0,1};front.dist=60;
    for(int i=0;i<2;++i){V args=json(R"({"camera":{"direction":[0,-1,0],"up":[0,0,1],"target_mm":[0,0,500],"distance_mm":60}})");args.set("id",V::makeNum((double)s.document.features[i].id));reply=s.call("snapshot",args);check(!reply.find("isError")->b,"Shading snapshot succeeds");
        check(reply.find("structuredContent")->find("visual_evidence")->find("shading")->str==kShading,"Snapshot receipt states its shading");
        const auto image=toolImage(reply);tool[i]=columnSteps(image.rgba.data(),image.w,image.h,4);
        const auto mesh=s.document.evalBodies[i].tessellate(s.document.tessOpt);const auto baseline=renderSnapshot(mesh,1024,640,front,std::vector<SnapshotMaterial>(mesh.triCount()));flat[i]=columnSteps(baseline.px.data(),baseline.w,baseline.h,3);}
    check(flat[1].jumps>=3&&tool[1].jumps==0,"Exact curved body is lit with interpolated normals, without its mesh facet steps");
    check(tool[0].jumps>=2&&tool[0].jumps==flat[0].jumps&&tool[0].largest>=2*tool[1].largest,"Legacy sampled extrusion stays faceted, as Studio draws it");
}
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
int main(){try{raster();materials();smoothShading();hardSurfaceReceipt();
#ifdef ARK_CONTRACTS_NATIVE_TESTS
nativeFaces();nativeSmoothShading();
#endif
std::cout<<"032D snapshot color, emission and opacity checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
