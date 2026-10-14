// Double-click exposes authored dimensions, never inferred mesh extents.
struct SolidDimensionRow { std::string key,expression; double value=0; };
struct SolidDimensions { u64 feature=0,revision=0,epoch=0; int body=-1; std::vector<SolidDimensionRow> rows; };
static SolidDimensions g_solidDimensions;
static std::vector<SolidDimensionRow> authoredSolidDimensions(const Feature& feature){
    static const std::set<std::string> keys{"width","depth","height","length","radius","radius1","radius2","r1","r2","major_radius","minor_radius","distance","angle","thickness","offset","tx","ty","tz","rx","ry","rz","sx","sy","sz"};
    std::vector<SolidDimensionRow> rows;const auto env=g_doc.buildEnv();for(const auto& [key,expression]:feature.p)if(keys.count(key)){bool ok=false;double value=evalExpr(expression,env,&ok);if(ok&&std::isfinite(value))rows.push_back({key,expression,value});}
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.key<b.key;});return rows;
}
static void exposeSolidDimensions(int body){
    if(body<0||(size_t)body>=g_bodyIds.size())return;
    cancelDirectAuthoring();cancelSketchEditing();cancelSketchDrawing();cancelComponentMove();
    selBody=body;g_explorerFeature=0;g_activeSketch.clear();g_showSketches=false;g_picked.clear();refreshSelectionLines();rebuildSketchOverlay();
    g_solidDimensions={g_bodyIds[(size_t)body],g_session.revision,g_documentEpoch,body,{}};g_pickKind=0;g_navMode=0;
    g_status="Authored solid dimensions / click a value to edit its expression / Esc closes";
}
static void editSolidDimension(size_t index){
    using V=arkjson::Value;const auto target=g_solidDimensions;
    if(target.epoch!=g_documentEpoch||target.revision!=g_session.revision)throw std::runtime_error("Displayed dimensions are stale; double-click the current solid again");
    if(index>=target.rows.size())throw std::runtime_error("Choose a displayed authored dimension");
    const auto row=target.rows[index];std::vector<EditorField> fields{{row.key+" / expression",row.expression}};
    if(!editFields("Edit authored solid dimension","Expressions use document parameters. This edits the part definition, including every placed occurrence. Invalid geometry rejects atomically.",fields))return;
    V command=V::makeObj(),p=V::makeObj();command.set("op",V::makeStr("feature.edit"));command.set("id",V::makeNum((double)target.feature));p.set(row.key,V::makeStr(trimField(fields[0].value)));command.set("p",p);applyUiCommandAtRevision(command,target.revision,target.epoch);
}
static bool handleSolidDimensionAction(int id){if(id==769){g_solidDimensions={};return true;}if(id>=760&&id<768){editSolidDimension((size_t)(id-760));return true;}return false;}
template<class Rect,class Button> static void drawSolidDimensions(Rect rect,Button button){
    auto& dimensions=g_solidDimensions;if(!dimensions.feature)return;
    if(dimensions.epoch!=g_documentEpoch||dimensions.body!=selBody||selBody<0||(size_t)selBody>=g_bodyIds.size()||g_bodyIds[(size_t)selBody]!=dimensions.feature){dimensions={};return;}
    auto* feature=g_doc.find(dimensions.feature);if(!feature){dimensions={};return;}
    dimensions.revision=g_session.revision;dimensions.rows=authoredSolidDimensions(*feature);
    const bool driving=!drivingSketchNames(*feature).empty();const size_t capacity=(size_t)std::clamp((viewH-150-(driving?34:0))/47,0,8);const size_t shown=std::min(capacity,dimensions.rows.size());float width=std::min(342.f,(float)viewW-32),x=(float)(viewX+viewW)-width-16,y=(float)viewY+152,height=108+(float)shown*47+(shown==0?32:0)+(driving?34:0);
    if(y+height>viewY+viewH-50)y=std::max((float)viewY+12,(float)(viewY+viewH)-height-50);
    g_zones.erase(std::remove_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.x<x+width&&z.x+z.w>x&&z.y<y+height&&z.y+z.h>y;}),g_zones.end());
    rect(x,y,width,height,8,.035f,.080f,.105f,.97f,.22f,.60f,.71f,.9f,1);textOut(fitText(feature->name,.70f,width-26),x+12,y+12,.70f,.71f,.95f,1);textOut("Authored dimensions / click to edit",x+12,y+36,.52f,.61f,.82f,.90f);
    for(size_t i=0;i<shown;++i){const auto& row=dimensions.rows[i];const bool angle=row.key=="angle"||row.key=="rx"||row.key=="ry"||row.key=="rz";const bool scale=row.key=="sx"||row.key=="sy"||row.key=="sz";std::string value=row.key+"  "+(angle?drawingNumber(row.value)+" deg":scale?drawingNumber(row.value):g_displayUnits.distance(row.value));float ry=y+60+(float)i*47;textOut(fitText(value,.66f,width-24),x+12,ry,.66f,.79f,.96f,1);textOut(fitText("= "+row.expression,.49f,width-24),x+12,ry+22,.49f,.61f,.79f,.87f);g_zones.push_back({x+6,ry-4,width-12,44,760+(int)i});}
    if(!shown)textOut("Driven by source recipe / use Edit feature",x+12,y+61,.52f,.71f,.84f,.91f);
    if(driving)button("Edit driving sketch",x+12,y+height-74,width-24,162);
    button(shown?"All source values":"Edit feature",x+12,y+height-40,width-110,shown?659:161);button("Close",x+width-86,y+height-40,74,769);
}
