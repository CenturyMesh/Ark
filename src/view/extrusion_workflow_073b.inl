// Human extrusion controls. Exact native discovery/preview owns geometry;
// this editor retains the user's source selection and driving expressions.
namespace extrusion_view_073b {
using V=arkjson::Value;
enum Field {SketchName,Depth,StartOffset,Direction,Symmetric,Shape,Result,Targets,Thickness,WallSide,ResultName,Placement};
static const V& get(const V& value,const char* key){static const V none;const auto* field=value.find(key);return field?*field:none;}
static void erase(V& value,const char* key){value.obj.erase(std::remove_if(value.obj.begin(),value.obj.end(),[&](const auto& field){return field.first==key;}),value.obj.end());}
static std::string parameter(const Feature* feature,const char* key,const char* fallback){if(!feature)return fallback;const auto declared=feature->p.find(key);if(declared!=feature->p.end())return declared->second;if(feature->type!=FeatureType::NativeSolid)return fallback;const auto* native=feature->nativeSpec.find(key);return native?(native->t==V::Str?native->str:native->dump()):fallback;}
static std::string text(HWND window){const int size=GetWindowTextLengthA(window);std::string value(size+1,'\0');GetWindowTextA(window,value.data(),size+1);value.resize(size);return value;}
static std::string bodyLabel(const Feature& feature){return "#"+std::to_string(feature.id)+" / "+feature.name;}
static std::vector<u64> solidChoices(const Feature* editing){
    std::vector<u64> ids;for(const auto& feature:g_doc.features){if(editing&&feature.id==editing->id)continue;for(const auto& body:g_doc.evalBodies)if(body.id==feature.id&&isNativeFeature(feature.type)){const auto* report=body.nativeData.find("report");if(report&&get(*report,"brep_valid").b&&get(*report,"solids").num==1)ids.push_back(feature.id);break;}}return ids;
}
static void updateControls(const std::vector<HWND>& controls){
    if(controls.size()<=ResultName)return;const auto shape=text(controls[Shape]);const bool surface=shape=="Surface",thin=shape=="Thin wall";
    auto enabled=[&](size_t index,bool allow){if(!allow&&GetFocus()==controls[index])SetFocus(controls[Shape]);EnableWindow(controls[index],allow?TRUE:FALSE);};
    if(surface&&text(controls[Result])!="New body")SendMessage(controls[Result],CB_SETCURSEL,0,0);
    enabled(Result,!surface);enabled(Targets,!surface&&text(controls[Result])=="Join targets");enabled(Thickness,thin);enabled(WallSide,thin);
}
struct Context {
    u64 revision=g_session.revision,epoch=g_documentEpoch;std::optional<Feature> source;
    explicit Context(const Feature* editing){if(editing)source=*editing;}
    void current()const{if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Drawing changed while preparing the extrusion. Reopen its current sketch and targets.");}
};
static std::string uniqueName(const std::string& requested,std::set<std::string>& used){
    auto value=trimField(requested);if(value.empty())throw std::runtime_error("Name the extrusion result");const auto base=value;for(unsigned suffix=2;used.count(value);++suffix)value=base+" ("+std::to_string(suffix)+")";used.insert(value);return value;
}
static HBITMAP resultBitmap(const Image& image){
    if(image.w<=0||image.h<=0||image.px.size()!=(size_t)image.w*image.h*3)return nullptr;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=image.w;info.bmiHeader.biHeight=-image.h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap)return nullptr;auto* output=static_cast<unsigned char*>(pixels);for(size_t i=0;i<(size_t)image.w*image.h;++i){output[4*i]=image.px[3*i+2];output[4*i+1]=image.px[3*i+1];output[4*i+2]=image.px[3*i];output[4*i+3]=255;}return bitmap;
}
static bool review(const Context& context,const V& preview,const std::vector<std::string>& resultNames,const std::string& description){
    context.current();if(!get(preview,"valid").b||!get(preview,"candidate_cached").b){const auto& cause=get(get(preview,"candidate_result"),"error");throw std::runtime_error(cause.t==V::Str?cause.str:"The exact extrusion candidate did not validate");}
    const auto candidateId=get(preview,"candidate_id").asStr();auto candidate=g_session.takePreparedCandidate(candidateId);if(!candidate)throw std::runtime_error("Extrusion preview expired; prepare it again");
    std::vector<u64> results;for(const auto& name:resultNames){u64 id=0;if(context.source)if(const auto* feature=candidate->document().find(context.source->id);feature&&feature->name==name)id=feature->id;
        if(!id)for(const auto& feature:candidate->document().features)if(feature.name==name){if(id)throw std::runtime_error("Extrusion preview result name is ambiguous");id=feature.id;}if(!id)throw std::runtime_error("Extrusion preview result is missing");results.push_back(id);}
    Image image;std::ostringstream summary;summary<<description<<"\r\n\r\n"<<results.size()<<" exact result"<<(results.size()==1?"":"s")<<".\r\n";
    auto render=[&](){Mesh display;for(auto id:results){const auto& doc=candidate->document();const auto body=std::find_if(doc.evalBodies.begin(),doc.evalBodies.end(),[&](const Body& value){return value.id==id;});if(body==doc.evalBodies.end())throw std::runtime_error("Prepared extrusion has no evaluated result");display.append(body->tessellate(doc.tessOpt));const auto* report=body->nativeData.find("report");const auto* feature=doc.find(id);summary<<(feature?feature->name:std::to_string(id));if(report&&get(*report,"volume_mm3").t==V::Num)summary<<" / "<<get(*report,"volume_mm3").num<<" mm3";else if(report&&get(*report,"area_mm2").t==V::Num)summary<<" / "<<get(*report,"area_mm2").num<<" mm2";summary<<"\r\n";}
        if(!display.triCount())throw std::runtime_error("The extrusion preview has no display faces");const auto bounds=display.computeBBox();SnapCam camera;camera.useTarget=true;camera.target=(bounds.mn+bounds.mx)*.5;camera.dist=std::max(10.,(bounds.mx-bounds.mn).len()*2.2);image=renderSnapshot(display,784,300,camera);return V::makeObj();};
    try{callSessionWithProgress("extrusion_preview",V::makeObj(),render);}catch(...){g_session.adoptPreparedCandidate(std::move(*candidate),context.revision);throw;}
    g_session.adoptPreparedCandidate(std::move(*candidate),context.revision);context.current();HBITMAP bitmap=resultBitmap(image);if(!bitmap)throw std::runtime_error("Could not display the checked extrusion");
    const auto previousShot=g_dialogShot;if(!previousShot.empty())g_dialogShot=previousShot+".candidate.png";std::vector<EditorField> fields{{"Checked extrusion",summary.str()}};
    const bool accepted=editFields("Extrude / exact preview","Review all resulting bodies. Apply saves this checked result; Cancel keeps the current drawing.",fields,true,"Apply",bitmap,true);DeleteObject(bitmap);g_dialogShot=previousShot;if(!accepted)return false;context.current();
    V apply=V::makeObj();apply.set("candidate_id",V::makeStr(candidateId));apply.set("expected_revision",V::makeNum((double)context.revision));runTool("doc_apply",apply);if(g_error)throw std::runtime_error(g_status);
    g_activeSketch.clear();g_showSketches=false;g_picked.clear();selBody=-1;for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==results.back()){selBody=(int)i;break;}refreshSelectionLines();frameScene();g_status="Extrusion saved / source sketch and driving expressions retained";return true;
}
static V regionDiscovery(const Context& context,const std::string& sketch){
    context.current();V request=V::makeObj();request.set("name",V::makeStr(sketch));request.set("native_regions",V::makeBool(true));const auto response=runTool("sketch_inspect",request);if(g_error)throw std::runtime_error(g_status);context.current();const auto& data=get(get(response,"structuredContent"),"native_regions");
    if(get(data,"schema").asStr()!="ark.extrusion-regions.073b")throw std::runtime_error("Exact sketch-region discovery is unavailable");
    if(get(data,"status").asStr()=="invalid"){const auto error=get(data,"error").asStr();throw std::runtime_error(error.empty()?"Sketch contours cannot form the requested extrusion":error);}return data;
}
static std::vector<V> chooseRegions(const Context& context,const V& discovery){
    const auto& rows=get(discovery,"regions");if(rows.t!=V::Arr)throw std::runtime_error("Exact region list is missing");
    if(!get(discovery,"profile_closed").b&&rows.arr.empty())return {V{}};
    if(rows.arr.empty())throw std::runtime_error("The sketch contains no selectable closed region");
    if(rows.arr.size()==1){if(!get(rows.arr[0],"selectable").b)throw std::runtime_error("The sketch region has no stable selector");return {get(rows.arr[0],"seed")};}
    std::vector<EditorField> fields;const auto* prior=context.source?context.source->nativeSpec.find("regions"):nullptr;
    for(size_t i=0;i<rows.arr.size();++i){const auto& row=rows.arr[i];std::string label=get(row,"label").asStr();if(label.empty())label="Region";if(label.size()>22)label=label.substr(0,19)+"...";label=std::to_string(i+1)+" / "+label;const auto holes=get(row,"hole_count").asNum();if(holes)label+=" / "+std::to_string((int)holes)+" hole"+(holes==1?"":"s");bool selected=!prior;
        if(prior)for(const auto& seed:prior->arr)if(seed.dump()==get(row,"seed").dump())selected=true;
        fields.push_back({label,selected&&get(row,"selectable").b?"1":"0",true});
    }
    const auto previousShot=g_dialogShot;if(!previousShot.empty())g_dialogShot=previousShot+".regions.png";
    const bool accepted=editFields("Extrude / sketch regions","Choose the closed regions to extrude. Each outer loop includes its holes; unchecked regions remain in the sketch.",fields,false,"Preview");g_dialogShot=previousShot;if(!accepted)return {};
    context.current();std::vector<V> selected;for(size_t i=0;i<fields.size();++i)if(fields[i].value=="1"){if(!get(rows.arr[i],"selectable").b)throw std::runtime_error("Selected region has no stable native selector");selected.push_back(get(rows.arr[i],"seed"));}
    if(selected.empty())throw std::runtime_error("Select at least one sketch region");if(context.source&&selected.size()!=1)throw std::runtime_error("Editing an extrusion selects one region. Use Extrude to create several new bodies.");return selected;
}
static std::vector<u64> chooseTargets(const Context& context,const std::string& choice,const std::vector<u64>& ids){
    context.current();if(!g_doc.assembly.empty())throw std::runtime_error("Join targets operates on part definitions before assembly. Create a separate body here, or edit the target part definition.");
    for(auto id:ids)if(const auto* feature=g_doc.find(id);feature&&bodyLabel(*feature)==choice)return {id};
    if(choice!="Choose multiple bodies...")throw std::runtime_error("Choose at least one exact target body");if(ids.empty())throw std::runtime_error("No exact target solid is available");
    std::vector<EditorField> fields;for(auto id:ids){const auto* feature=g_doc.find(id);fields.push_back({feature?bodyLabel(*feature):std::to_string(id),"0",true});}
    if(!editFields("Extrude / join targets","Choose the solid definitions that receive material. The resulting exact union must be one connected solid.",fields,false,"Preview"))return {};
    context.current();std::vector<u64> targets;for(size_t i=0;i<fields.size();++i)if(fields[i].value=="1")targets.push_back(ids[i]);if(targets.empty())throw std::runtime_error("Select at least one exact target solid");return targets;
}
static void show(bool surface,const Feature* editing){
    Context context(editing);const Feature* source=context.source?&*context.source:nullptr;const auto targetIds=solidChoices(source);std::vector<std::string> targets{"Choose multiple bodies..."};std::string target=targets.front();
    for(auto id:targetIds){const auto label=bodyLabel(*g_doc.find(id));targets.push_back(label);if(selBody>=0&&(size_t)selBody<g_bodyIds.size()&&g_bodyIds[(size_t)selBody]==id)target=label;}
    const auto sourceSolid=parameter(source,"solid","0");
    std::string shape=source?(source->type==FeatureType::ExtrudeSurface?(formNumber(sourceSolid)==0?"Surface":"Solid"):(source->p.count("thickness")||source->nativeSpec.find("thickness"))?"Thin wall":"Solid"):surface?"Surface":"Solid";
    const auto sourceDirection=parameter(source,"direction","1"),sourceSymmetric=parameter(source,"symmetric","0");
    std::string distance=parameter(source,source&&source->type==FeatureType::ExtrudeSurface?"distance":"depth","10"),direction=formNumber(sourceDirection)==-1?"Reverse":"Forward";
    if(source&&source->type==FeatureType::ExtrudeSurface&&formNumber(distance)<0){distance="-("+distance+")";direction="Reverse";}
    const auto side=source?get(source->nativeSpec,"thin_side").asStr():std::string();const auto sideLabel=side=="symmetric"?"Centered":side=="outside"||side=="right"?"Outside / right":"Inside / left";
    std::vector<std::string> shapes=source?(source->type==FeatureType::ExtrudeSurface?std::vector<std::string>{"Surface","Solid"}:std::vector<std::string>{"Solid","Thin wall"}):std::vector<std::string>{"Solid","Surface","Thin wall"};
    const auto initialSymmetric=formNumber(sourceSymmetric)!=0?"1":"0";
    std::vector<EditorField> fields{pickField("Sketch",source?source->get("sketch"):defaultSketchName(),8),{"Depth / mm",distance},{"Start offset / mm",parameter(source,"start_offset","0")},choiceField("Direction",direction,{"Forward","Reverse"}),{"Symmetric",initialSymmetric,true},choiceField("Shape",shape,shapes),choiceField("Result","New body",{"New body","Join targets"}),choiceField("Target bodies",target,targets),{"Wall thickness / mm",parameter(source,"thickness","2")},choiceField("Wall side",sideLabel,{"Inside / left","Outside / right","Centered"}),{"Result name",source?source->name:"Extrusion"}};
    if(!source&&!g_doc.assembly.empty())fields.push_back(choiceField("Placement","New occurrence",{"New occurrence","Definition only"}));
    if(!editFields(source?"Edit extrusion":"Extrude sketch","Pick a sketch, set the depth and start plane. Choose Solid, Surface or Thin wall; Preview checks the complete result.",fields,false,"Preview",nullptr,false,updateControls))return;
    context.current();const auto sketch=trimField(fields[SketchName].value);if(!g_doc.findSketch(sketch))throw std::runtime_error("Choose an existing sketch");if(formNumber(fields[Depth].value)<=0)throw std::runtime_error("Extrusion depth must be positive; choose Reverse to change direction");(void)formNumber(fields[StartOffset].value);
    const bool thin=fields[Shape].value=="Thin wall",isSurface=fields[Shape].value=="Surface",join=fields[Result].value=="Join targets"&&!isSurface;
    if(thin&&formNumber(fields[Thickness].value)<=0)throw std::runtime_error("Thin-wall thickness must be positive");
    if(fields[Direction].value!="Forward"&&fields[Direction].value!="Reverse")throw std::runtime_error("Choose a valid extrusion direction");
    const auto discovery=regionDiscovery(context,sketch);const bool closed=get(discovery,"profile_closed").b||!get(discovery,"regions").arr.empty();if(!closed&&!thin&&!isSurface)throw std::runtime_error("A solid extrusion needs a closed region. Choose Surface or Thin wall for an open contour.");
    auto selected=chooseRegions(context,discovery);if(selected.empty())return;std::vector<u64> targetBodies;if(join){targetBodies=chooseTargets(context,fields[Targets].value,targetIds);if(targetBodies.empty())return;if(targetBodies.size()+selected.size()>16)throw std::runtime_error("An exact union accepts up to 16 selected target bodies and extruded regions together");}
    context.current();std::set<std::string> used;for(const auto& feature:g_doc.features)if(!source||feature.id!=source->id)used.insert(feature.name);if(!source&&fields.size()>Placement&&fields[Placement].value=="New occurrence")for(const auto& occurrence:g_doc.assembly)used.insert(occurrence.name);V commands=V::makeArr();std::vector<std::string> extrusions,resultNames;
    for(size_t i=0;i<selected.size();++i){const auto name=source?trimField(fields[ResultName].value):uniqueName(fields[ResultName].value+(selected.size()>1?" / region "+std::to_string(i+1):""),used);if(name.empty())throw std::runtime_error("Name the extrusion result");if(source&&name!=source->name&&used.count(name))throw std::runtime_error("Another feature already uses the requested name");used.insert(name);const auto type=source?source->type:isSurface?FeatureType::ExtrudeSurface:FeatureType::NativeSolid;V command=V::makeObj(),parameters=V::makeObj(),native=source?source->nativeSpec:V::makeObj();
        command.set("op",V::makeStr(source?"feature.edit":"feature.add"));if(source)command.set("id",V::makeNum(source->id));else command.set("type",V::makeStr(featureTypeName(type)));command.set("name",V::makeStr(name));
        if(source)for(const auto& p:source->p)parameters.set(p.first,V::makeStr(p.second));parameters.set("sketch",V::makeStr(sketch));
        const auto offset=trimField(fields[StartOffset].value);if(offset!="0"||(source&&(source->p.count("start_offset")||(type==FeatureType::NativeSolid&&source->nativeSpec.find("start_offset")))))parameters.set("start_offset",V::makeStr(offset));else erase(parameters,"start_offset");
        const std::string symmetry=source&&fields[Symmetric].value==initialSymmetric?sourceSymmetric:fields[Symmetric].value;
        if(type!=FeatureType::ExtrudeSurface||symmetry!="0"||(source&&source->p.count("symmetric")))parameters.set("symmetric",V::makeStr(symmetry));else erase(parameters,"symmetric");
        if(type==FeatureType::ExtrudeSurface){parameters.set("distance",V::makeStr(source&&fields[Depth].value==distance&&fields[Direction].value==direction?parameter(source,"distance","10"):fields[Direction].value=="Reverse"?"-("+trimField(fields[Depth].value)+")":trimField(fields[Depth].value)));parameters.set("solid",V::makeStr(source&&fields[Shape].value==shape?sourceSolid:isSurface?"0":"1"));}
        else{parameters.set("op",V::makeStr("extrude"));parameters.set("depth",V::makeStr(trimField(fields[Depth].value)));parameters.set("direction",V::makeStr(source&&fields[Direction].value==direction?sourceDirection:fields[Direction].value=="Reverse"?"-1":"1"));}
        if(thin){parameters.set("thickness",V::makeStr(trimField(fields[Thickness].value)));native.set("thin_side",V::makeStr(fields[WallSide].value=="Centered"?"symmetric":fields[WallSide].value=="Inside / left"?(closed?"inside":"left"):(closed?"outside":"right")));}else{erase(parameters,"thickness");erase(native,"thin_side");}
        // CLI recipes may declare the same numeric/expression fields in native.
        // Normalize edited controls into p and remove the old duplicate source.
        if(type==FeatureType::NativeSolid)for(const char* key:{"start_offset","symmetric","thickness","depth","direction"})erase(native,key);
        if(selected[i].isNull())erase(native,"regions");else{V regions=V::makeArr();regions.arr.push_back(selected[i]);native.set("regions",regions);}
        if(type==FeatureType::ExtrudeSurface&&isSurface&&!selected[i].isNull())native.set("region_walls",V::makeBool(true));else erase(native,"region_walls");
        if(source){V removed=V::makeArr();for(const auto& p:source->p)if(!parameters.find(p.first.c_str()))removed.arr.push_back(V::makeStr(p.first));if(!removed.arr.empty())command.set("p_remove",removed);}
        command.set("p",parameters);command.set("native",native);commands.arr.push_back(command);extrusions.push_back(name);
    }
    if(join){const auto name=uniqueName(fields[ResultName].value+" / joined",used);V command=V::makeObj(),parameters=V::makeObj(),inputs=V::makeArr();command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr("native_solid"));command.set("name",V::makeStr(name));parameters.set("op",V::makeStr("union"));command.set("p",parameters);for(auto id:targetBodies)inputs.arr.push_back(V::makeNum(id));for(const auto& extrusion:extrusions)inputs.arr.push_back(source?V::makeNum(source->id):V::makeStr(extrusion));command.set("inputs",inputs);commands.arr.push_back(command);resultNames.push_back(name);}else resultNames=extrusions;
    if(!source&&fields.size()>Placement&&fields[Placement].value=="New occurrence")for(const auto& name:resultNames){V occurrence=V::makeObj();occurrence.set("op",V::makeStr("assembly.add"));occurrence.set("name",V::makeStr(name));occurrence.set("feature",V::makeStr(name));occurrence.set("p",V::makeObj());commands.arr.push_back(occurrence);}
    V args=V::makeObj();args.set("commands",commands);args.set("expected_revision",V::makeNum(context.revision));args.set("detail",V::makeStr("changes"));const auto response=runTool("doc_preview",args);if(g_error)throw std::runtime_error(g_status);
    std::ostringstream description;description<<fields[Shape].value<<" / "<<fields[Result].value<<"\r\nSketch: "<<sketch<<"\r\nDepth: "<<fields[Depth].value<<" mm / start offset: "<<fields[StartOffset].value<<" mm\r\n"<<fields[Direction].value<<(fields[Symmetric].value=="1"?" / symmetric":"");if(thin)description<<"\r\nWall: "<<fields[Thickness].value<<" mm / "<<fields[WallSide].value;
    review(context,get(response,"structuredContent"),resultNames,description.str());
}
}
static void extrusionWorkflow073(bool surface,const Feature* editing){extrusion_view_073b::show(surface,editing);}
