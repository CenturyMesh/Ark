// 059C / A.02 B.03 D.02: typed exact-surface forms with captured revision and
// epoch. Include after surfaceEditDialog; all writes use the existing atomic API.
namespace surface_workflow_059c {
using V=arkjson::Value;
static std::function<void()> beforePublishTest;
struct Context {
    u64 revision=g_session.revision,epoch=g_documentEpoch;
    std::optional<Feature> source;
    explicit Context(const Feature* editing){if(editing)source=*editing;}
    void current() const {if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Drawing changed while editing the surface. Reopen the current feature.");}
    V native() const{return source?source->nativeSpec:V::makeObj();}
    V parameters() const {V p=V::makeObj();if(source)for(const auto& item:source->p)p.set(item.first,V::makeStr(item.second));return p;}
    std::string name(const char* fallback)const{return source?source->name:fallback;}
    std::string input(size_t index)const{return source&&index<source->inputs.size()?std::to_string(source->inputs[index]):index==0?selectedFeatureId():"";}
};
static std::string value(const V& obj,const char* key,const char* fallback="") {const auto* v=obj.find(key);return v?(v->t==V::Str?v->str:v->dump()):fallback;}
static void erase(V& obj,const char* key){obj.obj.erase(std::remove_if(obj.obj.begin(),obj.obj.end(),[&](const auto& p){return p.first==key;}),obj.obj.end());}
static std::vector<std::string> split(const std::string& text,char delimiter){std::vector<std::string> out;std::istringstream stream(text);std::string part;while(std::getline(stream,part,delimiter)){part=trimField(part);if(!part.empty())out.push_back(part);}return out;}
static int integer(const std::string& text,int low,int high,const char* label){double n=formNumber(text);if(n!=std::floor(n)||n<low||n>high)throw std::runtime_error(std::string(label)+" is outside its integer range");return (int)n;}
static std::string bitValue(const V& obj,const char* key){return integer(value(obj,key,"0"),0,1,key)?"1":"0";}
static bool toggle(const EditorField& f){if(f.value!="0"&&f.value!="1")throw std::runtime_error("Invalid checkbox value");return f.value=="1";}
static V xyz(const std::string& text,bool normal=false){auto parts=split(text,',');if(parts.size()!=3||std::count(text.begin(),text.end(),',')!=2)throw std::runtime_error("Coordinates need three comma-separated numbers or expressions");V result=V::makeArr();double norm=0;for(const auto& part:parts){const double n=formNumber(part);if(std::abs(n)>1e9)throw std::runtime_error("Coordinate exceeds supported range");norm+=n*n;result.arr.push_back(V::makeStr(part));}if(normal&&norm<1e-20)throw std::runtime_error("Plane normal cannot be zero");return result;}
static std::string xyzText(const V* v,const char* fallback){if(!v||v->t!=V::Arr||v->arr.size()!=3)return fallback;std::string out;for(const auto& item:v->arr){if(!out.empty())out+=", ";out+=item.t==V::Str?item.str:item.dump();}return out;}
static u64 sourceId(const Context& context,const std::string& text,bool curve=false){
    const u64 id=featureIdField(text);const Feature* found=g_doc.find(id);
    if(!found||!isNativeFeature(found->type))throw std::runtime_error("Choose an existing native source feature");
    if(curve&&!isCurveFeature(found->type))throw std::runtime_error("Choose an exact 3D curve feature");
    if(context.source){for(const auto& feature:g_doc.features){if(feature.id==context.source->id)break;if(feature.id==id)return id;}throw std::runtime_error("A surface input must precede the edited feature");}
    return id;
}
static void publish(const Context& context,FeatureType type,const std::string& title,V p,V native,const std::vector<u64>& inputs){
    if(beforePublishTest)beforePublishTest();context.current();const auto name=trimField(title);if(name.empty())throw std::runtime_error("Name the resulting surface");
    V command=V::makeObj(),parents=V::makeArr();command.set("op",V::makeStr(context.source?"feature.edit":"feature.add"));
    if(context.source)command.set("id",V::makeNum(context.source->id));else command.set("type",V::makeStr(featureTypeName(type)));
    if(context.source){V removed=V::makeArr();for(const auto& item:context.source->p)if(!p.find(item.first.c_str()))removed.arr.push_back(V::makeStr(item.first));if(!removed.arr.empty())command.set("p_remove",removed);}
    command.set("name",V::makeStr(name));command.set("p",std::move(p));command.set("native",std::move(native));
    for(u64 id:inputs)parents.arr.push_back(V::makeNum(id));command.set("inputs",parents);
    applyUiCommandAtRevision(std::move(command),context.revision,context.epoch);
    if(!g_error){g_activeSketch.clear();g_showSketches=false;g_showEdges=true;if(!context.source)selBody=(int)g_bodyIds.size()-1;frameScene();g_status="Exact surface saved / inspect the measured native report";}
}
static void mirror(const Feature* editing){
    Context c(editing);V native=c.native();std::vector<EditorField> f{{"Result name",c.name("Mirrored surface")},pickField("Source surface",c.input(0),4),
        {"Plane / blank: custom",value(native,"plane",native.find("origin")?"":"XZ")},{"Origin X,Y,Z / mm",xyzText(native.find("origin"),"0, 0, 0")},
        {"Normal X,Y,Z",xyzText(native.find("normal"),"0, 1, 0")},choiceField("Seam continuity",value(native,"continuity","G0"),{"G0","G1"})};
    if(!editFields(editing?"Edit mirrored surface":"Mirror and join surface","Join a half-surface at its plane seam. XZ means Y=0; blank plane uses origin/normal. G1 is checked, without smoothing.",f))return;c.current();
    const auto source=sourceId(c,f[1].value);native.set("op",V::makeStr("mirror"));erase(native,"plane");erase(native,"origin");erase(native,"normal");
    const auto plane=trimField(f[2].value);if(plane.empty()){native.set("origin",xyz(f[3].value));native.set("normal",xyz(f[4].value,true));}else{resolvePlane(g_doc,plane);native.set("plane",V::makeStr(plane));}
    if(f[5].value!="G0"&&f[5].value!="G1")throw std::runtime_error("Mirror continuity must be G0 or G1");native.set("continuity",V::makeStr(f[5].value));publish(c,FeatureType::SurfaceEdit,f[0].value,c.parameters(),native,{source});
}
static void trimTool(const Feature* editing){
    Context c(editing);V native=c.native();std::vector<EditorField> f{{"Result name",c.name("Trimmed surface")},pickField("Source surface",c.input(0),4),pickField("Trimming tool",c.input(1),5),
        choiceField("Tool geometry",value(native,"tool_kind","surface"),{"surface","solid","curve"}),{"Keep X,Y,Z / local mm",xyzText(native.find("keep_point"),"0, 0, 0")}};
    if(!editFields(editing?"Edit surface trim tool":"Trim surface with exact tool","Keep point selects one region; all others are discarded. Use source-local mm. Curve tools must lie on the surface.",f))return;c.current();
    const auto source=sourceId(c,f[1].value),tool=sourceId(c,f[2].value,f[3].value=="curve");if(source==tool)throw std::runtime_error("Choose distinct source and tool features");
    if(f[3].value!="surface"&&f[3].value!="solid"&&f[3].value!="curve")throw std::runtime_error("Choose surface, solid or curve tool geometry");
    native.set("op",V::makeStr("trim_tool"));native.set("tool_kind",V::makeStr(f[3].value));native.set("keep_point",xyz(f[4].value));publish(c,FeatureType::SurfaceEdit,f[0].value,c.parameters(),native,{source,tool});
}
static void ruled(const Feature* editing){
    Context c(editing);V native=c.native();std::vector<EditorField> f{{"Result name",c.name("Ruled surface")},pickField("First exact curve",c.input(0),4),pickField("Second exact curve",c.input(1),5),{"Reverse second curve",native.find("reverse_second")&&native.find("reverse_second")->b?"1":"0",true}};
    if(!editFields(editing?"Edit ruled surface":"Ruled surface between 3D curves","Choose two open single-edge 3D wires. Reverse second changes their endpoint correspondence explicitly.",f))return;c.current();
    const auto a=sourceId(c,f[1].value,true),b=sourceId(c,f[2].value,true);if(a==b)throw std::runtime_error("Choose two distinct exact curves");
    native.set("op",V::makeStr("ruled"));native.set("reverse_second",V::makeBool(toggle(f[3])));publish(c,FeatureType::SurfaceEdit,f[0].value,c.parameters(),native,{a,b});
}
static V profiles(const Context& c,const std::string& text,bool guides,std::vector<u64>& inputs){
    V result=V::makeArr();for(const auto& name:split(text,';')){
        int matches=g_doc.findSketch(name)?1:0;const Feature* curve=nullptr;
        if(!guides)for(const auto& plane:g_doc.planes)if(plane.name==name&&plane.kind=="point")++matches;
        for(const auto& feature:g_doc.features)if(feature.name==name&&isCurveFeature(feature.type)){++matches;curve=&feature;}
        if(matches!=1)throw std::runtime_error("Profile name is missing or ambiguous: "+name);
        if(curve){const u64 id=sourceId(c,std::to_string(curve->id),true);if(std::find(inputs.begin(),inputs.end(),id)==inputs.end())inputs.push_back(id);}
        result.arr.push_back(V::makeStr(name));
    }
    if(result.arr.size()>(guides?8u:32u)||(!guides&&result.arr.size()<2))throw std::runtime_error(guides?"Loft accepts at most 8 guides":"Loft requires 2..32 sections");return result;
}
static void loft(const Feature* editing){
    Context c(editing);V native=c.native(),p=c.parameters();V controls=native.find("controls")?*native.find("controls"):V::makeObj();
    std::string initial=joinedSketchList(native.find("sections"));if(initial.empty())initial=pickedSketches();
    const std::string continuity=value(p,"continuity");
    std::vector<EditorField> f{{"Result name",c.name("Surface loft")},pickField("Sections / ; names",initial,3),{"Guides / ; names",joinedSketchList(native.find("guides"))},
        {"Solid",bitValue(p,"solid"),true},{"Ruled",bitValue(p,"ruled"),true},{"Smooth",bitValue(p,"smooth"),true},{"Compatible sections",bitValue(p,"compatible"),true},
        {"Max degree / optional",value(p,"max_degree")},{"Continuity / 0,1,2",continuity},
        {"Guide checkpoints",value(controls,"guide_samples","17")},{"Guide error / mm",value(controls,"max_guide_deviation_mm","0.05")},
        {"Ordered contacts",controls.find("require_ordered_contacts")&&!controls.find("require_ordered_contacts")->b?"0":"1",true}};
    if(!editFields(editing?"Edit surface loft and guides":"Loft through sketches, 3D curves or tips","Use ordered sketch, 3D curve or point names; separate with ;. Guides use sketch/curve names. Existing seams are retained.",f))return;c.current();
    std::vector<u64> inputs;auto sections=profiles(c,f[1].value,false,inputs),guides=profiles(c,f[2].value,true,inputs);native.set("sections",sections);
    if(const auto* alignment=controls.find("section_alignment");alignment&&alignment->arr.size()!=sections.arr.size())throw std::runtime_error("Retained section alignment count differs from the new section count; edit alignment explicitly first");
    const char* bits[]={"solid","ruled","smooth","compatible"};for(size_t i=0;i<4;++i){toggle(f[3+i]);if(!p.find(bits[i])||f[3+i].value!=bitValue(p,bits[i]))p.set(bits[i],V::makeStr(f[3+i].value));}
    if(trimField(f[7].value).empty())erase(p,"max_degree");else{integer(f[7].value,1,25,"Maximum degree");p.set("max_degree",V::makeStr(f[7].value));}
    if(trimField(f[8].value).empty())erase(p,"continuity");else{integer(f[8].value,0,2,"Loft continuity");p.set("continuity",V::makeStr(f[8].value));}
    if(guides.arr.empty()){erase(native,"guides");for(const char* key:{"guide_samples","max_guide_deviation_mm","require_ordered_contacts"})erase(controls,key);}
    else{native.set("guides",guides);integer(f[9].value,2,129,"Guide checkpoints");double error=formNumber(f[10].value);if(error<1e-6||error>.05)throw std::runtime_error("Guide error must be within 1e-6..0.05 mm");controls.set("guide_samples",V::makeStr(f[9].value));controls.set("max_guide_deviation_mm",V::makeStr(f[10].value));controls.set("require_ordered_contacts",V::makeBool(toggle(f[11])));}
    if(controls.obj.empty())erase(native,"controls");else native.set("controls",controls);publish(c,FeatureType::Loft,f[0].value,p,native,inputs);
}
static void verifySupport(u64 id,const std::string& face){
    const Body* body=nullptr;for(const auto& candidate:g_doc.evalBodies)if(candidate.id==id){body=&candidate;break;}
    const V* topology=body?body->nativeData.find("topology"):nullptr;const V* digest=topology?topology->find("brep_sha256"):nullptr;const V* refs=topology?topology->find("references"):nullptr;
    if(!digest||digest->t!=V::Str||digest->str.size()!=64||!refs||refs->t!=V::Arr)throw std::runtime_error("Support has no current exact producer topology; inspect or evaluate it first");
    int matches=0;for(const auto& ref:refs->arr)if(value(ref,"name")==face&&value(ref,"kind")=="face"&&value(ref,"status")=="bound"){
        const auto* indices=ref.find("indices");if(indices&&indices->t==V::Arr&&indices->arr.size()==1)++matches;
    }
    if(matches!=1)throw std::runtime_error("Support face must be one current, unambiguous bound producer reference");
}
static void fill(const Feature* editing){
    Context c(editing);V native=c.native(),p=c.parameters();const V* old=native.find("constraints");const V constraints=old?*old:V::makeArr();
    const V validation=native.find("validation")?*native.find("validation"):V::makeObj();
    const std::string kind=native.find("boundary_curve")?"curve":native.find("profile")?"retained profile":"sketch";
    std::string guideText;if(const auto* guides=native.find("guide_curves"))for(const auto& guide:guides->arr){if(!guideText.empty())guideText+="; ";guideText+=guide.t==V::Str?guide.str:guide.dump();}
    std::vector<EditorField> f{{"Result name",c.name("Constrained surface patch")},choiceField("Boundary source",kind,editing?std::vector<std::string>{"sketch","curve","retained profile"}:std::vector<std::string>{"sketch","curve"}),
        {"Sketch name / curve ID",kind=="curve"?value(native,"boundary_curve"):value(p,"sketch",g_activeSketch.c_str())},choiceField("Patch mode",value(native,"op")=="blend"?"blend":"fill",{"fill","blend"}),
        {"Supported edge count",std::to_string(constraints.arr.size())},{"Checkpoints",value(validation,"samples","33")},{"Tangent limit / deg",value(validation,"tangent_deg","0.001")},{"Curvature limit / mm^-1",value(validation,"curvature_per_mm","0.0001")},
        {"G0 guide IDs / ;",guideText}};
    if(!editFields(editing?"Edit supported surface patch":"Fill boundary with G1/G2 supports","Use a closed boundary. G0 guides shape the interior; following forms set G1/G2 edge supports. Cancel abandons every step.",f))return;c.current();
    std::vector<u64> inputs;const auto boundary=trimField(f[2].value);
    if(f[1].value=="sketch"){if(!g_doc.findSketch(boundary))throw std::runtime_error("Choose an existing closed sketch");p.set("sketch",V::makeStr(boundary));erase(native,"profile");erase(native,"boundary_curve");}
    else if(f[1].value=="curve"){const auto id=sourceId(c,boundary,true);inputs.push_back(id);native.set("boundary_curve",V::makeNum(id));erase(native,"profile");erase(p,"sketch");}
    else if(f[1].value!="retained profile"||!c.source||!native.find("profile"))throw std::runtime_error("No explicit retained boundary profile is available");
    V guides=V::makeArr();std::set<u64> guideIds;for(const auto& entry:split(f[8].value,';')){const auto id=sourceId(c,entry,true);if(!guideIds.insert(id).second)throw std::runtime_error("Use each interior guide curve only once");if(guides.arr.size()==16)throw std::runtime_error("Patch accepts at most 16 interior guide curves");guides.arr.push_back(V::makeNum(id));if(std::find(inputs.begin(),inputs.end(),id)==inputs.end())inputs.push_back(id);}
    if(guides.arr.empty())erase(native,"guide_curves");else native.set("guide_curves",guides);
    const int count=integer(f[4].value,0,128,"Supported edge count");integer(f[5].value,3,129,"Continuity checkpoints");
    const double tangent=formNumber(f[6].value),curvature=formNumber(f[7].value);if(tangent<=0||tangent>.1||curvature<=0||curvature>.01)throw std::runtime_error("Continuity limits require 0<tangent<=0.1 deg and 0<curvature<=0.01/mm");
    V next=V::makeArr();std::set<int> edges;std::set<std::pair<u64,std::string>> supportFaces;
    for(int i=0;i<count;++i){const V row=(size_t)i<constraints.arr.size()?constraints.arr[(size_t)i]:V::makeObj();
        std::vector<EditorField> fields{{"Edge index / from 0",value(row,"edge",std::to_string(i).c_str())},pickField("Support feature ID",value(row,"support"),4),
            pickField("Exact semantic face",value(row,"face"),1),choiceField("Requested continuity",integer(value(row,"continuity","1"),1,2,"Support continuity")==2?"G2":"G1",{"G1","G2"})};
        std::string title="Boundary support "+std::to_string(i+1)+" of "+std::to_string(count);
        if(!editFields(title.c_str(),"Bind one boundary edge to a current exact face. G1 checks tangent planes; G2 checks curvature too, at finite checkpoints.",fields))return;c.current();
        const int edge=integer(fields[0].value,0,127,"Boundary edge");if(!edges.insert(edge).second)throw std::runtime_error("A boundary edge may have only one support");const auto id=sourceId(c,fields[1].value);const auto face=trimField(fields[2].value);verifySupport(id,face);supportFaces.emplace(id,face);
        if(fields[3].value!="G1"&&fields[3].value!="G2")throw std::runtime_error("Support continuity must be G1 or G2");const int continuity=fields[3].value=="G2"?2:1;V item=V::makeObj();item.set("edge",V::makeStr(fields[0].value));item.set("support",V::makeNum(id));item.set("face",V::makeStr(face));item.set("continuity",row.find("continuity")&&integer(value(row,"continuity"),1,2,"Support continuity")==continuity?*row.find("continuity"):V::makeNum(continuity));next.arr.push_back(item);if(std::find(inputs.begin(),inputs.end(),id)==inputs.end())inputs.push_back(id);
    }
    if(f[3].value=="blend"){if(supportFaces.size()<2)throw std::runtime_error("Blend needs at least two distinct support faces");native.set("op",V::makeStr("blend"));}else if(f[3].value=="fill")erase(native,"op");else throw std::runtime_error("Patch mode must be fill or blend");
    if(next.arr.empty()){erase(native,"constraints");erase(native,"validation");}else{native.set("constraints",next);V settings=validation;settings.set("samples",V::makeStr(f[5].value));settings.set("tangent_deg",V::makeStr(f[6].value));settings.set("curvature_per_mm",V::makeStr(f[7].value));native.set("validation",settings);}
    publish(c,FeatureType::SurfaceFill,f[0].value,p,native,inputs);
}
} // namespace surface_workflow_059c
static bool surfaceWorkflowAction059(int id){using namespace surface_workflow_059c;switch(id){case 49400:mirror(nullptr);return true;case 49401:trimTool(nullptr);return true;case 49402:ruled(nullptr);return true;case 49403:loft(nullptr);return true;case 49404:fill(nullptr);return true;default:return false;}}
static bool surfaceWorkflowEdit059(const Feature* feature){using namespace surface_workflow_059c;if(!feature)return false;if(feature->type==FeatureType::Loft){loft(feature);return true;}if(feature->type==FeatureType::SurfaceFill){fill(feature);return true;}if(feature->type!=FeatureType::SurfaceEdit)return false;const auto op=value(feature->nativeSpec,"op");if(op=="mirror")mirror(feature);else if(op=="trim_tool")trimTool(feature);else if(op=="ruled")ruled(feature);else return false;return true;}
