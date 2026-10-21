// Included inside namespace ark by motion_graph.cpp. 046B read-only clearance
// over a fresh bake. Exact B-rep distance per pose; frame intervals certified
// with a conservative displacement bound, then subdivided through the same
// joint solver. The math and its limits are in docs/MOTION_CLEARANCE_046B.md.
namespace {
constexpr double kClearanceEpsilon046=1e-6,kBoxMargin046=1e-3,kExactBudgetMs046=8000;
constexpr size_t kMaxClearancePairs046=1024,kMaxPairFrames046=1048576,kMaxExact046=4096,kMaxSubPoses046=2048,kPosesPerRequest046=128,kMaxListed046=64,kExcursionSamples046=8,kClearanceCacheEntries046=8;
enum ClearanceState046 {kClear046=0,kUnproven046=1,kBelow046=2,kUncertified046=3,kTouching046=4,kInterference046=5};
const char* clearanceStateName046(int s){static const char* names[]={"clear","threshold_unproven","below_threshold","uncertified","touching","interference"};return names[std::clamp(s,0,5)];}
struct ClearanceItem046 {size_t index=0,slot=0;u64 node=0,feature=0,cluster=0;int copy=0;std::string name;bool moving=false,chain=true;Vec3 center,half;double radius=0;std::vector<size_t> channels;Mat4 fixedWorld;};
struct ClearancePair046 {size_t a=0,b=0;bool chain=true,proven=true;std::vector<size_t> onlyA,onlyB;};
struct ClearanceSample046 {double t=0;long frame=-1;size_t interval=0;std::vector<Mat4> world;std::vector<MotionJointFrame> joints;};
struct ClearanceMeasure046 {double value=0,volume=0;bool contact=false,interference=false;V points,overlap;};
struct ClearanceSpan046 {size_t pair=0,s1=0,s2=0,interval=0;int depth=0;};
struct ClearanceLeaf046 {size_t pair=0,interval=0,s1=0,s2=0;int state=0,depth=0;double lower=0;bool proven=true;std::string reason;};
struct ClearanceBound046 {double d=0;bool proven=false;};
std::mutex g_clearanceCacheMutex046;std::list<std::pair<std::string,V>> g_clearanceCache046;
Mat4 rigidInverse046(const Mat4& m){Mat4 r=Mat4::identity();for(int i=0;i<3;++i)for(int j=0;j<3;++j)r.m[j*4+i]=m.m[i*4+j];for(int i=0;i<3;++i)r.m[12+i]=-(r.m[i]*m.m[12]+r.m[4+i]*m.m[13]+r.m[8+i]*m.m[14]);return r;}
// ||R2-R1||_2 for two rotations: 2 sin(theta/2) = ||R2-R1||_F/sqrt(2).
double rotationChord046(const Mat4& a,const Mat4& b){double s=0;for(int i=0;i<3;++i)for(int j=0;j<3;++j){const double x=a.m[i*4+j]-b.m[i*4+j];s+=x*x;}return std::min(2.,std::sqrt(s/2));}
V matrixJson046(const Mat4& m){V out=V::makeArr();for(double x:m.m)out.arr.push_back(V::makeNum(x));return out;}
u64 clearanceNode046(const Document& d,const V& value,const char* key){
    if(value.t==V::Num){need(std::isfinite(value.num)&&value.num>=1&&value.num==std::floor(value.num),std::string("MOTION_CLEARANCE_INVALID: pair ")+key+" must be a node ID or unique occurrence name");for(const auto& n:d.assembly)if((double)n.id==value.num)return n.id;}
    else if(value.t==V::Str){u64 found=0;for(const auto& n:d.assembly)if(n.name==value.str){need(!found,"MOTION_CLEARANCE_INVALID: ambiguous occurrence name "+value.str+"; use its node ID");found=n.id;}if(found)return found;}
    throw std::runtime_error(std::string("MOTION_CLEARANCE_INVALID: pair ")+key+" occurrence not found: "+value.dump());
}
int clearanceCopy046(const V& item,const char* key){const auto* value=item.find(key);if(!value)return 0;need(value->t==V::Num&&value->num>=0&&value->num<=31&&value->num==std::floor(value->num),std::string("MOTION_CLEARANCE_INVALID: ")+key+" must be integer 0..31");return (int)value->num;}
}

V inspectMotionClearance(Document& d,u64 revision,const std::string& name,const V& request,const std::atomic_bool* cancel){
    using Clock=std::chrono::steady_clock;const auto started=Clock::now();auto since=[](Clock::time_point a){return std::chrono::duration<double,std::milli>(Clock::now()-a).count();};
    auto cancelled=[&]{need(!(cancel&&cancel->load()),"MOTION_CLEARANCE_CANCELLED: clearance check cancelled; no result was published");};
    need(request.t==V::Obj,"MOTION_CLEARANCE_INVALID: clearance must be an object");
    for(const auto& field:request.obj)need(field.first=="pairs"||field.first=="scope"||field.first=="threshold_mm"||field.first=="max_subdivisions"||field.first=="from_frame"||field.first=="to_frame","MOTION_CLEARANCE_INVALID: unknown clearance field: "+field.first);
    double threshold=0;if(const auto* t=request.find("threshold_mm")){need(t->t==V::Num&&std::isfinite(t->num)&&t->num>=0&&t->num<=1000,"MOTION_CLEARANCE_INVALID: threshold_mm must be in [0,1000]");threshold=t->num;}
    int maxSub=4;if(const auto* s=request.find("max_subdivisions")){need(s->t==V::Num&&s->num>=0&&s->num<=10&&s->num==std::floor(s->num),"MOTION_CLEARANCE_INVALID: max_subdivisions must be an integer 0..10");maxSub=(int)s->num;}
    std::string scope="all";if(const auto* s=request.find("scope")){need(s->t==V::Str&&(s->str=="all"||s->str=="static"),"MOTION_CLEARANCE_INVALID: scope must be all or static");need(!request.find("pairs"),"MOTION_CLEARANCE_INVALID: give pairs or scope, not both");scope=s->str;}
    const V* pairList=request.find("pairs");if(pairList){need(pairList->t==V::Arr&&!pairList->arr.empty()&&pairList->arr.size()<=256,"MOTION_CLEARANCE_INVALID: pairs must be 1..256 objects");scope="pairs";}
    const V* motion=findMotion(d,name);need(motion,"MOTION_NOT_FOUND: "+name);const V* bake=motion->find("bake");need(bake&&bake->t==V::Obj,"MOTION_NOT_BAKED: motion "+name+" has no bake; run doc_apply motion.bake first");
    const V graph=*motion->find("graph");AssemblyManipulationProgram program;std::string error;need(buildAssemblyManipulation(d,program,error),"MOTION_CLEARANCE_REJECTED: assembly solve failed: "+error);
    const std::string digest=motionSourceDigest(d,program.baseline),graphDigest=motionGraphDigest(graph);std::vector<std::string> reasons;
    if(bake->find("source_digest")->str!=digest)reasons.push_back("assembly_changed");if(bake->find("graph_digest")->str!=graphDigest)reasons.push_back("graph_changed");
    GraphCheck g;try{g=checkGraph(d,graph);}catch(const std::exception&){reasons.push_back("graph_invalid");}
    if(!reasons.empty()){std::string text;for(const auto& r:reasons)text+=(text.empty()?"":", ")+r;throw std::runtime_error("MOTION_BAKE_STALE: motion "+name+" bake is stale ("+text+"); bake it again with doc_apply motion.bake and the current expected_revision before checking clearance; clearance never re-bakes or uses stale frames");}
    // 049A receipts carry no frames: the double-precision re-solve below must reproduce frames_sha256.
    BakedMotionFrames baked;const bool receipt=bakeIsReceipt(*bake);if(receipt)decodeBakeHeader(*bake,baked);else need(decodeBakedMotion(*motion,baked,error),error);
    const auto times=motionFrameTimes(g.compiled.duration,baked.fps);need(times.size()==baked.frameCount,"MOTION_BAKE_MISMATCH: the bake has "+std::to_string(baked.frameCount)+" frames but its graph and fps give "+std::to_string(times.size())+"; bake it again");
    long k0=0,k1=(long)times.size()-1;
    for(const char* key:{"from_frame","to_frame"})if(const auto* f=request.find(key)){need(f->t==V::Num&&f->num>=0&&f->num==std::floor(f->num)&&f->num<(double)times.size(),std::string("MOTION_CLEARANCE_INVALID: ")+key+" must be a baked frame index");(std::string(key)=="from_frame"?k0:k1)=(long)f->num;}
    need(k1>k0,"MOTION_CLEARANCE_INVALID: to_frame must exceed from_frame");const size_t intervals=(size_t)(k1-k0);
    // Occurrences with exact geometry. Scope queries consider every visible
    // placed solid; explicit pairs may name hidden occurrences.
    std::vector<ClearanceItem046> items;std::map<size_t,size_t> itemOf;
    auto addItem=[&](size_t index){if(auto at=itemOf.find(index);at!=itemOf.end())return at->second;const auto& p=program.baseline[index];ClearanceItem046 it;it.index=index;it.node=p.node;it.copy=p.copy;it.feature=p.feature;it.name=p.name;it.fixedWorld=p.world;it.cluster=program.rigidCluster.count(p.node)?program.rigidCluster.at(p.node):p.node;items.push_back(it);itemOf[index]=items.size()-1;return items.size()-1;};
    std::vector<std::pair<size_t,size_t>> explicitPairs;
    if(pairList){std::set<std::pair<size_t,size_t>> seen;
        for(const auto& row:pairList->arr){need(row.t==V::Obj,"MOTION_CLEARANCE_INVALID: pair must be an object");for(const auto& field:row.obj)need(field.first=="a"||field.first=="b"||field.first=="copy_a"||field.first=="copy_b","MOTION_CLEARANCE_INVALID: unknown pair field: "+field.first);
            need(row.find("a")&&row.find("b"),"MOTION_CLEARANCE_INVALID: pair requires a and b");size_t ends[2];
            for(int side=0;side<2;++side){const u64 node=clearanceNode046(d,*row.find(side?"b":"a"),side?"b":"a");const int copy=clearanceCopy046(row,side?"copy_b":"copy_a");size_t found=program.baseline.size();
                for(size_t i=0;i<program.baseline.size();++i)if(program.baseline[i].node==node&&program.baseline[i].copy==copy)found=i;
                need(found<program.baseline.size()&&program.baseline[found].feature,"MOTION_CLEARANCE_INVALID: occurrence #"+std::to_string(node)+" copy "+std::to_string(copy)+" is not a placed solid");ends[side]=addItem(found);}
            need(ends[0]!=ends[1],"MOTION_CLEARANCE_INVALID: a pair needs two distinct occurrences");const auto key=std::minmax(ends[0],ends[1]);need(seen.insert({key.first,key.second}).second,"MOTION_CLEARANCE_INVALID: duplicate pair");explicitPairs.push_back({ends[0],ends[1]});}}
    else for(size_t i=0;i<program.baseline.size();++i)if(program.baseline[i].feature&&program.baseline[i].visible)addItem(i);
    std::map<u64,std::string> breps,brepDigests;std::map<u64,std::array<double,6>> bounds;
    for(const auto& it:items)if(!breps.count(it.feature)){
        try{ensureNativeBody(d,it.feature,d.evalBodies);}catch(const std::exception& e){throw std::runtime_error("MOTION_CLEARANCE_UNSUPPORTED_GEOMETRY: occurrence "+it.name+" (node #"+std::to_string(it.node)+") has no exact native solid: "+e.what());}
        const Body* body=nullptr;for(const auto& b:d.evalBodies)if(b.id==it.feature)body=&b;const V* brep=body?body->nativeData.find("brep_base64"):nullptr;
        need(brep&&brep->t==V::Str&&!brep->str.empty(),"MOTION_CLEARANCE_UNSUPPORTED_GEOMETRY: occurrence "+it.name+" (node #"+std::to_string(it.node)+") has no exact native solid; exact motion clearance never reinterprets meshes. Declare pairs without it");
        breps[it.feature]=brep->str;brepDigests[it.feature]=iterationSha256(brep->str);
        if(const auto* report=body->nativeData.find("report"))if(const auto* box=report->find("bounds_mm");box&&box->t==V::Arr&&box->arr.size()==6){std::array<double,6> b{};bool ok=true;for(int k=0;k<6;++k){ok=ok&&box->arr[k].t==V::Num&&std::isfinite(box->arr[k].num);b[k]=ok?box->arr[k].num:0;}if(ok&&b[3]>=b[0]&&b[4]>=b[1]&&b[5]>=b[2])bounds[it.feature]=b;}}
    // Derived evidence is cached per process by every input that can change it.
    V keyRoot=V::makeObj(),bodyKeys=V::makeArr();keyRoot.set("format",V::makeStr("ark.motion_clearance.v1"));keyRoot.set("motion",V::makeStr(name));keyRoot.set("source_digest",V::makeStr(digest));keyRoot.set("graph_digest",V::makeStr(graphDigest));keyRoot.set("fps",V::makeNum(baked.fps));keyRoot.set("request",request);
    for(const auto& [feature,hash]:brepDigests){V row=V::makeArr();row.arr.push_back(V::makeNum((double)feature));row.arr.push_back(V::makeStr(hash));bodyKeys.arr.push_back(row);}keyRoot.set("bodies",bodyKeys);canonical(keyRoot);const std::string cacheKey=iterationSha256(keyRoot.dump());
    {std::lock_guard<std::mutex> lock(g_clearanceCacheMutex046);for(auto at=g_clearanceCache046.begin();at!=g_clearanceCache046.end();++at)if(at->first==cacheKey){g_clearanceCache046.splice(g_clearanceCache046.begin(),g_clearanceCache046,at);V hit=g_clearanceCache046.front().second;
        V binding=*hit.find("bound_to");binding.set("revision",V::makeNum((double)revision));hit.set("bound_to",binding);hit.set("revision",V::makeNum((double)revision));hit.set("cached",V::makeBool(true));hit.set("cache_lookup_ms",V::makeNum(since(started)));return hit;}}
    double exactMs=0;size_t nativeRequests=0;
    for(auto& [feature,brep]:breps)if(!bounds.count(feature)){V model=V::makeObj();model.set("op",V::makeStr("collision_frames"));model.set("a_brep",V::makeStr(brep));model.set("b_brep",V::makeStr(brep));model.set("poses",V::makeArr());
        const auto t=Clock::now();const V reply=nativeModelRequest(model);exactMs+=since(t);++nativeRequests;const auto* box=reply.find("a_bounds_mm");need(box&&box->t==V::Arr&&box->arr.size()==6,"MOTION_CLEARANCE_REJECTED: native bounds missing");std::array<double,6> b{};for(int k=0;k<6;++k)b[k]=box->arr[k].num;bounds[feature]=b;}
    for(auto& it:items){const auto& b=bounds.at(it.feature);it.center={(b[0]+b[3])/2,(b[1]+b[4])/2,(b[2]+b[5])/2};it.half={(b[3]-b[0])/2+kBoxMargin046,(b[4]-b[1])/2+kBoxMargin046,(b[5]-b[2])/2+kBoxMargin046};it.radius=it.half.len();}
    // Double-precision frames from the same solver path as the bake; the stored
    // float32 frames must agree, otherwise the bake does not describe this solve.
    cancelled();auto t0=Clock::now();MotionSolvedFrames solved;need(solveMotionFrames(d,g.compiled,g.bindings,times,solved,error,true),error);const double solveMs=since(t0);
    need(solved.baseline.size()==program.baseline.size()&&solved.joints.size()==times.size(),"MOTION_CLEARANCE_REJECTED: occurrence inventory changed");for(size_t i=0;i<program.baseline.size();++i)need(solved.baseline[i].node==program.baseline[i].node&&solved.baseline[i].copy==program.baseline[i].copy,"MOTION_CLEARANCE_REJECTED: occurrence order changed");
    auto movedAt=[&](size_t i){for(const auto& frame:solved.poses)for(int e=0;e<16;++e)if(std::fabs(frame[i].m[e]-solved.baseline[i].world.m[e])>1e-9)return true;return false;};
    if(receipt){BakeBytes raw=bakeBytes(solved);bool same=raw.moving.size()==baked.nodes.size()&&iterationSha256(raw.frames)==bake->find("frames_sha256")->str&&iterationSha256(raw.coordinates)==bake->find("coordinates_sha256")->str;
        for(size_t q=0;same&&q<raw.moving.size();++q)same=solved.baseline[raw.moving[q]].node==baked.nodes[q]&&solved.baseline[raw.moving[q]].copy==baked.copies[q];
        need(same,"MOTION_BAKE_MISMATCH: the re-solved frames do not reproduce the receipt's frames_sha256 (a different solver or input); bake it again");
        fillFrames(baked,raw.frames,raw.coordinates);cacheFrames(frameKey(*bake),std::move(raw.frames),std::move(raw.coordinates),"memory");}
    double bakeDeviation=0;{size_t matched=0;for(size_t i=0;i<solved.baseline.size();++i){if(!movedAt(i))continue;size_t j=baked.nodes.size();for(size_t q=0;q<baked.nodes.size();++q)if(baked.nodes[q]==solved.baseline[i].node&&baked.copies[q]==solved.baseline[i].copy)j=q;
            need(j<baked.nodes.size(),"MOTION_BAKE_MISMATCH: occurrence "+solved.baseline[i].name+" moves in the solve but not in the bake; bake it again");++matched;
            for(size_t k=0;k<times.size();++k)for(int e:{0,1,2,4,5,6,8,9,10,12,13,14}){const double delta=std::fabs(solved.poses[k][i].m[e]-baked.poses[k*baked.nodes.size()+j].m[e]);bakeDeviation=std::max(bakeDeviation,e>=12?delta:delta*1000);}}
        need(matched==baked.nodes.size(),"MOTION_BAKE_MISMATCH: the bake stores occurrences the solve does not move; bake it again");
        need(bakeDeviation<=1e-2,"MOTION_BAKE_MISMATCH: re-solved frames differ from the stored float32 bake by "+num(bakeDeviation)+" mm; bake it again");}
    // Channel moving sets from the solved joints. A chain is proven only for
    // unconstrained rigid-side joints whose moving sets nest (a joint tree).
    std::vector<long> channelSet(g.bindings.size(),-1);std::vector<bool> channelConstrained(g.bindings.size(),false);
    for(size_t c=0;c<g.bindings.size();++c)if(g.bindings[c].mate&&solved.joints[0][c].valid){channelSet[c]=(long)solved.joints[0][c].movingSet;for(const auto& row:solved.joints)if(row[c].constrained||!row[c].valid||(long)row[c].movingSet!=channelSet[c])channelConstrained[c]=true;}
    size_t movingCount=0;
    for(auto& it:items){for(size_t c=0;c<g.bindings.size();++c)if(channelSet[c]>=0){const auto& set=solved.movingSets[(size_t)channelSet[c]];if(std::binary_search(set.begin(),set.end(),it.node)){it.channels.push_back(c);if(channelConstrained[c])it.chain=false;}}
        for(size_t x=0;x<it.channels.size();++x)for(size_t y=x+1;y<it.channels.size();++y){const auto& a=solved.movingSets[(size_t)channelSet[it.channels[x]]];const auto& b=solved.movingSets[(size_t)channelSet[it.channels[y]]];if(!std::includes(a.begin(),a.end(),b.begin(),b.end())&&!std::includes(b.begin(),b.end(),a.begin(),a.end()))it.chain=false;}
        it.moving=!it.channels.empty()||movedAt(it.index);if(it.moving&&it.channels.empty())it.chain=false;if(it.moving)it.slot=movingCount++;}
    // Candidate pairs. Scope queries skip structurally rigid pairs (one rigid
    // cluster, or proven chains with identical channel sets) and mated pairs.
    std::set<std::pair<u64,u64>> mated;for(const auto& m:d.mates){mated.insert({m.a,m.b});mated.insert({m.b,m.a});}
    std::vector<ClearancePair046> pairs;size_t skippedRigid=0,skippedMated=0,skippedComoving=0;
    auto makePair=[&](size_t a,size_t b){if(!items[a].moving)std::swap(a,b);ClearancePair046 p;p.a=a;p.b=b;p.chain=items[a].chain&&(items[b].chain||!items[b].moving);p.proven=p.chain;
        std::set_difference(items[a].channels.begin(),items[a].channels.end(),items[b].channels.begin(),items[b].channels.end(),std::back_inserter(p.onlyA));std::set_difference(items[b].channels.begin(),items[b].channels.end(),items[a].channels.begin(),items[a].channels.end(),std::back_inserter(p.onlyB));return p;};
    if(!explicitPairs.empty()){for(const auto& [a,b]:explicitPairs){need(items[a].moving||items[b].moving,"MOTION_CLEARANCE_STATIC_PAIR: neither "+items[a].name+" nor "+items[b].name+" moves in motion "+name+"; use collision_inspect for a static pair");pairs.push_back(makePair(a,b));}}
    else for(size_t a=0;a<items.size();++a)if(items[a].moving)for(size_t b=0;b<items.size();++b){if(b==a||(items[b].moving&&b<a))continue;if(scope=="static"&&items[b].moving)continue;
        if(items[a].cluster==items[b].cluster){++skippedRigid;continue;}if(mated.count({items[a].node,items[b].node})){++skippedMated;continue;}
        if(items[a].chain&&items[b].chain&&items[a].channels==items[b].channels){++skippedComoving;continue;}
        need(pairs.size()<kMaxClearancePairs046,"MOTION_CLEARANCE_CAPACITY: more than 1024 candidate pairs exceed the query capacity; declare pairs");pairs.push_back(makePair(a,b));}
    need(!pairs.empty(),"MOTION_CLEARANCE_NO_PAIRS: motion "+name+" has no candidate pair in scope "+scope+"; no clearance evidence is produced. Declare pairs explicitly");
    need(pairs.size()*(intervals+1)<=kMaxPairFrames046,"MOTION_CLEARANCE_CAPACITY: "+std::to_string(pairs.size())+" pairs over "+std::to_string(intervals+1)+" frames exceed the 1048576 pair-frame query capacity; declare pairs or split with from_frame/to_frame");
    // Samples: every frame, then subdivision poses appended.
    std::vector<ClearanceSample046> samples(times.size());
    auto storeSample=[&](ClearanceSample046& s,const MotionSolvedFrames& f,size_t k){s.world.resize(movingCount);for(const auto& it:items)if(it.moving)s.world[it.slot]=f.poses[k][it.index];s.joints=f.joints[k];};
    for(size_t k=0;k<times.size();++k){samples[k].t=times[k];samples[k].frame=(long)k;samples[k].interval=std::min(k,times.size()-2);storeSample(samples[k],solved,k);}
    auto world=[&](const ClearanceItem046& it,size_t s)->const Mat4&{return it.moving?samples[s].world[it.slot]:it.fixedWorld;};
    auto boxGap=[&](size_t p,size_t s){Vec3 c[2],h[2];for(int side=0;side<2;++side){const auto& it=items[side?pairs[p].b:pairs[p].a];const Mat4& m=world(it,s);c[side]=m.transformPoint(it.center);const double* r=m.m;
            h[side]={std::fabs(r[0])*it.half.x+std::fabs(r[4])*it.half.y+std::fabs(r[8])*it.half.z,std::fabs(r[1])*it.half.x+std::fabs(r[5])*it.half.y+std::fabs(r[9])*it.half.z,std::fabs(r[2])*it.half.x+std::fabs(r[6])*it.half.y+std::fabs(r[10])*it.half.z};}
        const double x=std::max(0.,std::fabs(c[0].x-c[1].x)-h[0].x-h[1].x),y=std::max(0.,std::fabs(c[0].y-c[1].y)-h[0].y-h[1].y),z=std::max(0.,std::fabs(c[0].z-c[1].z)-h[0].z-h[1].z);return std::sqrt(x*x+y*y+z*z);};
    // Joint excursion per channel over an interval, from each end (deg or mm).
    std::vector<double> seams;for(size_t c=0;c<g.compiled.channels.size();++c){const auto s=motionSeams(g.compiled,c);seams.insert(seams.end(),s.begin(),s.end());}std::sort(seams.begin(),seams.end());
    std::map<std::pair<size_t,size_t>,std::array<std::vector<double>,2>> excursions;
    auto excursion=[&](size_t s1,size_t s2)->const std::array<std::vector<double>,2>&{
        if(auto found=excursions.find({s1,s2});found!=excursions.end())return found->second;
        const double ta=samples[s1].t,tb=samples[s2].t;std::vector<double> ts{ta,tb};
        for(auto at=std::upper_bound(seams.begin(),seams.end(),ta);at!=seams.end()&&*at<tb;++at)ts.push_back(*at);
        for(size_t i=1;i<=kExcursionSamples046;++i)ts.push_back(ta+(tb-ta)*(double)i/(double)(kExcursionSamples046+1));
        std::sort(ts.begin(),ts.end());ts.erase(std::unique(ts.begin(),ts.end()),ts.end());
        const size_t n=g.bindings.size();const auto va=evaluateMotion(g.compiled,ta),vb=evaluateMotion(g.compiled,tb);
        std::array<std::vector<double>,2> e{std::vector<double>(n,0.),std::vector<double>(n,0.)};std::vector<double> margin(n,0.),previous(n,0.);double previousT=ta;
        for(size_t i=0;i<ts.size();++i)for(int side:{-1,1}){if(i==0&&side<0)continue;std::vector<double> slopes;const auto v=evaluateMotion(g.compiled,ts[i],&slopes,side);
            for(size_t c=0;c<n;++c){e[0][c]=std::max(e[0][c],std::fabs(v[c]-va[c]));e[1][c]=std::max(e[1][c],std::fabs(v[c]-vb[c]));}
            if(side<0)for(size_t c=0;c<n;++c)margin[c]=std::max(margin[c],.5*std::max(std::fabs(previous[c]),std::fabs(slopes[c]))*(ts[i]-previousT));else{previous=slopes;previousT=ts[i];}}
        for(size_t c=0;c<n;++c){const double tolerance=g.bindings[c].type=="revolute"?kRevoluteTolerance:kPrismaticTolerance;for(auto& side:e)side[c]+=margin[c]+tolerance;}
        return excursions.emplace(std::pair{s1,s2},std::move(e)).first->second;};
    // d: upper bound of the relative displacement of any point of the pair's
    // bounding spheres between an interval end and any time inside it.
    auto bound=[&](size_t p,size_t s1,size_t s2){auto& pr=pairs[p];const auto& A=items[pr.a];const auto& B=items[pr.b];
        const Mat4 r1=rigidInverse046(world(B,s1))*world(A,s1),r2=rigidInverse046(world(B,s2))*world(A,s2);
        const double envelope=(r2.transformPoint(A.center)-r1.transformPoint(A.center)).len()+A.radius*rotationChord046(r1,r2);
        ClearanceBound046 out{envelope,false};if(!pr.chain)return out;
        const auto& e=excursion(s1,s2);double from[2]={0,0};
        for(int side=0;side<2;++side){const size_t s=side?s2:s1;
            auto term=[&](const ClearanceItem046& x,size_t c){const auto& j=samples[s].joints[c];if(!j.valid||j.constrained||(long)j.movingSet!=channelSet[c])return -1.;if(!j.rotate)return e[side][c];
                const Vec3 centre=world(x,s).transformPoint(x.center);const double rho=(centre-j.origin).cross(j.axis).len()+x.radius;return 2*rho*std::sin(std::min(deg2rad(e[side][c]),K_PI)/2);};
            for(size_t c:pr.onlyA){const double t=term(A,c);if(t<0){pr.proven=false;return out;}from[side]+=t;}
            for(size_t c:pr.onlyB){const double t=term(B,c);if(t<0){pr.proven=false;return out;}from[side]+=t;}}
        const double joint=std::max(from[0],from[1]);
        if(envelope>joint+1e-6+1e-9*(joint+A.radius)){pr.proven=false;return out;}
        out.d=std::max(joint,envelope);out.proven=true;return out;};
    // Exact native distance, batched per pair in bounded frame chunks.
    std::map<std::pair<size_t,size_t>,ClearanceMeasure046> exact;size_t exactCount=0;
    auto value=[&](size_t p,size_t s){const auto at=exact.find({p,s});return at!=exact.end()?at->second.value:boxGap(p,s);};
    auto measure=[&](size_t p,std::vector<size_t> list){
        std::sort(list.begin(),list.end());list.erase(std::unique(list.begin(),list.end()),list.end());list.erase(std::remove_if(list.begin(),list.end(),[&](size_t s){return exact.count({p,s})!=0;}),list.end());
        const auto& A=items[pairs[p].a];const auto& B=items[pairs[p].b];
        for(size_t start=0;start<list.size();){cancelled();const size_t end=std::min(list.size(),start+kPosesPerRequest046);V poses=V::makeArr();
            for(size_t i=start;i<end;++i){V pose=V::makeArr();pose.arr.push_back(matrixJson046(world(A,list[i])));pose.arr.push_back(matrixJson046(world(B,list[i])));poses.arr.push_back(std::move(pose));}
            V model=V::makeObj();model.set("op",V::makeStr("collision_frames"));model.set("a_brep",V::makeStr(breps.at(A.feature)));model.set("b_brep",V::makeStr(breps.at(B.feature)));model.set("poses",poses);model.set("budget_ms",V::makeNum(kExactBudgetMs046));
            const auto t=Clock::now();const V reply=nativeModelRequest(model);exactMs+=since(t);++nativeRequests;
            const auto* rows=reply.find("frames");need(rows&&rows->t==V::Arr&&!rows->arr.empty()&&rows->arr.size()<=end-start,"MOTION_CLEARANCE_REJECTED: the native worker returned no measured pose");
            for(size_t r=0;r<rows->arr.size();++r){const auto& row=rows->arr[r];ClearanceMeasure046 m;m.value=row.find("min_distance_mm")->num;m.volume=row.find("common_volume_mm3")->num;m.interference=row.find("interference")->b;m.contact=m.interference||row.find("touching")->b;m.points=*row.find("nearest_points_mm");if(const auto* o=row.find("overlap_bounds_mm"))m.overlap=*o;exact[{p,list[start+r]}]=std::move(m);++exactCount;}
            start+=rows->arr.size();}};
    // Frames that the box lower bound cannot settle for the threshold and both
    // adjacent intervals are measured exactly.
    t0=Clock::now();std::vector<std::vector<ClearanceBound046>> frameBound(pairs.size());std::vector<std::vector<double>> frameGap(pairs.size());std::vector<std::vector<size_t>> mandatory(pairs.size());size_t mandatoryCount=0;
    for(size_t p=0;p<pairs.size();++p){frameGap[p].resize(intervals+1);for(size_t k=0;k<=intervals;++k)frameGap[p][k]=boxGap(p,(size_t)k0+k);frameBound[p].resize(intervals);for(size_t k=0;k<intervals;++k)frameBound[p][k]=bound(p,(size_t)k0+k,(size_t)k0+k+1);
        for(size_t k=0;k<=intervals;++k){const double adjacent=std::max(k>0?frameBound[p][k-1].d:0.,k<intervals?frameBound[p][k].d:0.);if(frameGap[p][k]<=threshold+kClearanceEpsilon046+adjacent){mandatory[p].push_back((size_t)k0+k);++mandatoryCount;}}}
    const double prefilterMs=since(t0);
    need(mandatoryCount<=kMaxExact046,"MOTION_CLEARANCE_CAPACITY: "+std::to_string(mandatoryCount)+" exact pair-frame evaluations near contact exceed the 4096 query capacity; declare fewer pairs or split the frames with from_frame/to_frame");
    for(size_t p=0;p<pairs.size();++p)measure(p,mandatory[p]);
    // Certification and bisection. A span is certified when the smaller end
    // value minus d exceeds its target; otherwise its midpoint is solved.
    std::vector<ClearanceLeaf046> leaves;std::vector<ClearanceSpan046> pending;
    auto settle=[&](const ClearanceSpan046& span,const ClearanceBound046& b){
        ClearanceLeaf046 leaf;leaf.pair=span.pair;leaf.interval=span.interval;leaf.s1=span.s1;leaf.s2=span.s2;leaf.depth=span.depth;leaf.proven=b.proven;
        const auto e1=exact.find({span.pair,span.s1}),e2=exact.find({span.pair,span.s2});const bool c1=e1!=exact.end()&&e1->second.contact,c2=e2!=exact.end()&&e2->second.contact;
        if(c1||c2){leaf.state=(c1&&e1->second.interference)||(c2&&e2->second.interference)?kInterference046:kTouching046;leaves.push_back(leaf);return;}
        const double v1=value(span.pair,span.s1),v2=value(span.pair,span.s2);leaf.lower=std::min(v1,v2)-b.d;const bool above=threshold<=0||(v1>=threshold&&v2>=threshold);
        if(above&&leaf.lower>threshold+kClearanceEpsilon046){leaf.state=kClear046;leaves.push_back(leaf);return;}
        if(!above&&leaf.lower>kClearanceEpsilon046){leaf.state=kBelow046;leaves.push_back(leaf);return;}
        if(span.depth<maxSub){pending.push_back(span);return;}
        leaf.state=leaf.lower>kClearanceEpsilon046?kUnproven046:kUncertified046;leaf.reason=leaf.lower>kClearanceEpsilon046?"threshold_not_proven_at_max_subdivisions":"bound_not_conclusive_at_max_subdivisions";leaves.push_back(leaf);};
    for(size_t p=0;p<pairs.size();++p)for(size_t k=0;k<intervals;++k)settle({p,(size_t)k0+k,(size_t)k0+k+1,(size_t)k0+k,0},frameBound[p][k]);
    double subSolveMs=0;int levels=0;size_t subPoses=0;double pathDeviation=0;
    auto giveUp=[&](const char* reason){for(const auto& span:pending){ClearanceLeaf046 leaf;leaf.pair=span.pair;leaf.interval=span.interval;leaf.s1=span.s1;leaf.s2=span.s2;leaf.depth=span.depth;const auto b=bound(span.pair,span.s1,span.s2);leaf.proven=b.proven;leaf.lower=std::min(value(span.pair,span.s1),value(span.pair,span.s2))-b.d;leaf.state=kUncertified046;leaf.reason=reason;leaves.push_back(leaf);}pending.clear();};
    while(!pending.empty()){
        cancelled();++levels;std::map<double,size_t> mids;for(const auto& span:pending)mids.emplace((samples[span.s1].t+samples[span.s2].t)/2,0);
        if(subPoses+mids.size()>kMaxSubPoses046){giveUp("subdivision_pose_capacity");break;}
        std::vector<double> merged;const double last=mids.rbegin()->first;for(double t:times){if(t>last)break;merged.push_back(t);}for(const auto& item:mids)merged.push_back(item.first);std::sort(merged.begin(),merged.end());
        t0=Clock::now();MotionSolvedFrames sub;need(solveMotionFrames(d,g.compiled,g.bindings,merged,sub,error,true),"MOTION_CLEARANCE_REJECTED: subdivision solve failed: "+error);subSolveMs+=since(t0);
        for(size_t i=0;i<merged.size();++i){const auto mid=mids.find(merged[i]);
            if(mid==mids.end()){const size_t k=(size_t)(std::lower_bound(times.begin(),times.end(),merged[i])-times.begin());for(const auto& it:items)if(it.moving)for(int e=0;e<16;++e)pathDeviation=std::max(pathDeviation,std::fabs(sub.poses[i][it.index].m[e]-samples[k].world[it.slot].m[e])*(e>=12?1:1000));continue;}
            ClearanceSample046 s;s.t=merged[i];storeSample(s,sub,i);mid->second=samples.size();samples.push_back(std::move(s));++subPoses;}
        need(pathDeviation<=1e-5,"MOTION_CLEARANCE_REJECTED: the subdivision solve reached a baked frame "+num(pathDeviation)+" mm away from the frame solve; the joint path is not reproducible and no clearance result was published");
        std::vector<std::pair<ClearanceSpan046,ClearanceSpan046>> children;std::vector<std::vector<size_t>> needed(pairs.size());size_t neededCount=0;
        for(const auto& span:pending){const size_t mid=mids.at((samples[span.s1].t+samples[span.s2].t)/2);samples[mid].interval=span.interval;
            const ClearanceSpan046 a{span.pair,span.s1,mid,span.interval,span.depth+1},b{span.pair,mid,span.s2,span.interval,span.depth+1};
            if(boxGap(span.pair,mid)<=threshold+kClearanceEpsilon046+std::max(bound(span.pair,a.s1,a.s2).d,bound(span.pair,b.s1,b.s2).d)){needed[span.pair].push_back(mid);++neededCount;}children.push_back({a,b});}
        if(exactCount+neededCount>kMaxExact046){giveUp("exact_evaluation_capacity");break;}
        for(size_t p=0;p<pairs.size();++p)measure(p,needed[p]);
        pending.clear();for(const auto& [a,b]:children){settle(a,bound(a.pair,a.s1,a.s2));settle(b,bound(b.pair,b.s1,b.s2));}
    }
    // Exact minimum over the frames: branch and bound on the box lower bounds.
    double best=std::numeric_limits<double>::infinity();for(const auto& [key,m]:exact)best=std::min(best,m.value);bool minimumExact=true;
    {std::vector<size_t> order(pairs.size());std::vector<double> minGap(pairs.size());for(size_t p=0;p<pairs.size();++p){order[p]=p;minGap[p]=*std::min_element(frameGap[p].begin(),frameGap[p].end());}std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return minGap[a]<minGap[b];});
        for(size_t p:order){if(minGap[p]>=best)break;std::vector<std::pair<double,size_t>> candidates;for(size_t k=0;k<=intervals;++k)if(!exact.count({p,(size_t)k0+k}))candidates.push_back({frameGap[p][k],(size_t)k0+k});std::sort(candidates.begin(),candidates.end());
            for(size_t i=0;i<candidates.size()&&candidates[i].first<best;){std::vector<size_t> list;while(i<candidates.size()&&candidates[i].first<best&&list.size()<16)list.push_back(candidates[i++].second);
                if(exactCount+list.size()>kMaxExact046){minimumExact=false;break;}measure(p,list);for(size_t s:list)best=std::min(best,exact.at({p,s}).value);}
            if(!minimumExact)break;}}
    // Report.
    auto who=[&](size_t i){const auto& it=items[i];V row=V::makeObj();row.set("node",V::makeNum((double)it.node));row.set("copy",V::makeNum(it.copy));row.set("name",V::makeStr(it.name));return row;};
    auto nearestFrame=[&](size_t s){return samples[s].frame>=0?(size_t)samples[s].frame:(size_t)std::lround(samples[s].t*baked.fps);};
    std::vector<int> state(intervals,kClear046);for(const auto& leaf:leaves)state[leaf.interval-(size_t)k0]=std::max(state[leaf.interval-(size_t)k0],leaf.state);
    for(const auto& [key,m]:exact)if(!m.contact&&m.value<threshold){const auto& s=samples[key.second];if(s.frame>=0){for(long k:{s.frame-1,s.frame})if(k>=k0&&k<k1)state[(size_t)(k-k0)]=std::max(state[(size_t)(k-k0)],(int)kBelow046);}else state[s.interval-(size_t)k0]=std::max(state[s.interval-(size_t)k0],(int)kBelow046);}
    bool allFree=true,allClear=true,allProven=true;double lowest=std::numeric_limits<double>::infinity();
    for(const auto& leaf:leaves){allFree=allFree&&leaf.state<kUncertified046;allClear=allClear&&leaf.state==kClear046;allProven=allProven&&leaf.proven;lowest=std::min(lowest,leaf.state>=kTouching046?0.:leaf.lower);}
    for(const auto& p:pairs)allProven=allProven&&p.proven;
    V report=V::makeObj();report.set("motion",V::makeStr(name));report.set("revision",V::makeNum((double)revision));
    V binding=V::makeObj();binding.set("revision",V::makeNum((double)revision));binding.set("source_digest",V::makeStr(digest));binding.set("graph_digest",V::makeStr(graphDigest));binding.set("bake_source_revision",*bake->find("source_revision"));report.set("bound_to",binding);
    report.set("scope",V::makeStr(scope));report.set("threshold_mm",V::makeNum(threshold));report.set("max_subdivisions",V::makeNum(maxSub));report.set("fps",V::makeNum(baked.fps));report.set("frame_count",V::makeNum((double)times.size()));report.set("from_frame",V::makeNum((double)k0));report.set("to_frame",V::makeNum((double)k1));
    report.set("continuous_certified",V::makeBool(allFree&&allProven));report.set("threshold_certified",V::makeBool(allClear&&allProven));if(!allProven)report.set("envelope_assumption_certified",V::makeBool(allFree));
    report.set("continuous_lower_bound_mm",V::makeNum(std::isfinite(lowest)?lowest:0));report.set("continuous_lower_bound_proven",V::makeBool(allProven));
    report.set("min_clearance_mm",std::isfinite(best)?V::makeNum(std::max(0.,best)):V{});report.set("min_clearance_exact_over_frames",V::makeBool(minimumExact&&std::isfinite(best)));
    const ClearanceMeasure046* worst=nullptr;std::pair<size_t,size_t> worstKey{0,0};for(const auto& [key,m]:exact)if(!worst||m.value<worst->value-1e-12||(std::fabs(m.value-worst->value)<=1e-12&&m.volume>worst->volume)){worst=&m;worstKey=key;}
    if(worst){V row=V::makeObj();row.set("a",who(pairs[worstKey.first].a));row.set("b",who(pairs[worstKey.first].b));row.set("clearance_mm",V::makeNum(worst->value));row.set("status",V::makeStr(worst->interference?"interference":worst->contact?"touching":worst->value<threshold?"below_threshold":"clear"));
        row.set("time_s",V::makeNum(samples[worstKey.second].t));row.set("frame",V::makeNum((double)nearestFrame(worstKey.second)));row.set("at_baked_frame",V::makeBool(samples[worstKey.second].frame>=0));row.set("nearest_points_mm",worst->points);if(worst->volume>0)row.set("common_volume_mm3",V::makeNum(worst->volume));report.set("worst",row);}
    V contacts=V::makeArr(),between=V::makeArr();size_t contactPairFrames=0,betweenContacts=0,belowPairFrames=0;std::set<size_t> contactFrames;
    for(const auto& [key,m]:exact){const auto& s=samples[key.second];if(!m.contact){if(m.value<threshold&&s.frame>=0)++belowPairFrames;continue;}
        V row=V::makeObj();row.set("a",V::makeNum((double)items[pairs[key.first].a].node));row.set("b",V::makeNum((double)items[pairs[key.first].b].node));row.set("status",V::makeStr(m.interference?"interference":"touching"));row.set("time_s",V::makeNum(s.t));row.set("common_volume_mm3",V::makeNum(m.volume));if(!m.overlap.isNull())row.set("overlap_bounds_mm",m.overlap);
        if(s.frame>=0){++contactPairFrames;contactFrames.insert((size_t)s.frame);row.set("frame",V::makeNum((double)s.frame));if(contacts.arr.size()<kMaxListed046)contacts.arr.push_back(row);}
        else{++betweenContacts;row.set("from_frame",V::makeNum((double)s.interval));row.set("to_frame",V::makeNum((double)s.interval+1));if(between.arr.size()<kMaxListed046)between.arr.push_back(row);}}
    report.set("interference_frames",contacts);report.set("contact_pair_frames",V::makeNum((double)contactPairFrames));report.set("contact_frame_count",V::makeNum((double)contactFrames.size()));
    report.set("between_frame_contacts",between);report.set("between_frame_contact_count",V::makeNum((double)betweenContacts));report.set("below_threshold_pair_frames",V::makeNum((double)belowPairFrames));
    V uncertified=V::makeArr();size_t uncertifiedCount=0;for(const auto& leaf:leaves)if(leaf.state==kUncertified046){++uncertifiedCount;if(uncertified.arr.size()<kMaxListed046){V row=V::makeObj();row.set("a",V::makeNum((double)items[pairs[leaf.pair].a].node));row.set("b",V::makeNum((double)items[pairs[leaf.pair].b].node));
        row.set("from_frame",V::makeNum((double)leaf.interval));row.set("to_frame",V::makeNum((double)leaf.interval+1));row.set("from_s",V::makeNum(samples[leaf.s1].t));row.set("to_s",V::makeNum(samples[leaf.s2].t));row.set("lower_bound_mm",V::makeNum(leaf.lower));row.set("depth",V::makeNum(leaf.depth));row.set("reason",V::makeStr(leaf.reason));uncertified.arr.push_back(row);}}
    report.set("uncertified_intervals",uncertified);report.set("uncertified_interval_count",V::makeNum((double)uncertifiedCount));
    V markers=V::makeArr();size_t markerRuns=0;for(size_t k=0;k<intervals;){if(!state[k]){++k;continue;}size_t e=k;while(e+1<intervals&&state[e+1]==state[k])++e;++markerRuns;
        if(markers.arr.size()<256){V row=V::makeObj();row.set("status",V::makeStr(clearanceStateName046(state[k])));row.set("from_frame",V::makeNum((double)(k0+(long)k)));row.set("to_frame",V::makeNum((double)(k0+(long)e+1)));row.set("from_s",V::makeNum(times[(size_t)k0+k]));row.set("to_s",V::makeNum(times[(size_t)k0+e+1]));markers.arr.push_back(row);}k=e+1;}
    report.set("markers",markers);report.set("marker_runs",V::makeNum((double)markerRuns));
    std::vector<size_t> order(pairs.size());std::vector<double> pairMinimum(pairs.size(),std::numeric_limits<double>::infinity());for(size_t p=0;p<pairs.size();++p){order[p]=p;for(size_t k=0;k<=intervals;++k)pairMinimum[p]=std::min(pairMinimum[p],value(p,(size_t)k0+k));}
    std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return pairMinimum[a]<pairMinimum[b];});
    V pairRows=V::makeArr();for(size_t p:order){if(pairRows.arr.size()>=32)break;const auto& pr=pairs[p];V row=V::makeObj();row.set("a",who(pr.a));row.set("b",who(pr.b));row.set("moving",V::makeStr(items[pr.b].moving?"both":"a"));row.set("bound_model",V::makeStr(pr.chain?"joint_chain":"endpoint_envelope"));row.set("bound_proven",V::makeBool(pr.proven));
        double exactMin=std::numeric_limits<double>::infinity();size_t at=0;int worstState=0;size_t pairUncertified=0;for(const auto& [key,m]:exact)if(key.first==p&&m.value<exactMin){exactMin=m.value;at=key.second;}
        for(const auto& leaf:leaves)if(leaf.pair==p){worstState=std::max(worstState,leaf.state);pairUncertified+=leaf.state==kUncertified046;}
        row.set("frame_minimum_lower_bound_mm",V::makeNum(pairMinimum[p]));if(std::isfinite(exactMin)){row.set("min_clearance_mm",V::makeNum(exactMin));row.set("time_s",V::makeNum(samples[at].t));row.set("frame",V::makeNum((double)nearestFrame(at)));}
        row.set("status",V::makeStr(clearanceStateName046(worstState)));row.set("uncertified_intervals",V::makeNum((double)pairUncertified));pairRows.arr.push_back(row);}
    report.set("pairs",pairRows);report.set("pairs_checked",V::makeNum((double)pairs.size()));report.set("pairs_listed",V::makeNum((double)pairRows.arr.size()));
    if(scope!="pairs"){V skipped=V::makeObj();skipped.set("rigid_cluster",V::makeNum((double)skippedRigid));skipped.set("mated",V::makeNum((double)skippedMated));skipped.set("co_moving_chain",V::makeNum((double)skippedComoving));skipped.set("note",V::makeStr("pairs whose relative pose the joint structure fixes, and directly mated pairs, are not motion clearance; check them with collision_inspect or declare pairs"));report.set("pairs_skipped",skipped);}
    V moving=V::makeArr();for(size_t i=0;i<items.size();++i)if(items[i].moving){V row=who(i);V channels=V::makeArr();for(size_t c:items[i].channels)channels.arr.push_back(V::makeStr(g.compiled.channels[c].name));row.set("channels",channels);row.set("joint_chain",V::makeBool(items[i].chain));moving.arr.push_back(row);}report.set("moving_instances",moving);
    V counts=V::makeObj();counts.set("frames",V::makeNum((double)(intervals+1)));counts.set("pairs",V::makeNum((double)pairs.size()));counts.set("pair_frames",V::makeNum((double)(pairs.size()*(intervals+1))));size_t exactFrames=0;for(const auto& [key,m]:exact)exactFrames+=samples[key.second].frame>=0;
    counts.set("exact_pair_frames",V::makeNum((double)exactFrames));counts.set("box_settled_pair_frames",V::makeNum((double)(pairs.size()*(intervals+1)-exactFrames)));counts.set("exact_pair_subposes",V::makeNum((double)(exactCount-exactFrames)));counts.set("sub_poses",V::makeNum((double)subPoses));counts.set("subdivision_levels",V::makeNum(levels));counts.set("native_requests",V::makeNum((double)nativeRequests));counts.set("certified_leaves",V::makeNum((double)leaves.size()));report.set("evaluations",counts);
    V timing=V::makeObj();timing.set("frame_solve",V::makeNum(solveMs));timing.set("prefilter_and_bounds",V::makeNum(prefilterMs));timing.set("exact_native",V::makeNum(exactMs));timing.set("subdivision_solve",V::makeNum(subSolveMs));timing.set("total",V::makeNum(since(started)));report.set("timing_ms",timing);
    V method=V::makeObj();method.set("poses",V::makeStr("double-precision re-solve of every baked frame through the bake joint solver; stored float32 frames must agree within 0.01 mm"));method.set("bake_pose_max_deviation_mm",V::makeNum(bakeDeviation));method.set("bake_frames",V::makeStr(receipt?"derived receipt: the re-solve reproduces frames_sha256 and coordinates_sha256":"inline 044B-048B float32 payload"));method.set("subdivision_path_deviation_mm",V::makeNum(pathDeviation));
    method.set("distance",V::makeStr("exact native B-rep minimum distance per pose; boolean common volume where the distance is within the 1e-6 mm contact tolerance"));
    method.set("prefilter",V::makeStr("world AABB of native local bounds enlarged by 1e-3 mm; its gap is a lower bound of the exact distance"));
    method.set("certification",V::makeStr("interval [a,b] certified when min(V(a),V(b)) - d > target + 1e-6 mm; V is the exact distance or the box lower bound; target is threshold_mm when both ends are at or above it, otherwise 0 (contact free)"));
    method.set("bound",V::makeStr("joint_chain: d = max over both ends of the sum, over joints moving only one body of the pair, of 2*(dist(sphere centre, solved joint axis)+radius)*sin(E/2) for revolute or E for prismatic, E the joint excursion inside the interval; endpoint_envelope (assumed, never proven): |dc| + r*||R2-R1|| of the relative transform"));
    method.set("excursion",V::makeStr("analytic graph at the interval ends, every segment seam on both sides and 8 interior points, plus half the larger neighbouring slope times each sub-step and the joint solver tolerance"));
    method.set("subdivision",V::makeStr("bisection at the interval midpoint, solved by the bake joint solver along the same frame path, up to max_subdivisions levels"));
    method.set("rigid_bodies_only",V::makeBool(true));method.set("ideal_kinematics",V::makeBool(true));report.set("method",method);
    report.set("document_unchanged",V::makeBool(true));report.set("persisted",V::makeBool(false));report.set("dynamics_checked",V::makeBool(false));report.set("contact_mechanics_checked",V::makeBool(false));report.set("manufacturing_qualified",V::makeBool(false));report.set("cached",V::makeBool(false));
    {std::lock_guard<std::mutex> lock(g_clearanceCacheMutex046);g_clearanceCache046.push_front({cacheKey,report});while(g_clearanceCache046.size()>kClearanceCacheEntries046)g_clearanceCache046.pop_back();}
    return report;
}
