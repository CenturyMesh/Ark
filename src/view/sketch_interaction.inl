// Explicit viewport sketch authoring. This file is included after viewRay and
// projectScreen: screen distance chooses a placement, never production geometry.
struct SketchPlacement {
    Vec2 point{}; std::string label; int entity=-1,vertex=-1;
    bool alignedX=false,alignedY=false;
};
struct SketchDrawing {
    bool active=false,hover=false,stale=false,grid=true;
    int tool=0; // line, polyline, rectangle, circle, three-point arc
    std::string name,plane="XY",dimension,secondDimension;
    u64 revision=0,epoch=0; Mat4 frame;
    std::vector<SketchPlacement> anchors; SketchPlacement pointer;
    std::vector<float> lines,fill,guides,points;
    arkjson::Value entity; std::string previewError;
};
static SketchDrawing g_sketchDrawing;
static double sketchDrawingGridSpacing();
static const char* sketchDrawingToolName(int tool){static const char* names[]={"Line","Polyline","Rectangle","Circle","Arc 3 pt"};return names[std::clamp(tool,0,4)];}
static std::string drawingNumber(double value){std::ostringstream text;text<<std::setprecision(15)<<value;return text.str();}
static double drawingDistance(Vec2 a,Vec2 b){return std::hypot(a.x-b.x,a.y-b.y);}
static bool drawingInView(int x,int y){return x>=viewX&&x<viewX+viewW&&y>=viewY&&y<viewY+viewH;}
static bool drawingCurrent(){return g_sketchDrawing.revision==g_session.revision&&g_sketchDrawing.epoch==g_documentEpoch;}
static void cancelSketchDrawing(){g_sketchDrawing={};g_status="Sketch preview cancelled / saved geometry retained";}
static void beginSketchDrawing(int tool,bool choose){
    if(g_busy)throw std::runtime_error("Wait for the current document operation before drawing a sketch");
    std::string name=g_activeSketch.empty()?"Interactive sketch":g_activeSketch,plane=g_activePlane;
    const u64 revision=g_session.revision,epoch=g_documentEpoch;
    if(choose){std::vector<EditorField> fields{{"Sketch name",name},{"Plane",plane},{"Tool: line / polyline / rectangle / circle / arc","line"}};
        if(!editFields("Draw sketch in the view","Click anchors; cyan geometry is an unsaved preview. Tab or click a live dimension for expressions. Esc cancels; Enter ends a polyline. Existing numeric forms remain in the ribbon.",fields))return;
        name=trimField(fields[0].value);plane=trimField(fields[1].value);auto kind=trimField(fields[2].value);
        if(kind=="line")tool=0;else if(kind=="polyline")tool=1;else if(kind=="rectangle")tool=2;else if(kind=="circle")tool=3;else if(kind=="arc")tool=4;else throw std::runtime_error("Choose line, polyline, rectangle, circle or arc");}
    if(name.empty())throw std::runtime_error("Sketch needs a name");
    if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Drawing changed while choosing sketch tools; choose the current sketch again");
    if(auto* existing=g_doc.findSketch(name)){if(existing->sk.plane!=plane)throw std::runtime_error("Preserve the existing sketch plane or choose a new name");}
    g_sketchDrawing={};auto& drawing=g_sketchDrawing;drawing.active=true;drawing.tool=tool;drawing.name=name;drawing.plane=plane;drawing.frame=resolvePlane(g_doc,plane);drawing.revision=revision;drawing.epoch=epoch;
    g_activeSketch=g_doc.findSketch(name)?name:"";g_activePlane=plane;g_showSketches=true;g_toolModule=2;g_navMode=0;g_pickKind=0;g_picked.clear();refreshSelectionLines();rebuildSketchOverlay();alignSketchCamera();
    g_status=std::string(sketchDrawingToolName(tool))+" / click first anchor / Tab dimensions / Esc cancel / MMB orbit";
}
static SketchPlacement drawingSnap(Vec2 raw,int mouseX,int mouseY){
    auto& drawing=g_sketchDrawing;SketchPlacement result;result.point=raw;
    // The acquisition radius is constant in screen pixels at every camera scale.
    double best=11.;bool acquired=false;
    auto offer=[&](Vec2 point,const char* label,int entity=-1,int vertex=-1){float sx,sy;if(!projectScreen(drawing.frame.transformPoint({point.x,point.y,0}),sx,sy))return;
        double distance=std::hypot(sx-mouseX,sy-mouseY);if(distance<best){best=distance;result={point,label,entity,vertex};acquired=true;}};
    auto* sketch=g_doc.findSketch(drawing.name);
    if(sketch)for(size_t i=0;i<sketch->sk.ents.size();++i){const auto& entity=sketch->sk.ents[i];
        try{auto samples=sampleSketchEntity(entity,sketch->sk.tolerance,&sketch->sk);
            if(entity.kind==SketchEnt::Line&&entity.dof.size()>=4){offer({entity.dof[0],entity.dof[1]},"Endpoint",(int)i,0);offer({entity.dof[2],entity.dof[3]},"Endpoint",(int)i,1);offer({(entity.dof[0]+entity.dof[2])*.5,(entity.dof[1]+entity.dof[3])*.5},"Midpoint placement");}
            else if(entity.kind==SketchEnt::Wire){for(int vertex=0;vertex<sketchVertexCount(entity);++vertex){int at=sketchVertexDof(entity,vertex);if(at>=0&&at+1<(int)entity.dof.size())offer({entity.dof[(size_t)at],entity.dof[(size_t)at+1]},"Endpoint",(int)i,vertex);}}
            else if(entity.kind==SketchEnt::Circle&&entity.dof.size()>=3)offer({entity.dof[0],entity.dof[1]},"Center",(int)i,0);
            else if(entity.kind==SketchEnt::Arc){if(entity.dof.size()>=3)offer({entity.dof[0],entity.dof[1]},"Center placement");if(!samples.empty()){offer(samples.front(),"Endpoint placement");offer(samples.back(),"Endpoint placement");}}
            else if(!samples.empty()){offer(samples.front(),"Endpoint placement");offer(samples.back(),"Endpoint placement");}
        }catch(...){}
    }
    for(const auto& anchor:drawing.anchors)offer(anchor.point,"Draft anchor");offer({0,0},"Origin");
    if(!acquired&&g_grid){const double spacing=sketchDrawingGridSpacing();offer({std::round(raw.x/spacing)*spacing,std::round(raw.y/spacing)*spacing},"Grid placement");}
    if(!acquired&&!drawing.anchors.empty()){
        // Alignment is acquired in pixels too; coordinate equality is exact.
        const auto previous=drawing.anchors.back().point;float sx,sy;
        if(projectScreen(drawing.frame.transformPoint({previous.x,raw.y,0}),sx,sy)&&std::hypot(sx-mouseX,sy-mouseY)<8){result.point.x=previous.x;result.alignedX=true;result.label="Vertical alignment";}
        if(projectScreen(drawing.frame.transformPoint({raw.x,previous.y,0}),sx,sy)&&std::hypot(sx-mouseX,sy-mouseY)<8){result.point.y=previous.y;result.alignedY=true;result.label=result.alignedX?"Anchor alignment":"Horizontal alignment";}
    }
    return result;
}
static arkjson::Value drawingEntity(Vec2 end,bool expressions){
    using V=arkjson::Value;auto& drawing=g_sketchDrawing;V entity=V::makeObj(),dof=V::makeArr();
    if(drawing.anchors.empty())return entity;Vec2 start=drawing.anchors[0].point;
    auto value=[&](double numeric,const std::string& expression=""){return expressions&&!expression.empty()?V::makeStr(expression):V::makeNum(numeric);};
    auto coordinate=[&](double numeric,const std::string& expression=""){dof.arr.push_back(value(numeric,expression));};
    std::string expression=drawing.dimension;double dimension=0;
    if(!expression.empty()){bool ok=false;dimension=evalExpr(expression,g_doc.buildEnv(),&ok);if(!ok||!std::isfinite(dimension)||dimension<=1e-9||dimension>1e7)throw std::runtime_error("Dimension must evaluate to a positive length up to 1e7 mm");}
    if(drawing.tool==3){double radius=expression.empty()?drawingDistance(start,end):dimension;if(radius<=1e-8)throw std::runtime_error("Circle radius must be positive");entity.set("kind",V::makeStr("circle"));coordinate(start.x);coordinate(start.y);coordinate(radius,expression);}
    else if(drawing.tool==2){double dx=end.x-start.x,dy=end.y-start.y;double width=expression.empty()?std::abs(dx):dimension,height=std::abs(dy);std::string hExpr=drawing.secondDimension;
        if(!hExpr.empty()){bool ok=false;height=evalExpr(hExpr,g_doc.buildEnv(),&ok);if(!ok||!std::isfinite(height)||height<=1e-9||height>1e7)throw std::runtime_error("Height must evaluate to a positive length up to 1e7 mm");}
        if(width<=1e-8||height<=1e-8)throw std::runtime_error("Rectangle needs distinct width and height");double signX=dx<0?-1.:1.,signY=dy<0?-1.:1.;end={start.x+signX*width,start.y+signY*height};entity.set("kind",V::makeStr("wire"));entity.set("closed",V::makeBool(true));
        auto xExpr=expression.empty()?"":drawingNumber(start.x)+(signX<0?"-(":"+(")+expression+")",yExpr=hExpr.empty()?"":drawingNumber(start.y)+(signY<0?"-(":"+(")+hExpr+")";
        V segments=V::makeArr();
        // Wire DTO has an explicit start and typed segment ends.
        V startValue=V::makeArr();startValue.arr={V::makeNum(start.x),V::makeNum(start.y)};entity.set("start",startValue);
        Vec2 corners[]={{end.x,start.y},end,{start.x,end.y},start};for(auto point:corners){V segment=V::makeObj(),pair=V::makeArr();segment.set("kind",V::makeStr("line"));pair.arr={value(point.x,point.x==end.x?xExpr:""),value(point.y,point.y==end.y?yExpr:"")};segment.set("to",pair);segments.arr.push_back(segment);}entity.set("segments",segments);return entity;
    }else if(drawing.tool==4){entity.set("kind",V::makeStr("arc"));if(drawing.anchors.size()<2)return entity;V recipe=V::makeObj();recipe.set("type",V::makeStr("three_point"));auto pair=[](Vec2 p){V array=V::makeArr();array.arr={V::makeNum(p.x),V::makeNum(p.y)};return array;};recipe.set("start",pair(start));recipe.set("through",pair(drawing.anchors[1].point));recipe.set("end",pair(end));entity.set("recipe",recipe);
        if(!expression.empty()||!drawing.secondDimension.empty()){
            V sk=V::makeObj(),ents=V::makeArr();ents.arr.push_back(entity);sk.set("entities",ents);sk.set("constraints",V::makeArr());Sketch parsed;std::string error;if(!sketchFromJson(sk.dump(),parsed,&error))throw std::runtime_error(error);NamedSketch named;named.sk=parsed;evaluateSketch(named,g_doc.buildEnv());if(!named.error.empty())throw std::runtime_error(named.error);const auto& arc=named.sk.ents.front();const double a0=deg2rad(arc.dof[3]);double sweep=drawing.secondDimension.empty()?arc.dof[4]-arc.dof[3]:formNumber(drawing.secondDimension);if(std::abs(sweep)<1e-8||std::abs(sweep)>=360)throw std::runtime_error("Arc sweep must have magnitude between 0 and 360 degrees");std::string radius=expression.empty()?drawingNumber(arc.dof[2]):expression,angle=drawing.secondDimension.empty()?drawingNumber(sweep):drawing.secondDimension;
            for(const char* key:{"through","end"}){V point=V::makeArr();std::string phase="("+drawingNumber(a0)+")+deg(("+angle+")*"+(std::string(key)=="through"?"0.5":"1")+")";point.arr={V::makeStr(drawingNumber(start.x)+"+("+radius+")*(cos("+phase+")-("+drawingNumber(std::cos(a0))+"))"),V::makeStr(drawingNumber(start.y)+"+("+radius+")*(sin("+phase+")-("+drawingNumber(std::sin(a0))+"))")};recipe.set(key,point);}entity.set("recipe",recipe);
        }return entity;
    }else{Vec2 from=drawing.tool==1?drawing.anchors.back().point:start;double length=drawingDistance(from,end);double dx=length>1e-12?(end.x-from.x)/length:1.,dy=length>1e-12?(end.y-from.y)/length:0.;if(!expression.empty())end={from.x+dx*dimension,from.y+dy*dimension};entity.set("kind",V::makeStr("line"));coordinate(from.x);coordinate(from.y);coordinate(end.x,expression.empty()?"":drawingNumber(from.x)+"+("+drawingNumber(dx)+")*("+expression+")");coordinate(end.y,expression.empty()?"":drawingNumber(from.y)+"+("+drawingNumber(dy)+")*("+expression+")");}
    entity.set("dof",dof);return entity;
}
static void rebuildDrawingPreview(){
    auto& drawing=g_sketchDrawing;drawing.lines.clear();drawing.fill.clear();drawing.guides.clear();drawing.points.clear();drawing.entity={};drawing.previewError.clear();
    auto put=[&](std::vector<float>& target,Vec2 point){auto world=drawing.frame.transformPoint({point.x,point.y,0});target.insert(target.end(),{(float)world.x,(float)world.y,(float)world.z});};
    for(const auto& anchor:drawing.anchors)put(drawing.points,anchor.point);if(drawing.hover)put(drawing.points,drawing.pointer.point);
    if(drawing.anchors.empty()||!drawing.hover)return;
    try{drawing.entity=drawingEntity(drawing.pointer.point,true);if(drawing.tool==4&&drawing.anchors.size()<2){put(drawing.lines,drawing.anchors[0].point);put(drawing.lines,drawing.pointer.point);return;}
        arkjson::Value sketch=arkjson::Value::makeObj(),entities=arkjson::Value::makeArr();entities.arr.push_back(drawing.entity);sketch.set("entities",entities);sketch.set("constraints",arkjson::Value::makeArr());sketch.set("plane",arkjson::Value::makeStr(drawing.plane));Sketch parsed;std::string error;if(!sketchFromJson(sketch.dump(),parsed,&error))throw std::runtime_error(error);NamedSketch named;named.sk=parsed;evaluateSketch(named,g_doc.buildEnv());if(!named.error.empty())throw std::runtime_error(named.error);
        auto samples=sampleSketchEntity(named.sk.ents[0],.05,&named.sk);for(size_t i=1;i<samples.size();++i){put(drawing.lines,samples[i-1]);put(drawing.lines,samples[i]);}
        if((drawing.tool==2||drawing.tool==3)&&samples.size()>2){Vec2 center=drawing.tool==3?drawing.anchors[0].point:Vec2{(samples[0].x+samples[2].x)*.5,(samples[0].y+samples[2].y)*.5};for(size_t i=1;i<samples.size();++i){put(drawing.fill,center);put(drawing.fill,samples[i-1]);put(drawing.fill,samples[i]);}}
        Vec2 start=drawing.anchors.back().point,end=drawing.pointer.point;if(end.x==start.x){put(drawing.guides,{start.x,start.y-1000});put(drawing.guides,{start.x,start.y+1000});}if(end.y==start.y){put(drawing.guides,{start.x-1000,start.y});put(drawing.guides,{start.x+1000,start.y});}
    }catch(const std::exception& error){drawing.previewError=error.what();}
}
static bool moveSketchDrawing(int x,int y){
    auto& drawing=g_sketchDrawing;if(!drawing.active)return false;drawing.hover=false;
    if(!drawingCurrent()){drawing.stale=true;drawing.lines.clear();drawing.fill.clear();g_status="Sketch draft is stale / drawing changed / Esc then Draw sketch to restart";return true;}
    if(!drawingInView(x,y))return false;Vec3 origin,direction;viewRay(x,y,origin,direction);Vec3 normal=drawing.frame.transformDir({0,0,1}).normalized(),planeOrigin=drawing.frame.transformPoint({});double denominator=normal.dot(direction);
    if(std::abs(denominator)<1e-7){g_status="Sketch plane is edge-on / choose a face-on view";return true;}double travel=normal.dot(planeOrigin-origin)/denominator;if(travel<0)return true;auto local=drawing.frame.inverted().transformPoint(origin+direction*travel);if(!std::isfinite(local.x)||!std::isfinite(local.y)||std::abs(local.x)>1e7||std::abs(local.y)>1e7)return true;
    drawing.pointer=drawingSnap({local.x,local.y},x,y);drawing.hover=true;rebuildDrawingPreview();return true;
}
static void commitDrawingEntity(){
    using V=arkjson::Value;auto& drawing=g_sketchDrawing;if(!drawingCurrent())throw std::runtime_error("Sketch draft is stale; cancel and read the current drawing before drawing again");
    if(!drawing.previewError.empty())throw std::runtime_error(drawing.previewError);
    if(drawing.entity.t!=V::Obj||!drawing.entity.find("kind"))throw std::runtime_error("Choose the remaining sketch anchors before applying");
    V sketch=V::makeObj();if(auto* existing=g_doc.findSketch(drawing.name))sketch=sketchEditValue(*existing);else{sketch.set("plane",V::makeStr(drawing.plane));sketch.set("entities",V::makeArr());sketch.set("constraints",V::makeArr());}
    V entities=*sketch.find("entities"),constraints=*sketch.find("constraints"),entity=drawing.entity;int index=(int)entities.arr.size();unsigned serial=(unsigned)index+1;std::string name;do{name=std::string(drawing.tool==2?"rectangle":drawing.tool==3?"circle":drawing.tool==4?"arc":"line")+std::to_string(serial++);}while(std::any_of(entities.arr.begin(),entities.arr.end(),[&](const V& item){auto* n=item.find("name");return n&&n->asStr()==name;}));entity.set("name",V::makeStr(name));entities.arr.push_back(entity);
    // Only supported endpoint/center relations become persistent constraints.
    // Midpoints, grid and analytic arc samples are explicitly placements.
    if((drawing.tool==0||drawing.tool==1)&&drawing.dimension.empty()){
        auto add=[&](const char* type,int point,const SketchPlacement& target){V con=V::makeObj();con.set("type",V::makeStr(type));con.set("a",V::makeNum(index));con.set("pa",V::makeNum(point));con.set("b",V::makeNum(target.entity));con.set("pb",V::makeNum(target.vertex));con.set("id",V::makeStr("draw_"+name+"_"+std::to_string(constraints.arr.size())));constraints.arr.push_back(con);};
        const auto& start=drawing.tool==1?drawing.anchors.back():drawing.anchors.front();if(start.entity>=0)add("coincident",0,start);if(drawing.pointer.entity>=0)add("coincident",1,drawing.pointer);
        if(drawing.pointer.point.y==start.point.y)add("horizontal",0,{});else if(drawing.pointer.point.x==start.point.x)add("vertical",0,{});
    }
    sketch.set("entities",entities);sketch.set("constraints",constraints);V command=V::makeObj();command.set("op",V::makeStr("sketch.set"));command.set("name",V::makeStr(drawing.name));command.set("sketch",sketch);const u64 revision=drawing.revision,epoch=drawing.epoch;applyUiCommandAtRevision(command,revision,epoch);if(g_error)return;
    drawing.revision=g_session.revision;drawing.epoch=g_documentEpoch;g_activeSketch=drawing.name;g_showSketches=true;rebuildSketchOverlay();
    if(drawing.tool==0||drawing.tool==1){SketchPlacement next=drawing.pointer;auto* saved=g_doc.findSketch(drawing.name);if(saved&&index<(int)saved->sk.ents.size()){auto& line=saved->sk.ents[(size_t)index];next.point={line.dof[2],line.dof[3]};next.entity=index;next.vertex=1;}drawing.anchors={next};}else drawing.anchors.clear();
    drawing.dimension.clear();drawing.secondDimension.clear();drawing.lines.clear();drawing.fill.clear();drawing.guides.clear();drawing.points.clear();drawing.entity={};g_status=std::string(sketchDrawingToolName(drawing.tool))+" saved at r"+std::to_string(g_session.revision)+" / next anchor or Esc";
}
static bool clickSketchDrawing(int x,int y){
    if(!g_sketchDrawing.active||!drawingInView(x,y))return false;
    try{moveSketchDrawing(x,y);auto& drawing=g_sketchDrawing;if(!drawingCurrent())throw std::runtime_error("Sketch draft is stale / Esc and restart from the current drawing");if(!drawing.hover)return true;
        if(drawing.anchors.empty()||(drawing.tool==4&&drawing.anchors.size()<2)){drawing.anchors.push_back(drawing.pointer);rebuildDrawingPreview();g_status=drawing.tool==4&&drawing.anchors.size()==1?"Arc / click through point":"Click next anchor / Tab or click dimension / Esc cancel";return true;}
        if(drawingDistance(drawing.anchors.back().point,drawing.pointer.point)<1e-8)throw std::runtime_error("Choose a distinct anchor; Enter ends the polyline");commitDrawingEntity();
    }catch(const std::exception& error){g_error=true;g_status=error.what();}return true;
}
static void sketchDrawingDimensionDialog(){
    using V=arkjson::Value;
    auto& drawing=g_sketchDrawing;if(!drawing.active||drawing.anchors.empty())throw std::runtime_error("Draw the first anchor to edit its live dimensions");if(drawing.tool==4&&drawing.anchors.size()<2)throw std::runtime_error("Place the arc start and through anchors before entering radius and sweep");
    std::vector<EditorField> fields{{drawing.tool==3?"Radius / expression":drawing.tool==2?"Width / expression":"Length / expression",drawing.dimension.empty()?drawingNumber(drawing.tool==2?std::abs(drawing.pointer.point.x-drawing.anchors.front().point.x):drawingDistance(drawing.anchors.back().point,drawing.pointer.point)):drawing.dimension}};
    if(drawing.tool==2)fields.push_back({"Height / expression",drawing.secondDimension.empty()?drawingNumber(std::abs(drawing.pointer.point.y-drawing.anchors.front().point.y)):drawing.secondDimension});
    if(drawing.tool==4){fields[0].name="Radius / expression";V parsedValue=drawingEntity(drawing.pointer.point,false),sk=V::makeObj(),ents=V::makeArr();ents.arr.push_back(parsedValue);sk.set("entities",ents);sk.set("constraints",V::makeArr());Sketch parsed;std::string error;if(!sketchFromJson(sk.dump(),parsed,&error))throw std::runtime_error(error);NamedSketch named;named.sk=parsed;evaluateSketch(named,g_doc.buildEnv());if(!named.error.empty())throw std::runtime_error(named.error);if(drawing.dimension.empty())fields[0].value=drawingNumber(named.sk.ents.front().dof[2]);fields.push_back({"Signed sweep / degrees",drawing.secondDimension.empty()?drawingNumber(named.sk.ents.front().dof[4]-named.sk.ents.front().dof[3]):drawing.secondDimension});}
    if(!editFields("Live sketch dimensions","Use named parameters and expressions, e.g. width/2. Enter keeps this unsaved preview; the next anchor click commits atomically.",fields))return;
    if(!drawingCurrent())throw std::runtime_error("Drawing changed while entering dimensions; cancel this stale preview");drawing.dimension=trimField(fields[0].value);if(fields.size()>1)drawing.secondDimension=trimField(fields[1].value);rebuildDrawingPreview();if(!drawing.previewError.empty())throw std::runtime_error(drawing.previewError);
}
static void sourceParameterDialog(){
    using V=arkjson::Value;std::vector<std::pair<std::string,std::string>> expressions;std::string owner;
    auto collectSketch=[&](const NamedSketch& sketch,const std::string& entityName){for(const auto& entity:sketch.sk.ents)if(entityName.empty()||entity.name==entityName){for(const auto& expression:entity.expressions)if(!expression.empty())expressions.push_back({sketch.name+" / "+entity.name,expression});std::function<void(const V&,const std::string&)> recipeExpressions;recipeExpressions=[&](const V& value,const std::string& key){if(key=="type"||key=="text")return;if(value.t==V::Str){bool ok=false;evalExpr(value.str,g_doc.buildEnv(),&ok);if(ok)expressions.push_back({sketch.name+" / "+entity.name+" / "+key,value.str});}else if(value.t==V::Arr)for(const auto& child:value.arr)recipeExpressions(child,key);else if(value.t==V::Obj)for(const auto& child:value.obj)recipeExpressions(child.second,child.first);};if(!entity.entityRecipe.isNull())recipeExpressions(entity.entityRecipe,"recipe");if(!entity.arcRecipe.isNull())for(const char* key:{"start","through","end","tangent"})if(auto* point=entity.arcRecipe.find(key);point&&point->t==V::Arr)for(const auto& coordinate:point->arr)if(coordinate.t==V::Str)expressions.push_back({sketch.name+" / "+entity.name+" / "+key,coordinate.str});}for(const auto& constraint:sketch.sk.cons){if(!constraint.expression.empty())expressions.push_back({sketch.name+" / "+constraint.id,constraint.expression});if(!constraint.expression2.empty())expressions.push_back({sketch.name+" / "+constraint.id,constraint.expression2});}};
    const auto env=g_doc.buildEnv();
    const std::set<std::string> numericFields{"start","through","end","to","tangent","normal","origin","direction","xyz","point","points","poles","weights","knots","length","radius","angle","roll","width","height","distance","fraction","station","u","v","x","y","z","tx","ty","tz","rx","ry","rz","matrix","frame","max_extension"};
    const std::set<std::string> semanticFields{"name","id","kind","type","op","reference","face","edge","feature","instance","instances","inputs","section","sections","path","guides","profile","profiles","material","start_node","end_node"};
    std::function<void(const V&,const std::string&,bool)> collectNative;
    const std::set<std::string> scalarFields{"length","radius","angle","roll","width","height","distance","fraction","station","u","v","x","y","z","tx","ty","tz","rx","ry","rz","max_extension"};
    collectNative=[&](const V& value,const std::string& source,bool numeric){if(value.t==V::Str&&numeric){bool ok=false;double number=evalExpr(value.str,env,&ok);if(ok&&std::isfinite(number))expressions.push_back({source,value.str});}else if(value.t==V::Arr)for(const auto& item:value.arr)collectNative(item,source,numeric);else if(value.t==V::Obj)for(const auto& field:value.obj)if(!semanticFields.count(field.first))collectNative(field.second,source+" / "+field.first,numericFields.count(field.first)>0&&(field.second.t==V::Arr||scalarFields.count(field.first)>0));};
    for(const auto& pick:g_picked)if(pick.kind=="curve"&&!pick.owner.empty())if(auto* sketch=g_doc.findSketch(pick.owner)){owner=sketch->name;collectSketch(*sketch,pick.name);}
    if(expressions.empty()&&!g_activeSketch.empty())if(auto* sketch=g_doc.findSketch(g_activeSketch)){owner=sketch->name;collectSketch(*sketch,"");}
    if(expressions.empty()){auto* feature=selectedPartFeature();if(feature){owner=feature->name;std::vector<u64> pending{feature->id};std::set<u64> seen;for(size_t i=0;i<pending.size()&&i<128;++i)if(seen.insert(pending[i]).second)if(auto* source=g_doc.find(pending[i])){for(const auto& parameter:source->p)expressions.push_back({source->name+" / "+parameter.first,parameter.second});collectNative(source->nativeSpec,source->name+" / native",false);for(const auto& sketchName:drivingSketchNames(*source))if(auto* sketch=g_doc.findSketch(sketchName))collectSketch(*sketch,"");pending.insert(pending.end(),source->inputs.begin(),source->inputs.end());}}}
    std::vector<EditorField> fields;std::vector<std::string> names;std::string hint="Source: "+(owner.empty()?"select an object":owner)+". Linked named parameters only; dependent geometry recomputes through the document.";
    auto references=[](const std::string& expression,const std::string& name){size_t at=0;while((at=expression.find(name,at))!=std::string::npos){auto identifier=[](char c){return std::isalnum((unsigned char)c)||c=='_';};if((at==0||!identifier(expression[at-1]))&&(at+name.size()==expression.size()||!identifier(expression[at+name.size()])))return true;at+=name.size();}return false;};
    std::set<std::string> included;for(size_t pass=0;pass<=g_doc.params.size();++pass){bool added=false;for(const auto& parameter:g_doc.params)if(!included.count(parameter.name)){auto found=std::find_if(expressions.begin(),expressions.end(),[&](const auto& expression){return references(expression.second,parameter.name);});if(found!=expressions.end()){included.insert(parameter.name);names.push_back(parameter.name);fields.push_back({parameter.name+" <- "+found->first,parameter.expr});expressions.push_back({"parameter "+parameter.name,parameter.expr});added=true;}}if(!added)break;}
    if(fields.empty())throw std::runtime_error("Selected source has no named parameter bindings. Use Edit entity / Edit feature to author its expressions first.");
    const u64 revision=g_session.revision,epoch=g_documentEpoch;if(!editFields("Edit source parameters",hint.c_str(),fields))return;
    if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Drawing changed while editing source parameters; inspect the current object again");V commands=V::makeArr();for(size_t i=0;i<names.size();++i){V command=V::makeObj();command.set("op",V::makeStr("param.set"));command.set("name",V::makeStr(names[i]));command.set("expr",V::makeStr(trimField(fields[i].value)));commands.arr.push_back(command);}V arguments=V::makeObj(),actor=V::makeObj();actor.set("kind",V::makeStr("human"));actor.set("id",V::makeStr("studio"));arguments.set("actor",actor);arguments.set("expected_revision",V::makeNum((double)revision));arguments.set("commands",commands);runTool("doc_apply",arguments);
}
struct ViewSketchDimension {std::string sketch,entity,text;Vec3 position{};int action=70,constraint=-1;u64 revision=0,epoch=0;bool valid=false;};
static ViewSketchDimension g_viewSketchDimension;
static ViewSketchDimension selectedSketchDimension(){
    ViewSketchDimension result;const NamedSketch* named=nullptr;std::string entityName;for(const auto& pick:g_picked)if(pick.kind=="curve"&&!pick.owner.empty())if(auto* found=g_doc.findSketch(pick.owner)){named=found;entityName=pick.name;}
    if(!named&&!g_activeSketch.empty())if(auto* found=g_doc.findSketch(g_activeSketch);found&&found->sk.ents.size()==1){named=found;entityName=found->sk.ents.front().name;}
    if(!named)return result;auto found=std::find_if(named->sk.ents.begin(),named->sk.ents.end(),[&](const SketchEnt& entity){return entity.name==entityName;});if(found==named->sk.ents.end())return result;const auto& entity=*found;int index=(int)std::distance(named->sk.ents.begin(),found);Vec2 center;
    auto number=[](double value){char text[48];std::snprintf(text,sizeof text,"%.3f",value);std::string result=text;while(result.size()>1&&result.back()=='0')result.pop_back();if(result.back()=='.')result.pop_back();return result;};
    if(entity.kind==SketchEnt::Circle&&entity.dof.size()>=3){center={entity.dof[0]+entity.dof[2],entity.dof[1]};result.text="R "+number(entity.dof[2])+" mm";result.action=591;}
    else if(entity.kind==SketchEnt::Line&&entity.dof.size()>=4){center={(entity.dof[0]+entity.dof[2])*.5,(entity.dof[1]+entity.dof[3])*.5};result.text="L "+number(std::hypot(entity.dof[2]-entity.dof[0],entity.dof[3]-entity.dof[1]))+" mm";result.action=591;}
    else if(entity.kind==SketchEnt::Arc&&entity.dof.size()>=5){const double angle=deg2rad((entity.dof[3]+entity.dof[4])*.5);center={entity.dof[0]+entity.dof[2]*std::cos(angle),entity.dof[1]+entity.dof[2]*std::sin(angle)};result.text="R "+number(entity.dof[2])+" mm / "+number(entity.dof[4]-entity.dof[3])+" deg";result.action=591;}
    else if(entity.kind==SketchEnt::Recipe){auto paths=sampleSketchEntityPaths(entity,named->sk.tolerance,&named->sk);size_t count=0;for(const auto& path:paths)for(auto point:path){center=center+point;++count;}if(!count)return result;center=center/(double)count;auto* type=entity.entityRecipe.find("type");result.text=type?type->asStr():"Recipe dimensions";unsigned shown=0;for(const auto& field:entity.entityRecipe.obj)if(field.first!="type"&&field.first!="text"&&(field.second.t==arkjson::Value::Str||field.second.t==arkjson::Value::Num)){bool ok=false;double value=field.second.t==arkjson::Value::Num?field.second.num:evalExpr(field.second.str,g_doc.buildEnv(),&ok);if(field.second.t==arkjson::Value::Num)ok=true;if(ok&&shown++<3)result.text+=" / "+field.first+" "+number(value);}result.action=70;}
    else if(entity.kind==SketchEnt::Spline&&!entity.dof.empty()){center={entity.dof[0],entity.dof[1]};result.text="Spline / "+std::to_string(entity.dof.size()/2)+" driving anchors";result.action=70;}
    else if(entity.kind==SketchEnt::Wire&&entity.segments.size()==4&&std::all_of(entity.segments.begin(),entity.segments.end(),[](const SketchEnt::WireSeg& segment){return segment.kind==SketchEnt::WireSeg::Line;})){
        // Wire closure is represented by its explicit terminal vertex. The
        // persisted wire DTO does not use the spline's `closed` flag.
        int last=sketchVertexDof(entity,sketchVertexCount(entity)-1);if(last<0||last+1>=(int)entity.dof.size()||entity.dof[0]!=entity.dof[(size_t)last]||entity.dof[1]!=entity.dof[(size_t)last+1])return result;
        double minX=1e100,maxX=-1e100,minY=1e100,maxY=-1e100;for(int vertex=0;vertex<sketchVertexCount(entity);++vertex){int at=sketchVertexDof(entity,vertex);if(at<0||at+1>=(int)entity.dof.size())return result;minX=std::min(minX,entity.dof[(size_t)at]);maxX=std::max(maxX,entity.dof[(size_t)at]);minY=std::min(minY,entity.dof[(size_t)at+1]);maxY=std::max(maxY,entity.dof[(size_t)at+1]);}center={(minX+maxX)*.5,maxY};result.text="X span "+number(maxX-minX)+" / Y span "+number(maxY-minY)+" mm";
    }else return result;
    auto references=[](const std::string& expression,const std::string& name){size_t at=0;auto id=[](char c){return std::isalnum((unsigned char)c)||c=='_';};while((at=expression.find(name,at))!=std::string::npos){if((at==0||!id(expression[at-1]))&&(at+name.size()==expression.size()||!id(expression[at+name.size()])))return true;at+=name.size();}return false;};
    for(const auto& expression:entity.expressions)for(const auto& parameter:g_doc.params)if(references(expression,parameter.name))result.action=577;
    std::function<void(const arkjson::Value&)> recipeBinding=[&](const arkjson::Value& value){if(value.t==arkjson::Value::Str){for(const auto& parameter:g_doc.params)if(references(value.str,parameter.name))result.action=577;}else if(value.t==arkjson::Value::Arr)for(const auto& child:value.arr)recipeBinding(child);else if(value.t==arkjson::Value::Obj)for(const auto& child:value.obj)recipeBinding(child.second);};recipeBinding(entity.arcRecipe);recipeBinding(entity.entityRecipe);
    for(size_t i=0;i<named->sk.cons.size();++i){const auto& constraint=named->sk.cons[i];if(constraint.a==index&&(constraint.type==SketchCon::Radius||(constraint.type==SketchCon::DistPP&&constraint.b==index))){result.constraint=(int)i;result.action=89;for(const auto& parameter:g_doc.params)if(references(constraint.expression,parameter.name))result.action=577;break;}}
    result.sketch=named->name;result.entity=entityName;result.revision=g_session.revision;result.epoch=g_documentEpoch;result.position=resolvePlane(g_doc,named->sk.plane).transformPoint({center.x,center.y,0});result.valid=true;return result;
}
static void editViewportSketchDimension(){
    const auto target=g_viewSketchDimension;if(!target.valid)throw std::runtime_error("Select a saved sketch line, circle or four-edge wire to inspect its dimensions");if(target.revision!=g_session.revision||target.epoch!=g_documentEpoch)throw std::runtime_error("Dimension label is stale; select the current entity again");g_activeSketch=target.sketch;
    g_picked.clear();SelectionItem pick;pick.kind="curve";pick.owner=target.sketch;pick.name=target.entity;g_picked.push_back(pick);refreshSelectionLines();if(target.action==577)sourceParameterDialog();else if(target.action==591){curveDimensionDialog();}else if(target.action==89){g_selectedSketchConstraint=target.constraint;editSketchConstraintFields(false);}else{
        auto* named=g_doc.findSketch(target.sketch);auto found=std::find_if(named->sk.ents.begin(),named->sk.ents.end(),[&](const SketchEnt& entity){return entity.name==target.entity;});
        if(found->kind!=SketchEnt::Wire){editSketchEntityFields();return;}
        const NamedSketch snapshot=*named;const auto entity=*found;Vec2 vertices[5];if(sketchVertexCount(entity)!=5)throw std::runtime_error("Use Edit saved sketch for this wire recipe");for(int i=0;i<5;++i){int at=sketchVertexDof(entity,i);vertices[i]={entity.dof[(size_t)at],entity.dof[(size_t)at+1]};}
        if(vertices[0].y!=vertices[1].y||vertices[1].x!=vertices[2].x||vertices[2].y!=vertices[3].y||vertices[3].x!=vertices[0].x||vertices[4].x!=vertices[0].x||vertices[4].y!=vertices[0].y)throw std::runtime_error("This wire's displayed spans are measurements. Edit saved sketch to change its authored coordinates.");
        std::vector<EditorField> fields{{"Width / expression",drawingNumber(std::abs(vertices[1].x-vertices[0].x))},{"Height / expression",drawingNumber(std::abs(vertices[2].y-vertices[0].y))}};if(!editFields("Edit saved rectangle dimensions","Authored coordinate expressions retain the starting corner and direction. Existing constraints are evaluated atomically.",fields))return;
        if(target.revision!=g_session.revision||target.epoch!=g_documentEpoch)throw std::runtime_error("Rectangle changed while editing its dimension");for(const auto& field:fields){bool ok=false;double length=evalExpr(field.value,g_doc.buildEnv(),&ok);if(!ok||!std::isfinite(length)||length<=1e-8)throw std::runtime_error("Rectangle dimensions must evaluate to positive lengths");}
        using V=arkjson::Value;V sketch=sketchEditValue(snapshot),entities=*sketch.find("entities");auto index=(size_t)std::distance(named->sk.ents.begin(),found);auto edited=entities.arr[index];auto dof=*edited.find("dof");std::string x=sketchFieldText(dof.arr[0]),y=sketchFieldText(dof.arr[1]),endX="("+x+")"+(vertices[1].x<vertices[0].x?"-(":"+(")+fields[0].value+")",endY="("+y+")"+(vertices[2].y<vertices[0].y?"-(":"+(")+fields[1].value+")";
        const std::string coordinates[4][2]={{endX,y},{endX,endY},{x,endY},{x,y}};for(int i=0;i<4;++i){int at=sketchVertexDof(entity,i+1);dof.arr[(size_t)at]=V::makeStr(coordinates[i][0]);dof.arr[(size_t)at+1]=V::makeStr(coordinates[i][1]);}edited.set("dof",dof);entities.arr[index]=edited;sketch.set("entities",entities);V command=V::makeObj();command.set("op",V::makeStr("sketch.set"));command.set("name",V::makeStr(target.sketch));command.set("sketch",sketch);applyUiCommandAtRevision(command,target.revision,target.epoch);refreshSketchEditing();
    }
}
