#include "ark/exploded_view.hpp"
#include "ark/agent.hpp"
#include "ark/motion_graph.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
namespace ark {
namespace {
using V=arkjson::Value;
void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
std::string num(double value){char buf[48];std::snprintf(buf,sizeof buf,"%.6g",value);return buf;}
void keys(const V& v,std::initializer_list<const char*> allowed,const std::string& what){need(v.t==V::Obj,"EXPLODE_INVALID: "+what+" must be an object");for(const auto& p:v.obj){bool found=false;for(auto key:allowed)found|=p.first==key;need(found,"EXPLODE_INVALID: unknown "+what+" field: "+p.first);}}
std::string viewName(const V& v,const char* key){const auto* p=v.find(key);need(p&&p->t==V::Str&&!p->str.empty()&&p->str.size()<=128,std::string("EXPLODE_INVALID: ")+key+" requires 1..128 characters");for(unsigned char c:p->str)need(c>=32,"EXPLODE_INVALID: exploded view names cannot contain control characters");return p->str;}
V vec(Vec3 p){V a=V::makeArr();for(double x:{p.x,p.y,p.z})a.arr.push_back(V::makeNum(x));return a;}
// Authored direction: +X..-Z (case-insensitive, sign optional) or a nonzero [x,y,z].
Vec3 stepDirection(const V& v,std::string* canonical=nullptr){
    const char* usage="EXPLODE_INVALID: direction must be +X, -X, +Y, -Y, +Z, -Z or a finite [x,y,z]";
    if(v.t==V::Str){std::string s;for(char c:v.str)s+=(char)std::toupper((unsigned char)c);if(s.size()==1)s="+"+s;
        need(s.size()==2&&(s[0]=='+'||s[0]=='-')&&s[1]>='X'&&s[1]<='Z',usage);if(canonical)*canonical=s;Vec3 d;const double sign=s[0]=='-'?-1:1;(s[1]=='X'?d.x:s[1]=='Y'?d.y:d.z)=sign;return d;}
    need(v.t==V::Arr&&v.arr.size()==3,usage);double c[3];
    for(size_t i=0;i<3;++i){need(v.arr[i].t==V::Num&&std::isfinite(v.arr[i].num)&&std::fabs(v.arr[i].num)<=1e9,usage);c[i]=v.arr[i].num;}
    const Vec3 d{c[0],c[1],c[2]};need(d.len()>1e-9,"EXPLODE_ZERO_DIRECTION: step direction has zero length");return d/d.len();
}
double stepDistance(const V& v,const ExprEnv& env){
    double x=0;if(v.t==V::Num)x=v.num;
    else{need(v.t==V::Str&&!v.str.empty()&&v.str.size()<=2048,"EXPLODE_INVALID: distance must be millimetres as a number or parameter expression");bool ok=false;x=evalExpr(v.str,env,&ok);need(ok||!std::isfinite(x),"EXPLODE_INVALID_EXPRESSION: distance = "+v.str);}
    need(std::isfinite(x)&&std::fabs(x)<=1e6,"EXPLODE_NONFINITE_DISTANCE: distance "+(v.t==V::Str?v.str+" = ":std::string())+num(x)+" must be finite within 1e6 mm");return x;
}
bool positiveId(const V& v){return v.t==V::Num&&std::isfinite(v.num)&&v.num>=1&&v.num==std::floor(v.num)&&v.num<=9007199254740990.;}
// Structure shared by persistence and authoring; distances are not evaluated here.
void checkView(const V& view){
    keys(view,{"name","steps","description"},"exploded view");const auto name=viewName(view,"name");
    if(const auto* d=view.find("description"))need(d->t==V::Str&&d->str.size()<=1024,"EXPLODE_INVALID: description must be text of at most 1024 characters");
    const auto* steps=view.find("steps");need(steps&&steps->t==V::Arr&&!steps->arr.empty(),"EXPLODE_INVALID: exploded view "+name+" requires a nonempty steps array");
    need(steps->arr.size()<=kMaxExplodedSteps,"EXPLODE_STEP_CAPACITY: exploded view "+name+" has "+std::to_string(steps->arr.size())+" steps; at most 64");
    for(size_t i=0;i<steps->arr.size();++i){const auto& s=steps->arr[i];const std::string at="step "+std::to_string(i+1);keys(s,{"instances","direction","distance","with_previous","label"},at);
        const auto* instances=s.find("instances");need(instances&&instances->t==V::Arr&&!instances->arr.empty(),"EXPLODE_INVALID: "+at+" requires a nonempty instances array");
        need(instances->arr.size()<=kMaxExplodedStepInstances,"EXPLODE_STEP_CAPACITY: "+at+" moves "+std::to_string(instances->arr.size())+" instances; at most 256");
        std::set<u64> seen;for(const auto& id:instances->arr){need(positiveId(id),"EXPLODE_INVALID: persisted step instances are positive assembly instance IDs");need(seen.insert((u64)id.num).second,"EXPLODE_DUPLICATE_INSTANCE: "+at+" lists instance #"+std::to_string((u64)id.num)+" twice");}
        const auto* direction=s.find("direction");need(direction,"EXPLODE_INVALID: "+at+" requires direction");(void)stepDirection(*direction);
        const auto* distance=s.find("distance");need(distance&&((distance->t==V::Num&&std::isfinite(distance->num))||(distance->t==V::Str&&!distance->str.empty()&&distance->str.size()<=2048)),"EXPLODE_INVALID: "+at+" requires a finite distance or expression");
        if(const auto* w=s.find("with_previous")){need(w->t==V::Bool,"EXPLODE_INVALID: with_previous must be boolean");need(!(w->b&&i==0),"EXPLODE_INVALID: the first step has no previous step to run with");}
        if(const auto* l=s.find("label"))need(l->t==V::Str&&l->str.size()<=128,"EXPLODE_INVALID: label must be text of at most 128 characters");}
}
u64 instanceRef(const Document& d,const V& v){
    if(v.t==V::Num){need(positiveId(v),"EXPLODE_INVALID: instance IDs are positive integers");const u64 id=(u64)v.num;need(std::any_of(d.assembly.begin(),d.assembly.end(),[&](const AssemblyNode& n){return n.id==id;}),"EXPLODE_UNKNOWN_INSTANCE: assembly instance #"+std::to_string(id)+" does not exist");return id;}
    need(v.t==V::Str&&!v.str.empty(),"EXPLODE_INVALID: an instance is an assembly instance id or unique name");u64 found=0;int count=0;
    for(const auto& n:d.assembly)if(n.name==v.str){found=n.id;++count;}
    need(count<2,"EXPLODE_AMBIGUOUS_INSTANCE: several assembly instances are named "+v.str+"; use its id");need(count==1,"EXPLODE_UNKNOWN_INSTANCE: assembly instance "+v.str+" does not exist");return found;
}
// Authoring form: names resolve to ids, axis names canonicalize, distances evaluate.
V authoredView(const Document& d,const V& command){
    V view=V::makeObj(),steps=V::makeArr();view.set("name",V::makeStr(viewName(command,"name")));if(const auto* text=command.find("description"))view.set("description",*text);
    const auto* source=command.find("steps");need(source&&source->t==V::Arr&&!source->arr.empty(),"EXPLODE_INVALID: explode.set requires a nonempty steps array");
    need(source->arr.size()<=kMaxExplodedSteps,"EXPLODE_STEP_CAPACITY: "+std::to_string(source->arr.size())+" steps exceed 64 per exploded view");const auto env=d.buildEnv();
    for(size_t i=0;i<source->arr.size();++i){const auto& s=source->arr[i];const std::string at="step "+std::to_string(i+1);keys(s,{"instances","direction","distance","with_previous","label"},at);V step=V::makeObj(),ids=V::makeArr();
        const auto* instances=s.find("instances");need(instances&&instances->t==V::Arr&&!instances->arr.empty(),"EXPLODE_INVALID: "+at+" requires a nonempty instances array");
        need(instances->arr.size()<=kMaxExplodedStepInstances,"EXPLODE_STEP_CAPACITY: "+at+" moves more than 256 instances");std::set<u64> seen;
        for(const auto& ref:instances->arr){const u64 id=instanceRef(d,ref);need(seen.insert(id).second,"EXPLODE_DUPLICATE_INSTANCE: "+at+" lists instance #"+std::to_string(id)+" twice");ids.arr.push_back(V::makeNum((double)id));}
        const auto* direction=s.find("direction");need(direction,"EXPLODE_INVALID: "+at+" requires direction");std::string axis;(void)stepDirection(*direction,&axis);
        const auto* distance=s.find("distance");need(distance,"EXPLODE_INVALID: "+at+" requires distance");(void)stepDistance(*distance,env);
        step.set("instances",ids);step.set("direction",axis.empty()?*direction:V::makeStr(axis));step.set("distance",*distance);
        if(const auto* w=s.find("with_previous"))step.set("with_previous",*w);if(const auto* l=s.find("label"))step.set("label",*l);steps.arr.push_back(step);}
    view.set("steps",steps);checkView(view);return view;
}
V* locateView(V& rows,const std::string& name){for(auto& row:rows.arr)if(const auto* n=row.find("name");n&&n->str==name)return &row;return nullptr;}
void requireUnused(const Document& d,const std::string& name,const char* action){const auto users=motionsDrivingExplodedView(d,name);if(users.empty())return;std::string list;for(const auto& m:users)list+=(list.empty()?"":", ")+m;
    throw std::runtime_error("EXPLODE_VIEW_IN_USE: cannot "+std::string(action)+" exploded view "+name+"; motion "+list+" drives it through an explode_view channel. Rebind or remove that channel first");}
}

const V* findExplodedView(const Document& d,const std::string& name){if(d.explodedViews.t!=V::Arr)return nullptr;for(const auto& row:d.explodedViews.arr)if(const auto* key=row.find("name");key&&key->t==V::Str&&key->str==name)return &row;return nullptr;}
bool compileExplodedView(const Document& d,const V& view,ExplodedViewPlan& out,std::string& error){
    out={};error.clear();
    try{checkView(view);out.name=view.find("name")->str;const auto env=d.buildEnv();std::set<u64> known,missing;for(const auto& n:d.assembly)known.insert(n.id);
        for(const auto& s:view.find("steps")->arr){ExplodedStep step;for(const auto& id:s.find("instances")->arr){const u64 node=(u64)id.num;step.nodes.push_back(node);if(!known.count(node))missing.insert(node);}
            step.direction=stepDirection(*s.find("direction"));step.distance=stepDistance(*s.find("distance"),env);step.withPrevious=s.find("with_previous")&&s.find("with_previous")->b;if(const auto* l=s.find("label"))step.label=l->str;
            if(!step.withPrevious&&!out.steps.empty())++out.slices;step.slice=out.slices;out.steps.push_back(std::move(step));}
        ++out.slices;out.missing.assign(missing.begin(),missing.end());return true;
    }catch(const std::exception& e){error=e.what();out={};return false;}
}
double explodedStepProgress(const ExplodedViewPlan& plan,size_t step,double t){
    if(step>=plan.steps.size()||!plan.slices||!std::isfinite(t))return 0;return std::clamp(std::clamp(t,0.,1.)*(double)plan.slices-(double)plan.steps[step].slice,0.,1.);
}
std::vector<Vec3> explodedOffsets(const Document& d,const ExplodedViewPlan& plan,const std::vector<u64>& nodes,double t){
    std::vector<Vec3> out(nodes.size());if(plan.steps.empty())return out;std::unordered_map<u64,u64> parent;for(const auto& n:d.assembly)parent[n.id]=n.parent;
    std::vector<std::vector<u64>> chains(nodes.size());
    for(size_t i=0;i<nodes.size();++i)for(u64 n=nodes[i];n&&chains[i].size()<4096;){chains[i].push_back(n);const auto up=parent.find(n);if(up==parent.end())break;n=up->second;}
    for(size_t s=0;s<plan.steps.size();++s){const double p=explodedStepProgress(plan,s,t);if(p<=0)continue;const auto& step=plan.steps[s];const Vec3 delta=step.direction*(step.distance*p);
        const std::set<u64> moved(step.nodes.begin(),step.nodes.end());for(size_t i=0;i<nodes.size();++i)if(std::any_of(chains[i].begin(),chains[i].end(),[&](u64 n){return moved.count(n)!=0;}))out[i]+=delta;}
    return out;
}
void validateExplodedViewStorage(const V& views){
    if(views.t==V::Null)return;need(views.t==V::Arr&&!views.arr.empty()&&views.arr.size()<=kMaxExplodedViews,"EXPLODE_CAPACITY: exploded views must be a nonempty array of at most 16 views");
    std::set<std::string> names;for(const auto& view:views.arr){checkView(view);need(names.insert(view.find("name")->str).second,"EXPLODE_NAME_CONFLICT: duplicate exploded view "+view.find("name")->str);}
}
std::string motionExplodeView(const V& channel){
    const auto* view=channel.find("explode_view");if(!view)return {};
    need(view->t==V::Str&&!view->str.empty()&&view->str.size()<=128,"MOTION_INVALID_GRAPH: explode_view must name an exploded view (1..128 characters)");
    need(!channel.find("mate"),"MOTION_INVALID_GRAPH: a channel binds either a joint mate or an explode_view factor, not both");return view->str;
}
std::vector<std::string> motionsDrivingExplodedView(const Document& d,const std::string& view){
    std::vector<std::string> out;if(d.motions.t!=V::Arr)return out;
    for(const auto& motion:d.motions.arr){const auto* graph=motion.find("graph");const auto* channels=graph?graph->find("channels"):nullptr;if(!channels||channels->t!=V::Arr)continue;
        for(const auto& c:channels->arr)if(const auto* bound=c.find("explode_view");bound&&bound->t==V::Str&&bound->str==view){out.push_back(motion.find("name")->asStr());break;}}
    return out;
}
bool applyExplodedViewCommand(Document& d,const V& command,std::string& error){
    const auto* op=command.find("op");if(!op||op->t!=V::Str)return false;const auto& name=op->str;
    if(name!="explode.set"&&name!="explode.rename"&&name!="explode.remove")return false;
    try{
        V rows=d.explodedViews.t==V::Arr?d.explodedViews:V::makeArr();
        if(name=="explode.set"){keys(command,{"op","name","steps","description","replace"},name);bool replace=true;if(const auto* r=command.find("replace")){need(r->t==V::Bool,"EXPLODE_INVALID: replace must be boolean");replace=r->b;}
            V view=authoredView(d,command);const auto key=view.find("name")->str;if(V* at=locateView(rows,key)){need(replace,"EXPLODE_NAME_CONFLICT: exploded view "+key+" already exists; omit replace:false to replace it");*at=std::move(view);}
            else{need(rows.arr.size()<kMaxExplodedViews,"EXPLODE_CAPACITY: a document holds at most 16 exploded views");rows.arr.push_back(std::move(view));}}
        else if(name=="explode.rename"){keys(command,{"op","name","new_name"},name);const auto key=viewName(command,"name"),next=viewName(command,"new_name");V* at=locateView(rows,key);need(at,"EXPLODE_NOT_FOUND: "+key);
            need(next==key||!locateView(rows,next),"EXPLODE_NAME_CONFLICT: exploded view "+next+" already exists");if(next!=key)requireUnused(d,key,"rename");at->set("name",V::makeStr(next));}
        else{keys(command,{"op","name"},name);const auto key=viewName(command,"name");need(locateView(rows,key),"EXPLODE_NOT_FOUND: "+key);requireUnused(d,key,"remove");
            rows.arr.erase(std::remove_if(rows.arr.begin(),rows.arr.end(),[&](const V& row){return row.find("name")->str==key;}),rows.arr.end());}
        V next=rows.arr.empty()?V{}:std::move(rows);validateExplodedViewStorage(next);d.explodedViews=std::move(next);error.clear();
    }catch(const std::exception& e){error=e.what();}
    return true;
}

namespace {
std::string nodeName(const Document& d,u64 id){for(const auto& n:d.assembly)if(n.id==id)return n.name;return {};}
V resolvedView(const Document& d,const V& view,const ExplodedViewPlan& plan){
    V out=V::makeObj(),steps=V::makeArr(),missing=V::makeArr(),drivers=V::makeArr();out.set("name",V::makeStr(plan.name));if(const auto* text=view.find("description"))out.set("description",*text);
    for(size_t i=0;i<plan.steps.size();++i){const auto& s=plan.steps[i];V row=V::makeObj(),instances=V::makeArr(),range=V::makeArr();row.set("index",V::makeNum((double)i));
        for(u64 node:s.nodes){V item=V::makeObj();item.set("node",V::makeNum((double)node));const auto name=nodeName(d,node);if(name.empty())item.set("missing",V::makeBool(true));else item.set("name",V::makeStr(name));instances.arr.push_back(item);}
        row.set("instances",instances);row.set("direction",vec(s.direction));row.set("authored_direction",*view.find("steps")->arr[i].find("direction"));row.set("authored_distance",*view.find("steps")->arr[i].find("distance"));
        row.set("distance_mm",V::makeNum(s.distance));row.set("slice",V::makeNum((double)s.slice));range.arr.push_back(V::makeNum((double)s.slice/plan.slices));range.arr.push_back(V::makeNum((double)(s.slice+1)/plan.slices));row.set("factor_range",range);
        row.set("with_previous",V::makeBool(s.withPrevious));if(!s.label.empty())row.set("label",V::makeStr(s.label));steps.arr.push_back(row);}
    for(u64 node:plan.missing)missing.arr.push_back(V::makeNum((double)node));for(const auto& m:motionsDrivingExplodedView(d,plan.name))drivers.arr.push_back(V::makeStr(m));
    out.set("steps",steps);out.set("slices",V::makeNum((double)plan.slices));out.set("missing_instances",missing);out.set("driven_by_motions",drivers);return out;
}
// Read-only stack explode from display bounds: each unit leaves its base side along
// the axis it sits furthest out on, clearing the units inside it by gap_mm.
V proposeExplodedView(Document& d,const V& request){
    keys(request,{"name","axis","scope","gap_mm","instances"},"propose");need(!d.assembly.empty(),"EXPLODE_PROPOSE_UNAVAILABLE: the document has no assembly instances to explode");
    std::string scope="leaf";if(const auto* s=request.find("scope")){need(s->t==V::Str&&(s->str=="leaf"||s->str=="top"),"EXPLODE_INVALID: propose scope must be leaf or top");scope=s->str;}
    Vec3 forced;bool fixedAxis=false;if(const auto* a=request.find("axis")){need(a->t==V::Str,"EXPLODE_INVALID: propose axis must be auto or +X..-Z");if(a->str!="auto"){forced=stepDirection(*a);fixedAxis=true;}}
    double gap=-1;if(const auto* g=request.find("gap_mm")){need(g->t==V::Num&&std::isfinite(g->num)&&g->num>=0&&g->num<=1e5,"EXPLODE_INVALID: gap_mm must be 0..1e5");gap=g->num;}
    std::vector<AssemblyPlacement> placements;std::string error;need(resolveAssembly(d,placements,error),"EXPLODE_PROPOSE_UNAVAILABLE: assembly solve failed: "+error);
    std::unordered_map<u64,const AssemblyNode*> nodes;for(const auto& n:d.assembly)nodes[n.id]=&n;
    auto unitOf=[&](u64 node){if(scope=="leaf")return node;u64 top=node;for(int guard=0;guard<4096;++guard){const auto it=nodes.find(top);if(it==nodes.end()||!it->second->parent)break;top=it->second->parent;}return top;};
    std::unordered_map<u64,AABB> localBounds;std::map<u64,AABB> units;std::set<u64> fixed;
    for(const auto& p:placements){if(!p.visible)continue;auto lb=localBounds.find(p.feature);if(lb==localBounds.end()){AABB box;for(const auto& b:d.evalBodies)if(b.id==p.feature){box=b.tessellate(d.tessOpt).computeBBox();break;}lb=localBounds.emplace(p.feature,box).first;}
        if(!lb->second.valid())continue;const u64 unit=unitOf(p.node);units[unit].grow(lb->second.transformed(p.world));for(u64 n=p.node;n;){const auto it=nodes.find(n);if(it==nodes.end())break;if(it->second->fixed)fixed.insert(unit);n=it->second->parent;}}
    if(const auto* only=request.find("instances")){need(only->t==V::Arr&&!only->arr.empty(),"EXPLODE_INVALID: propose instances must be a nonempty array");std::set<u64> keep;for(const auto& ref:only->arr)keep.insert(unitOf(instanceRef(d,ref)));for(auto it=units.begin();it!=units.end();)if(!keep.count(it->first)&&!fixed.count(it->first))it=units.erase(it);else ++it;}
    need(units.size()>=2,"EXPLODE_PROPOSE_UNAVAILABLE: fewer than two visible occurrence units with display bounds");
    AABB base;V baseIds=V::makeArr();if(fixed.empty()){u64 largest=units.begin()->first;double volume=-1;for(const auto& [id,box]:units){const auto e=box.extent();const double v=e.x*e.y*e.z;if(v>volume){volume=v;largest=id;}}fixed.insert(largest);}
    for(u64 id:fixed)if(units.count(id)){base.grow(units[id]);baseIds.arr.push_back(V::makeNum((double)id));}
    need(base.valid(),"EXPLODE_PROPOSE_UNAVAILABLE: the grounded base has no display bounds");
    const Vec3 axes[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};const char* axisNames[6]={"+X","-X","+Y","-Y","+Z","-Z"};
    auto along=[](const AABB& b,Vec3 u,bool high){const double lo=u.x*(u.x>0?b.mn.x:b.mx.x)+u.y*(u.y>0?b.mn.y:b.mx.y)+u.z*(u.z>0?b.mn.z:b.mx.z),hi=u.x*(u.x>0?b.mx.x:b.mn.x)+u.y*(u.y>0?b.mx.y:b.mn.y)+u.z*(u.z>0?b.mx.z:b.mn.z);return high?hi:lo;};
    struct Unit {u64 id;int axis;double offset,lo,hi,distance=0;};std::vector<Unit> moving;std::vector<double> extents;
    for(const auto& [id,box]:units){if(fixed.count(id))continue;const Vec3 c=box.center()-base.center();int best=4;double score=-1e300;
        for(int a=0;a<6;++a){if(fixedAxis&&!(axes[a].approx(forced)||axes[a].approx(-forced)))continue;const Vec3 u=axes[a];const double half=(along(base,u,true)-along(base,u,false))*.5+(along(box,u,true)-along(box,u,false))*.5;const double s=c.dot(u)/std::max(half,1e-9)+(a==4?1e-9:0);if(s>score){score=s;best=a;}}
        moving.push_back({id,best,c.dot(axes[best]),along(box,axes[best],false),along(box,axes[best],true)});extents.push_back(std::max(1e-6,along(box,axes[best],true)-along(box,axes[best],false)));}
    if(gap<0){std::nth_element(extents.begin(),extents.begin()+extents.size()/2,extents.end());gap=std::max(1.,.25*extents[extents.size()/2]);}
    for(int a=0;a<6;++a){std::vector<Unit*> group;for(auto& u:moving)if(u.axis==a)group.push_back(&u);std::sort(group.begin(),group.end(),[](const Unit* x,const Unit* y){return x->offset<y->offset;});
        double reach=along(base,axes[a],true),previous=0;for(auto* u:group){u->distance=std::max({0.,previous,reach+gap-u->lo});u->distance=std::ceil(u->distance*10-1e-9)/10;previous=u->distance;reach=u->hi+u->distance;}}
    std::sort(moving.begin(),moving.end(),[](const Unit& x,const Unit& y){return x.distance!=y.distance?x.distance>y.distance:x.id<y.id;});
    struct Group {int axis;double distance;std::vector<u64> ids;};std::vector<Group> groups;
    for(const auto& u:moving){if(u.distance<=0)continue;auto it=std::find_if(groups.begin(),groups.end(),[&](const Group& g){return g.axis==u.axis&&std::fabs(g.distance-u.distance)<1e-6&&g.ids.size()<kMaxExplodedStepInstances;});if(it==groups.end())groups.push_back({u.axis,u.distance,{u.id}});else it->ids.push_back(u.id);}
    V omitted=V::makeArr();while(groups.size()>kMaxExplodedSteps){for(u64 id:groups.back().ids)omitted.arr.push_back(V::makeNum((double)id));groups.pop_back();}
    need(!groups.empty(),"EXPLODE_PROPOSE_UNAVAILABLE: every unit already clears its base; nothing to explode");
    std::string name="Exploded view";if(request.find("name"))name=viewName(request,"name");else for(int k=2;findExplodedView(d,name)&&k<100;++k)name="Exploded view "+std::to_string(k);
    V command=V::makeObj(),steps=V::makeArr();command.set("op",V::makeStr("explode.set"));command.set("name",V::makeStr(name));
    for(const auto& g:groups){V step=V::makeObj(),ids=V::makeArr();std::string label;for(u64 id:g.ids){ids.arr.push_back(V::makeNum((double)id));if(label.size()<96)label+=(label.empty()?"":", ")+nodeName(d,id);}if(label.size()>128)label=label.substr(0,125)+"...";
        step.set("instances",ids);step.set("direction",V::makeStr(axisNames[g.axis]));step.set("distance",V::makeNum(g.distance));step.set("label",V::makeStr(label));steps.arr.push_back(step);}
    command.set("steps",steps);V out=V::makeObj();out.set("proposal",command);out.set("base_units",baseIds);out.set("scope",V::makeStr(scope));out.set("gap_mm",V::makeNum(gap));out.set("units",V::makeNum((double)units.size()));if(!omitted.arr.empty())out.set("omitted_units",omitted);
    out.set("method",V::makeStr("Display-tessellation bounding boxes. Grounded units (or the largest unit) stay; every other unit moves along the axis it sits furthest out on, outermost first, each clearing the units inside it by gap_mm. Review, edit and commit with doc_apply explode.set at the observed expected_revision."));
    out.set("committed",V::makeBool(false));return out;
}
}
bool callExplodedViewTool(AgentSession& session,const std::string& tool,const V& args,V& out,Image* image){
    if(tool=="visualize_render"){need(image,"visualize_render requires an image target");out=visualizeRender(session.document,session.revision,args,*image);return true;}
    if(tool!="explode_info")return false;
    keys(args,{"name","factor","propose","expected_revision"},"explode_info");Document& d=session.document;
    if(const auto* expected=args.find("expected_revision"))need(expected->t==V::Num&&expected->num==(double)session.revision,"Revision conflict; the current revision is in this result. Re-read explode_info and retry");
    out=V::makeObj();out.set("revision",V::makeNum((double)session.revision));out.set("display_only",V::makeBool(true));
    if(const auto* propose=args.find("propose")){need(!args.find("name")&&!args.find("factor"),"explode_info propose excludes name and factor");out.set("propose",proposeExplodedView(d,*propose));out.set("document_unchanged",V::makeBool(true));return true;}
    const auto* nameArg=args.find("name");const auto* factorArg=args.find("factor");
    if(factorArg)need(nameArg&&factorArg->t==V::Num&&std::isfinite(factorArg->num)&&factorArg->num>=0&&factorArg->num<=1,"explode_info factor must be within [0,1] and requires name");
    V limits=V::makeObj();limits.set("views",V::makeNum((double)kMaxExplodedViews));limits.set("steps_per_view",V::makeNum((double)kMaxExplodedSteps));limits.set("instances_per_step",V::makeNum((double)kMaxExplodedStepInstances));out.set("limits",limits);
    if(!nameArg){V rows=V::makeArr();if(d.explodedViews.t==V::Arr)for(const auto& view:d.explodedViews.arr){ExplodedViewPlan plan;std::string error;V row=V::makeObj();row.set("name",*view.find("name"));
            if(compileExplodedView(d,view,plan,error)){row.set("steps",V::makeNum((double)plan.steps.size()));row.set("slices",V::makeNum((double)plan.slices));row.set("missing_instances",V::makeNum((double)plan.missing.size()));}else row.set("error",V::makeStr(error));
            V drivers=V::makeArr();for(const auto& m:motionsDrivingExplodedView(d,view.find("name")->str))drivers.arr.push_back(V::makeStr(m));row.set("driven_by_motions",drivers);rows.arr.push_back(row);}
        out.set("views",rows);out.set("count",V::makeNum((double)rows.arr.size()));out.set("document_unchanged",V::makeBool(true));return true;}
    need(nameArg->t==V::Str,"explode_info name must be text");const V* view=findExplodedView(d,nameArg->str);need(view,"EXPLODE_NOT_FOUND: "+nameArg->str);
    ExplodedViewPlan plan;std::string error;need(compileExplodedView(d,*view,plan,error),error);out.set("view",resolvedView(d,*view,plan));out.set("authored",*view);
    if(factorArg){std::vector<AssemblyPlacement> placements;need(resolveAssembly(d,placements,error),"EXPLODE_INVALID: assembly solve failed: "+error);std::vector<u64> ids;for(const auto& p:placements)ids.push_back(p.node);
        const auto offsets=explodedOffsets(d,plan,ids,factorArg->num);V rows=V::makeArr(),progress=V::makeArr();
        for(size_t i=0;i<placements.size();++i){if(offsets[i].lensq()<=0)continue;V row=V::makeObj();row.set("node",V::makeNum((double)placements[i].node));row.set("copy",V::makeNum(placements[i].copy));row.set("name",V::makeStr(placements[i].name));row.set("offset_mm",vec(offsets[i]));rows.arr.push_back(row);}
        for(size_t s=0;s<plan.steps.size();++s)progress.arr.push_back(V::makeNum(explodedStepProgress(plan,s,factorArg->num)));
        V state=V::makeObj();state.set("factor",V::makeNum(factorArg->num));state.set("step_progress",progress);state.set("moved_occurrences",rows);state.set("authority",V::makeStr("display transforms over the solved assembly; placements and B-rep unchanged"));out.set("state",state);}
    out.set("document_unchanged",V::makeBool(true));return true;
}
}
