// Viewport editing retains the authored DTO and evaluates constraints before
// publishing. Datum/entity UI can enter via beginSketchEditing().
struct SketchEditHandle {int entity=-1,anchor=-1;Vec2 point{};std::string role;};
struct SketchEditing {
    bool active=false,dragging=false,moved=false;int x=0,y=0,snapEntity=-1,snapAnchor=-1;
    std::string name,error,snap;u64 revision=0,epoch=0;Mat4 frame;
    NamedSketch snapshot,preview;arkjson::Value candidate;
    SketchEditHandle handle;Vec2 press{},target{};
};
static SketchEditing g_sketchEditing;
static void cancelSketchEditing(){g_sketchEditing={};g_status="Sketch edit ended / saved geometry retained";}
static void beginSketchEditing(){
    if(g_busy)throw std::runtime_error("Wait for the document operation before editing");
    std::string name=g_activeSketch;for(auto it=g_picked.rbegin();it!=g_picked.rend();++it)if(it->kind=="curve"&&!it->owner.empty()){name=it->owner;break;}
    if(!g_doc.findSketch(name)){std::vector<EditorField> fields{{"Saved sketch",name}};if(!editFields("Edit sketch in viewport",sketchNames().c_str(),fields))return;name=trimField(fields[0].value);}
    auto* named=g_doc.findSketch(name);if(!named)throw std::runtime_error("Select a saved sketch to edit");
    cancelSketchDrawing();g_sketchEditing={};auto& edit=g_sketchEditing;edit.active=true;edit.name=name;edit.frame=resolvePlane(g_doc,named->sk.plane);edit.snapshot=*named;edit.preview=*named;edit.revision=g_session.revision;edit.epoch=g_documentEpoch;
    g_activeSketch=name;g_activePlane=named->sk.plane;g_showSketches=true;g_toolModule=2;g_navMode=0;g_pickKind=0;rebuildSketchOverlay();alignSketchCamera();g_status="Edit sketch / drag anchor or curve / Tab coordinates / Esc cancels drag, then exits";
}
static bool sketchEditCurrent(){return g_sketchEditing.revision==g_session.revision&&g_sketchEditing.epoch==g_documentEpoch;}
static void refreshSketchEditing(){
    auto& edit=g_sketchEditing;if(!edit.active||edit.dragging||g_error)return;
    auto* named=g_doc.findSketch(edit.name);if(!named){cancelSketchEditing();return;}
    edit.snapshot=*named;edit.preview=*named;edit.revision=g_session.revision;edit.epoch=g_documentEpoch;edit.error.clear();edit.frame=resolvePlane(g_doc,named->sk.plane);
}
static std::vector<SketchEditHandle> sketchEditHandles(const Sketch& sketch){
    std::vector<SketchEditHandle> handles;
    for(size_t i=0;i<sketch.ents.size();++i){const auto& entity=sketch.ents[i];
        if(entity.kind==SketchEnt::Project||entity.kind==SketchEnt::Offset||entity.kind==SketchEnt::Recipe)continue;
        if(entity.arcRecipe.t==arkjson::Value::Obj){for(const char* key:{"start","through","end"})if(auto* pair=entity.arcRecipe.find(key);pair&&pair->t==arkjson::Value::Arr&&pair->arr.size()==2){bool okX=false,okY=false;auto env=g_doc.buildEnv();double x=pair->arr[0].t==arkjson::Value::Str?evalExpr(pair->arr[0].str,env,&okX):pair->arr[0].num,y=pair->arr[1].t==arkjson::Value::Str?evalExpr(pair->arr[1].str,env,&okY):pair->arr[1].num;if(pair->arr[0].t==arkjson::Value::Num)okX=true;if(pair->arr[1].t==arkjson::Value::Num)okY=true;if(okX&&okY)handles.push_back({(int)i,-1,{x,y},key});}continue;}
        for(int vertex=0;vertex<sketchVertexCount(entity);++vertex){int at=sketchVertexDof(entity,vertex);if(at>=0&&at+1<(int)entity.dof.size())handles.push_back({(int)i,vertex,{entity.dof[(size_t)at],entity.dof[(size_t)at+1]},"anchor"});}
        if((entity.kind==SketchEnt::Circle||entity.kind==SketchEnt::Arc)&&entity.dof.size()>=3)handles.push_back({(int)i,-1,{entity.dof[0]+entity.dof[2],entity.dof[1]},"radius"});
        if(entity.kind==SketchEnt::Arc&&entity.dof.size()>=5)for(int angle=3;angle<5;++angle){double radians=deg2rad(entity.dof[(size_t)angle]);handles.push_back({(int)i,-1,{entity.dof[0]+entity.dof[2]*std::cos(radians),entity.dof[1]+entity.dof[2]*std::sin(radians)},angle==3?"a0":"a1"});}
    }return handles;
}
static bool sketchEditPlanePoint(int x,int y,Vec2& point){
    Vec3 origin,direction;viewRay(x,y,origin,direction);auto& edit=g_sketchEditing;Vec3 normal=edit.frame.transformDir({0,0,1}).normalized();double denominator=normal.dot(direction);if(std::abs(denominator)<1e-7)throw std::runtime_error("Sketch plane is edge-on; choose Normal to sketch");double t=normal.dot(edit.frame.transformPoint({})-origin)/denominator;if(t<0)return false;auto p=edit.frame.inverted().transformPoint(origin+direction*t);point={p.x,p.y};return std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<1e7&&std::abs(p.y)<1e7;
}
static arkjson::Value sketchEditOffset(const arkjson::Value& original,double delta){
    using V=arkjson::Value;if(std::abs(delta)<1e-12)return original;
    // Offsets retain named parameter and recipe bindings, including a radius.
    return original.t==V::Str?V::makeStr("("+original.str+")+("+drawingNumber(delta)+")"):V::makeStr(drawingNumber(original.num+delta));
}
static arkjson::Value sketchEditCandidate(Vec2 target){
    using V=arkjson::Value;auto& edit=g_sketchEditing;auto sketch=sketchEditValue(edit.snapshot),entities=*sketch.find("entities");auto& dto=entities.arr[(size_t)edit.handle.entity];const auto& entity=edit.snapshot.sk.ents[(size_t)edit.handle.entity];
    Vec2 delta=target-edit.handle.point;bool whole=edit.handle.role=="move";
    auto pair=[&](V& point,Vec2 amount){if(point.t!=V::Arr||point.arr.size()!=2)throw std::runtime_error("This recipe needs its coordinate editor");point.arr[0]=sketchEditOffset(point.arr[0],amount.x);point.arr[1]=sketchEditOffset(point.arr[1],amount.y);};
    if(entity.arcRecipe.t==V::Obj){auto recipe=*dto.find("recipe");if(whole){for(const char* key:{"start","through","end"})if(auto* value=recipe.find(key)){auto p=*value;pair(p,delta);recipe.set(key,p);}}else{auto p=*recipe.find(edit.handle.role.c_str());pair(p,delta);recipe.set(edit.handle.role,p);}dto.set("recipe",recipe);}
    else if(entity.kind==SketchEnt::Wire){
        // Saved wires use flattened DOFs plus typed segment descriptors. Do
        // not assume the creation-only start/to coordinate DTO survived save.
        const auto* authored=dto.find("dof");if(!authored||authored->t!=V::Arr||authored->arr.size()!=entity.dof.size())throw std::runtime_error("Saved wire has no valid editable coordinates");V dof=*authored;
        auto shift=[&](int at){if(at<0||at+1>=(int)dof.arr.size())throw std::runtime_error("Wire handle is outside its authored coordinates");dof.arr[(size_t)at]=sketchEditOffset(dof.arr[(size_t)at],delta.x);dof.arr[(size_t)at+1]=sketchEditOffset(dof.arr[(size_t)at+1],delta.y);};
        if(whole){shift(0);int at=2;for(const auto& segment:entity.segments){if(segment.kind==SketchEnt::WireSeg::Cubic){shift(at);shift(at+2);shift(at+4);at+=6;}else{shift(at);at+=segment.kind==SketchEnt::WireSeg::Arc?3:2;}}}
        else{int vertex=edit.handle.anchor;shift(sketchVertexDof(entity,vertex));int last=sketchVertexCount(entity)-1,at=sketchVertexDof(entity,last);bool closed=at>=0&&at+1<(int)entity.dof.size()&&entity.dof[0]==entity.dof[(size_t)at]&&entity.dof[1]==entity.dof[(size_t)at+1];if(closed&&(vertex==0||vertex==last))shift(vertex==0?at:0);}
        dto.set("dof",dof);
    }else{auto* authored=dto.find("dof");if(!authored)throw std::runtime_error("This driven entity must be edited through its source recipe");auto dof=*authored;if(edit.handle.role=="radius"){double radius=drawingDistance({entity.dof[0],entity.dof[1]},target);if(radius<1e-8)throw std::runtime_error("Radius must remain positive");dof.arr[2]=sketchEditOffset(dof.arr[2],radius-entity.dof[2]);}
        else if(edit.handle.role=="a0"||edit.handle.role=="a1"){int at=edit.handle.role=="a0"?3:4;double angle=rad2deg(std::atan2(target.y-entity.dof[1],target.x-entity.dof[0]));while(angle-entity.dof[(size_t)at]>180)angle-=360;while(angle-entity.dof[(size_t)at]<-180)angle+=360;dof.arr[(size_t)at]=sketchEditOffset(dof.arr[(size_t)at],angle-entity.dof[(size_t)at]);}
        else if(whole){int coordinates=(entity.kind==SketchEnt::Circle||entity.kind==SketchEnt::Arc)?2:(int)dof.arr.size();for(int at=0;at+1<coordinates;at+=2){dof.arr[(size_t)at]=sketchEditOffset(dof.arr[(size_t)at],delta.x);dof.arr[(size_t)at+1]=sketchEditOffset(dof.arr[(size_t)at+1],delta.y);}}
        else{int at=sketchVertexDof(entity,edit.handle.anchor);dof.arr[(size_t)at]=sketchEditOffset(dof.arr[(size_t)at],delta.x);dof.arr[(size_t)at+1]=sketchEditOffset(dof.arr[(size_t)at+1],delta.y);}dto.set("dof",dof);}
    sketch.set("entities",entities);
    if(edit.handle.role=="anchor"&&edit.snapEntity>=0&&edit.snapAnchor>=0){auto constraints=*sketch.find("constraints");bool exists=false;for(const auto& c:edit.snapshot.sk.cons)if(c.type==SketchCon::Coincident&&((c.a==edit.handle.entity&&c.pa==edit.handle.anchor&&c.b==edit.snapEntity&&c.pb==edit.snapAnchor)||(c.b==edit.handle.entity&&c.pb==edit.handle.anchor&&c.a==edit.snapEntity&&c.pa==edit.snapAnchor)))exists=true;
        if(!exists){V relation=V::makeObj();relation.set("type",V::makeStr("coincident"));relation.set("a",V::makeNum(edit.handle.entity));relation.set("pa",V::makeNum(edit.handle.anchor));relation.set("b",V::makeNum(edit.snapEntity));relation.set("pb",V::makeNum(edit.snapAnchor));relation.set("id",V::makeStr("edit_snap_"+std::to_string(edit.handle.entity)+"_"+std::to_string(edit.handle.anchor)+"_"+std::to_string(constraints.arr.size())));constraints.arr.push_back(relation);sketch.set("constraints",constraints);}}
    return sketch;
}
static bool beginSketchEditDrag(int x,int y){
    auto& edit=g_sketchEditing;if(!edit.active||!drawingInView(x,y))return false;
    if(!sketchEditCurrent())throw std::runtime_error("Sketch edit is stale; exit and select the current sketch again");
    double best=12;SketchEditHandle chosen;
    for(auto handle:sketchEditHandles(edit.snapshot.sk)){float sx,sy;if(projectScreen(edit.frame.transformPoint({handle.point.x,handle.point.y,0}),sx,sy)){double distance=std::hypot(sx-x,sy-y);if(distance<best){best=distance;chosen=handle;}}}
    if(chosen.entity<0){best=9;for(size_t i=0;i<edit.snapshot.sk.ents.size();++i){const auto& entity=edit.snapshot.sk.ents[i];if(entity.kind==SketchEnt::Project||entity.kind==SketchEnt::Offset)continue;auto paths=sampleSketchEntityPaths(entity,edit.snapshot.sk.tolerance,&edit.snapshot.sk);for(const auto& samples:paths)for(size_t k=1;k<samples.size();++k){float ax,ay,bx,by;if(!projectScreen(edit.frame.transformPoint({samples[k-1].x,samples[k-1].y,0}),ax,ay)||!projectScreen(edit.frame.transformPoint({samples[k].x,samples[k].y,0}),bx,by))continue;double dx=bx-ax,dy=by-ay,length=dx*dx+dy*dy,t=length>1e-9?clamp(((x-ax)*dx+(y-ay)*dy)/length,0.,1.):0,distance=std::hypot(ax+dx*t-x,ay+dy*t-y);if(distance<best){best=distance;chosen={(int)i,-1,{},entity.kind==SketchEnt::Recipe?"recipe":"move"};}}}}
    if(chosen.entity<0){g_status="Click a sketch anchor or curve to edit / Exit sketch in ribbon";return true;}
    if(!sketchEditPlanePoint(x,y,edit.press))return true;if(chosen.role=="move")chosen.point=edit.press;edit.handle=chosen;edit.target=chosen.point;edit.x=x;edit.y=y;edit.dragging=true;edit.moved=false;edit.error.clear();
    const auto& entity=edit.snapshot.sk.ents[(size_t)chosen.entity];g_picked.clear();SelectionItem selected;selected.kind="curve";selected.owner=edit.name;selected.name=entity.name;g_picked.push_back(selected);refreshSelectionLines();g_status="Selected "+entityChoiceLabel(edit.snapshot.sk,chosen.entity)+" / drag "+chosen.role;if(chosen.role=="recipe"){edit.dragging=false;g_status="Parametric recipe selected / Coordinates in ribbon or Tab edits its source";}return true;
}
static void moveSketchEditDrag(int x,int y){
    auto& edit=g_sketchEditing;if(!edit.dragging)return;edit.error.clear();
    try{if(!sketchEditCurrent())throw std::runtime_error("Drawing changed during sketch drag; preview cannot be applied");if(std::abs(x-edit.x)<3&&std::abs(y-edit.y)<3&&!edit.moved)return;edit.moved=true;Vec2 raw;if(!sketchEditPlanePoint(x,y,raw))return;Vec2 target=edit.handle.point+(raw-edit.press);double best=11;edit.snap.clear();edit.snapEntity=edit.snapAnchor=-1;
        auto offer=[&](Vec2 point,const char* label,int entity=-1,int anchor=-1){float sx,sy;if(!projectScreen(edit.frame.transformPoint({point.x,point.y,0}),sx,sy))return;double distance=std::hypot(sx-x,sy-y);if(distance<best){best=distance;target=point;edit.snap=label;edit.snapEntity=entity;edit.snapAnchor=anchor;}};
        for(const auto& handle:sketchEditHandles(edit.snapshot.sk))if(handle.entity!=edit.handle.entity)offer(handle.point,handle.role=="anchor"?"Anchor snap":"Curve handle placement",handle.role=="anchor"?handle.entity:-1,handle.anchor);offer({0,0},"Origin snap");if(edit.snap.empty()&&g_grid){double spacing=sketchDrawingGridSpacing();offer({std::round(target.x/spacing)*spacing,std::round(target.y/spacing)*spacing},"Grid placement");}
        edit.target=target;edit.candidate=sketchEditCandidate(target);NamedSketch preview;preview.name=edit.name;std::string error;if(!sketchFromJson(edit.candidate.dump(),preview.sk,&error))throw std::runtime_error(error);evaluateSketch(preview,g_doc.buildEnv());if(!preview.error.empty())throw std::runtime_error(preview.error);edit.preview=std::move(preview);g_status="Sketch edit preview / "+edit.handle.role+(edit.snap.empty()?"":" / "+edit.snap)+" / release applies / Esc cancels";
    }catch(const std::exception& error){edit.error=error.what();edit.preview=edit.snapshot;g_status="Sketch edit rejected / "+edit.error;}
}
static void cancelSketchEditDrag(){auto& edit=g_sketchEditing;edit.dragging=false;edit.moved=false;edit.preview=edit.snapshot;edit.candidate={};edit.error.clear();g_dragged=true;g_status="Sketch drag cancelled / saved geometry retained";}
static void finishSketchEditDrag(){
    using V=arkjson::Value;auto& edit=g_sketchEditing;if(!edit.dragging)return;edit.dragging=false;
    // Recheck at release: the document can change after the last mouse move.
    // Keep the captured baseline; never refresh or rebase this candidate.
    if(edit.epoch!=g_documentEpoch)edit.error="Document changed during sketch edit; exit and select again";
    else if(edit.revision!=g_session.revision)edit.error="Drawing changed during sketch drag; preview cannot be applied";
    if(!edit.error.empty()){g_error=true;g_status="Sketch edit rejected / "+edit.error;edit.preview=edit.snapshot;edit.candidate={};edit.moved=false;return;}if(!edit.moved)return;
    V command=V::makeObj(),arguments=V::makeObj(),commands=V::makeArr();command.set("op",V::makeStr("sketch.set"));command.set("name",V::makeStr(edit.name));command.set("sketch",edit.candidate);commands.arr.push_back(command);arguments.set("commands",commands);arguments.set("expected_revision",V::makeNum((double)edit.revision));
    V actor=V::makeObj();actor.set("kind",V::makeStr("human"));actor.set("id",V::makeStr("studio"));arguments.set("actor",actor);
    runTool("doc_apply",arguments);if(g_error){edit.preview=edit.snapshot;edit.candidate={};edit.moved=false;return;}
    edit.snapshot=*g_doc.findSketch(edit.name);edit.preview=edit.snapshot;edit.revision=g_session.revision;edit.epoch=g_documentEpoch;edit.moved=false;refreshSelectionLines();g_status="Sketch edit saved atomically at r"+std::to_string(edit.revision)+" / drag another handle or Exit sketch";
}
static void drawSketchEditPreview(){
    auto& edit=g_sketchEditing;if(!edit.active||!sketchEditCurrent())return;static GLuint vao=0,vbo=0;if(!vao){glGenVertexArrays(1,&vao);glGenBuffers(1,&vbo);}std::vector<float> lines,points;
    auto put=[&](std::vector<float>& into,Vec2 point){auto p=edit.frame.transformPoint({point.x,point.y,0});into.insert(into.end(),{(float)p.x,(float)p.y,(float)p.z});};
    if(edit.dragging&&edit.moved&&edit.error.empty())for(const auto& entity:edit.preview.sk.ents){auto paths=sampleSketchEntityPaths(entity,edit.preview.sk.tolerance,&edit.preview.sk);for(const auto& samples:paths)for(size_t i=1;i<samples.size();++i){put(lines,samples[i-1]);put(lines,samples[i]);}}
    for(const auto& handle:sketchEditHandles(edit.preview.sk))put(points,handle.point);
    glViewport(viewX,winH-viewY-viewH,viewW,viewH);glDisable(GL_DEPTH_TEST);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(pSketch);auto matrix=cameraProjection()*cameraView();float vp[16];for(int i=0;i<16;++i)vp[i]=(float)matrix.m[i];glUniformMatrix4fv(glGetUniformLocation(pSketch,"vp"),1,GL_FALSE,vp);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12,nullptr);
    auto draw=[&](const std::vector<float>& vertices,GLenum mode,float alpha,float size){if(vertices.empty())return;glUniform4f(glGetUniformLocation(pSketch,"tint"),.18f,.95f,.88f,alpha);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(vertices.size()*sizeof(float)),vertices.data(),GL_DYNAMIC_DRAW);glPointSize(size);glDrawArrays(mode,0,(GLsizei)vertices.size()/3);};draw(lines,GL_LINES,.98f,1);draw(points,GL_POINTS,.18f,17);draw(points,GL_POINTS,1,7);glPointSize(1);glViewport(0,0,winW,winH);
}
