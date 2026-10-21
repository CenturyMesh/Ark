// 048B Parametric HUD: translucent viewport overlay over HUD-exposed named
// parameters. Edits are staged locally and published as one ordinary,
// revision-checked param.set transaction with the human actor; Auto publishes each edit.
struct ParametricHudState {
    bool open=false,collapsed=false,autoRebuild=false;
    std::vector<std::pair<std::string,double>> staged;u64 stagedRevision=~u64(0),epoch=~u64(0);
    std::string result;bool resultError=false;double rebuildMs=0,recomputeMs=0;u64 resultRevision=~u64(0);
    int scroll=0,visible=0;std::vector<size_t> rowParams;
};
static ParametricHudState g_parametricHud;
static std::vector<size_t> parametricHudRows(){
    std::vector<size_t> rows;for(size_t i=0;i<g_doc.params.size();++i)if(hudExposed(g_doc.params[i]))rows.push_back(i);
    std::stable_sort(rows.begin(),rows.end(),[](size_t a,size_t b){const auto x=hudSpec(g_doc.params[a]),y=hudSpec(g_doc.params[b]);return std::tie(x.group,x.order,g_doc.params[a].name)<std::tie(y.group,y.order,g_doc.params[b].name);});
    return rows;
}
static bool parametricHudDriven(const Parameter& p){bool ok=false;evalExpr(p.expr,ExprEnv{},&ok);return !ok;}
static double* parametricHudStagedValue(const std::string& name){for(auto& entry:g_parametricHud.staged)if(entry.first==name)return &entry.second;return nullptr;}
// Agent commits refresh the rows directly from g_doc; only staged values that
// no longer have an exposed literal parameter are dropped.
static void parametricHudSync(){
    auto& h=g_parametricHud;if(h.epoch!=g_documentEpoch){h.epoch=g_documentEpoch;h.staged.clear();h.result.clear();h.resultError=false;h.scroll=0;}
    h.staged.erase(std::remove_if(h.staged.begin(),h.staged.end(),[](const auto& entry){for(const auto& p:g_doc.params)if(p.name==entry.first)return !hudExposed(p)||parametricHudDriven(p);return true;}),h.staged.end());
    if(h.staged.empty())h.stagedRevision=~u64(0);
}
static std::string parametricHudRange(const Parameter& p,const HudSpec& s){
    const bool lo=p.hud.find("min")!=nullptr,hi=p.hud.find("max")!=nullptr;HudSpec plain=s;plain.unit="none";
    std::string text=lo&&hi?hudFormat(s.min,plain)+" .. "+hudFormat(s.max,s):lo?">= "+hudFormat(s.min,s):hi?"<= "+hudFormat(s.max,s):std::string("no limits");
    return text+"  /  step "+hudFormat(s.step,plain);
}
// Publishes edits at the revision the human observed when staging them.
static void parametricHudPublish(const std::vector<std::pair<std::string,double>>& edits,u64 observed){
    using V=arkjson::Value;auto& h=g_parametricHud;if(edits.empty())return;
    if(observed!=g_session.revision){h.result="Model changed since these values were staged (revision "+std::to_string(observed)+" -> "+std::to_string(g_session.revision)+"). Review them: Rebuild again applies them on the current model, Reset discards them.";h.resultError=true;h.stagedRevision=g_session.revision;g_status=h.result;return;}
    V a=V::makeObj(),commands=V::makeArr(),actor=V::makeObj();
    for(const auto& edit:edits){V c=V::makeObj();c.set("op",V::makeStr("param.set"));c.set("name",V::makeStr(edit.first));c.set("expr",V::makeNum(edit.second));commands.arr.push_back(c);}
    a.set("commands",commands);actor.set("kind",V::makeStr("human"));actor.set("id",V::makeStr("studio"));a.set("actor",actor);a.set("expected_revision",V::makeNum((double)observed));
    const auto started=std::chrono::steady_clock::now();runTool("doc_apply",a);h.rebuildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    h.resultRevision=g_session.revision;h.resultError=g_error;
    if(g_error){h.result="Rebuild rejected / model and revision "+std::to_string(g_session.revision)+" unchanged: "+g_status;return;}
    h.recomputeMs=g_doc.recomputeMs;char text[200];std::snprintf(text,sizeof text,"Rebuilt %zu parameter%s in %.0f ms (recompute %.0f ms) / revision %llu",edits.size(),edits.size()==1?"":"s",h.rebuildMs,h.recomputeMs,(unsigned long long)g_session.revision);
    h.result=text;g_status=h.result;
}
static void parametricHudRebuild(){auto& h=g_parametricHud;parametricHudSync();if(h.staged.empty()){h.result="Nothing staged / change a value first";h.resultError=false;return;}
    const auto edits=h.staged;parametricHudPublish(edits,h.stagedRevision);if(!h.resultError&&h.resultRevision==g_session.revision)h.staged.clear();if(h.staged.empty())h.stagedRevision=~u64(0);}
// The human UI clamps to the declared range and says so; agents are rejected instead.
static void parametricHudSet(size_t index,double value,const char* how){
    auto& h=g_parametricHud;if(index>=g_doc.params.size())return;const auto& p=g_doc.params[index];if(!hudExposed(p)||parametricHudDriven(p))return;const auto s=hudSpec(p);
    const bool lo=p.hud.find("min")!=nullptr,hi=p.hud.find("max")!=nullptr;std::string note;
    if(s.unit=="count")value=std::round(value);
    if(lo&&value<s.min){value=s.min;note="Clamped to the HUD minimum "+hudFormat(s.min,s);}
    if(hi&&value>s.max){value=s.max;note="Clamped to the HUD maximum "+hudFormat(s.max,s);}
    if(std::fabs(value)<1e-12)value=0;
    if(h.autoRebuild){const std::string name=p.name;parametricHudPublish({{name,value}},g_session.revision);if(g_error)return;
        h.staged.erase(std::remove_if(h.staged.begin(),h.staged.end(),[&](const auto& e){return e.first==name;}),h.staged.end());if(h.staged.empty())h.stagedRevision=~u64(0);if(!note.empty())h.result=note+" / "+h.result;return;}
    if(auto* staged=parametricHudStagedValue(p.name)){if(value==p.value)h.staged.erase(std::remove_if(h.staged.begin(),h.staged.end(),[&](const auto& e){return e.first==p.name;}),h.staged.end());else *staged=value;}
    else if(value!=p.value){if(h.staged.empty())h.stagedRevision=g_session.revision;h.staged.push_back({p.name,value});}
    if(h.staged.empty())h.stagedRevision=~u64(0);
    h.result=note.empty()?std::to_string(h.staged.size())+" staged / "+how+" "+(s.label.empty()?p.name:s.label)+" = "+hudFormat(value,s)+" / Rebuild publishes":note+" / staged";h.resultError=false;g_status=h.result;
}
static void parametricHudStep(size_t index,int direction){
    if(index>=g_doc.params.size())return;const auto& p=g_doc.params[index];const auto s=hudSpec(p);const auto* staged=parametricHudStagedValue(p.name);const double current=staged?*staged:p.value;
    // Next point of the step grid anchored at min (or zero), even from an off-grid value.
    const double origin=p.hud.find("min")?s.min:0,k=(current-origin)/s.step;const double next=origin+(direction>0?std::floor(k+1e-9)+1:std::ceil(k-1e-9)-1)*s.step;
    const bool atMax=p.hud.find("max")&&direction>0&&current>=s.max-1e-12,atMin=p.hud.find("min")&&direction<0&&current<=s.min+1e-12;
    if(atMax||atMin){g_parametricHud.result=std::string("Already at the HUD ")+(atMax?"maximum ":"minimum ")+hudFormat(atMax?s.max:s.min,s);g_parametricHud.resultError=false;g_status=g_parametricHud.result;return;}
    parametricHudSet(index,next,direction>0?"+":"-");
}
static void parametricHudTypeValue(size_t index){
    if(index>=g_doc.params.size())return;const auto p=g_doc.params[index];const auto s=hudSpec(p);const auto* staged=parametricHudStagedValue(p.name);
    std::string text=hudFormat(staged?*staged:p.value,HudSpec{"","none","",0,0,s.step});
    if(!editText(("HUD value: "+(s.label.empty()?p.name:s.label)+" ("+parametricHudRange(p,s)+")").c_str(),text))return;
    bool ok=false;const double value=evalExpr(trimField(text),g_doc.buildEnv(),&ok);
    if(!ok){g_parametricHud.result="Enter a number or an expression of other parameters for "+p.name;g_parametricHud.resultError=true;g_status=g_parametricHud.result;return;}
    parametricHudSet(index,value,"typed");
}
// Show in HUD: exposes an existing parameter through param.set{name,hud}.
static void parametricHudExposeDialog(int index){
    using V=arkjson::Value;if(g_doc.params.empty()){g_status="Create a named parameter first (Manage > Parameters)";return;}
    std::vector<std::string> names;for(const auto& p:g_doc.params)names.push_back(p.name);
    const Parameter* chosen=index>=0&&(size_t)index<g_doc.params.size()?&g_doc.params[(size_t)index]:nullptr;
    auto fill=[&](const Parameter& p){std::vector<EditorField> fields;const auto s=hudSpec(p);const bool exposed=hudExposed(p);auto num=[&](const char* key){const auto* v=p.hud.find(key);return v?hudFormat(v->num,HudSpec{"","none","",0,0,s.step}):std::string();};
        fields.push_back({"Label",exposed&&!s.label.empty()?s.label:p.name});fields.push_back({"Minimum",exposed?num("min"):""});fields.push_back({"Maximum",exposed?num("max"):""});
        fields.push_back({"Step",exposed?hudFormat(s.step,HudSpec{"","none","",0,0,s.step}):""});EditorField unit{"Unit",exposed?s.unit:"mm"};unit.choices={"mm","deg","count","none"};fields.push_back(unit);
        fields.push_back({"Group",s.group});fields.push_back({"Order",std::to_string(s.order)});EditorField shown{"Show in HUD","1"};shown.toggle=true;fields.push_back(shown);return fields;};
    std::vector<EditorField> fields;
    if(!chosen){EditorField pick{"Parameter",names.front()};pick.choices=names;fields.push_back(pick);const auto rest=fill(g_doc.params.front());fields.insert(fields.end(),rest.begin(),rest.end());}else fields=fill(*chosen);
    if(!editFields("Show in HUD","Expose a named parameter to the Parametric HUD. Minimum < maximum and step > 0; leave a bound empty for no limit. Values outside the range are rejected for agents and clamped in the HUD.",fields))return;
    size_t at=0;std::string name=chosen?chosen->name:trimField(fields[at++].value);
    if(std::find(names.begin(),names.end(),name)==names.end()){g_status="Parameter not found: "+name;g_error=true;return;}
    V c=V::makeObj(),hud=V::makeObj();c.set("op",V::makeStr("param.set"));c.set("name",V::makeStr(name));
    const std::string label=trimField(fields[at].value),minimum=trimField(fields[at+1].value),maximum=trimField(fields[at+2].value),step=trimField(fields[at+3].value),unit=trimField(fields[at+4].value),group=trimField(fields[at+5].value),order=trimField(fields[at+6].value);const bool show=fields[at+7].value=="1";
    auto number=[&](const std::string& text,const char* key){if(text.empty())return;bool ok=false;const double value=evalExpr(text,g_doc.buildEnv(),&ok);if(!ok)throw std::runtime_error(std::string("HUD ")+key+" must be a number");hud.set(key,V::makeNum(value));};
    if(show){if(!label.empty()&&label!=name)hud.set("label",V::makeStr(label));number(minimum,"min");number(maximum,"max");number(step,"step");if(!unit.empty())hud.set("unit",V::makeStr(unit));if(!group.empty())hud.set("group",V::makeStr(group));number(order,"order");c.set("hud",hud);}
    else c.set("hud",V{});
    applyCommand(c);if(!g_error){g_parametricHud.open=true;g_parametricHud.collapsed=false;g_status=show?name+" shown in the Parametric HUD / revision "+std::to_string(g_session.revision):name+" removed from the HUD / parameter kept";}
}
static bool parametricHudWheel(POINT point,int delta){
    auto& h=g_parametricHud;if(!h.open||h.collapsed||h.rowParams.empty())return false;
    for(const auto& zone:g_zones)if(zone.id==48009&&point.x>=zone.x&&point.x<=zone.x+zone.w&&point.y>=zone.y&&point.y<=zone.y+zone.h){h.scroll=std::clamp(h.scroll+(delta>0?-1:1),0,std::max(0,(int)h.rowParams.size()-std::max(1,h.visible)));return true;}
    return false;
}
static bool parametricHudAction(int id){
    if(id<48000||id>=49000)return false;auto& h=g_parametricHud;parametricHudSync();
    if(id==48000){h.open=!h.open;if(h.open)h.collapsed=false;g_status=h.open?"Parametric HUD / change exposed parameters, then Rebuild":"Parametric HUD hidden / staged values kept until Reset";return true;}
    if(id==48001){h.collapsed=!h.collapsed;return true;}
    if(id==48002){parametricHudRebuild();return true;}
    if(id==48003){h.autoRebuild=!h.autoRebuild;h.result=h.autoRebuild?(h.staged.empty()?"Auto: each change rebuilds at once":"Auto on / Rebuild publishes the staged values first"):"Auto off: changes are staged until Rebuild";h.resultError=false;return true;}
    if(id==48004){const size_t count=h.staged.size();h.staged.clear();h.stagedRevision=~u64(0);h.result=count?"Discarded "+std::to_string(count)+" staged value"+(count==1?"":"s")+" / model unchanged":"Nothing staged";h.resultError=false;g_status=h.result;return true;}
    if(id==48005){parametricHudExposeDialog(-1);return true;}
    if(id==48006||id==48007){h.scroll=std::clamp(h.scroll+(id==48006?-1:1),0,std::max(0,(int)h.rowParams.size()-std::max(1,h.visible)));return true;}
    if(id>=48100&&id<48100+4*(int)maxHudParameters){const size_t row=(size_t)(id-48100)/4;const int part=(id-48100)%4;if(row>=h.rowParams.size())return true;const size_t index=h.rowParams[row];
        if(part==0)parametricHudStep(index,-1);else if(part==2)parametricHudStep(index,1);else if(part==1)parametricHudTypeValue(index);return true;}
    if(id>=48400&&id<48400+256){parametricHudExposeDialog(id-48400);return true;}
    return true; // 48008/48009: the glass panel consumes clicks between controls
}
// Explorer chip on a Parameters row: the entry point for Show in HUD.
template<class Rect>static void drawParametricHudChip(Rect rect,size_t index,float right,float y,float h,POINT pointer,std::string& hint){
    if(index>=g_doc.params.size())return;const bool exposed=hudExposed(g_doc.params[index]);const float w=40,x=right-w-4,cy=y+1;const bool hover=pointer.x>=x&&pointer.x<x+w&&pointer.y>=y-3&&pointer.y<y-3+h;
    rect(x,cy,w,20,6,exposed?.05f:.08f,exposed?.25f:.11f,exposed?.30f:.14f,hover?.95f:.75f,exposed?.25f:.30f,exposed?.78f:.40f,exposed?.86f:.47f,hover?.9f:.55f,1);
    const char* text=exposed?"HUD":"+HUD";const float scale=.50f;textOut(text,x+(w-textWidth(text,scale))*.5f,cy+3,scale,exposed?.62f:.62f,exposed?.95f:.71f,exposed?.99f:.78f);
    if(hover)hint=exposed?"Edit HUD exposure of "+g_doc.params[index].name:"Show "+g_doc.params[index].name+" in the Parametric HUD...";
    g_zones.insert(g_zones.end()-1,{x,y-3,w+4,h,48400+(int)index});
}
template<class Rect>static void drawParametricHud(Rect rect,POINT pointer,std::string& hint){
    auto& h=g_parametricHud;if(!h.open||g_drawingViewport.active||viewW<360||viewH<220)return;parametricHudSync();
    const auto rows=parametricHudRows();h.rowParams=rows;std::vector<Zone> zones;
    // Below the display-style menu (and Inspect failed operation), clear of the
    // selection appearance bar and the view cube when the viewport is narrow.
    const float w=std::clamp((float)viewW*.27f,270.f,340.f),x=(float)(viewX+viewW)-w-14;float top=(float)viewY+58;
    if(g_error&&!g_lastFailureId.empty())top=(float)viewY+106;
    if(selBody>=0&&(size_t)selBody<g_bodyIds.size()&&!g_toolsOpen&&!g_cableGuide035.active){const float barRight=(g_showViewCube?(float)g_cubeX+g_cubeS+24:(float)viewX+16)+390;if(x<barRight+8)top=std::max(top,(float)viewY+76);}
    if(g_showViewCube&&g_cubeS>0&&x<g_cubeX+g_cubeS+12&&top<g_cubeY+g_cubeS+12)top=(float)(g_cubeY+g_cubeS+12);
    float bottom=(float)g_compassBounds.top-10;if(g_compassBounds.bottom<=g_compassBounds.top)bottom=(float)(viewY+viewH)-16;
    {const float dockW=414,dockX=viewX+(viewW-dockW)*.5f,dockY=viewY+viewH-66-18-(g_motionActive?92.f:0.f);if(!g_busy&&!g_toolsOpen&&x<dockX+dockW&&x+w>dockX)bottom=std::min(bottom,dockY-10);}
    auto mini=[&](const char* text,float bx,float by,float bw,float bh,int id,bool accent,bool active,bool enabled,const std::string& tip){
        const bool hover=enabled&&pointer.x>=bx&&pointer.x<bx+bw&&pointer.y>=by&&pointer.y<by+bh;
        rect(bx,by,bw,bh,5,accent?.10f:active?.05f:.07f,accent?.42f:active?.28f:.10f,accent?.50f:active?.33f:.13f,enabled?(hover?.98f:.85f):.45f,accent?.33f:.28f,accent?.86f:active?.75f:.42f,accent?.93f:active?.82f:.50f,hover?.95f:.55f,1);
        if(active)rect(bx+8,by+bh-3,bw-16,2,1,.30f,.91f,.95f,1,0,0,0,0,0);
        const float scale=std::min(.58f,(bw-8)/std::max(1.f,textWidth(text,1.f)));textOut(text,bx+(bw-textWidth(text,scale))*.5f,by+(bh-16*scale/.58f)*.5f,scale,enabled?.90f:.45f,enabled?.95f:.50f,enabled?.98f:.54f);
        if(hover&&!tip.empty())hint=tip;zones.push_back({bx,by,bw,bh,enabled?id:48008});};
    const std::string count=std::to_string(rows.size())+(rows.size()==1?" parameter":" parameters")+(h.staged.empty()?"":" / "+std::to_string(h.staged.size())+" staged");
    const float headerH=38;
    if(h.collapsed||bottom-top<headerH+120){
        const float cw=std::min(w,textWidth("HUD",.66f)+textWidth(count,.54f)+120);const float cx=(float)(viewX+viewW)-cw-14;
        rect(cx+2,top+3,cw,headerH,10,0,0,0,.18f,0,0,0,0,0);rect(cx,top,cw,headerH,10,.030f,.058f,.078f,.72f,.23f,.47f,.56f,.75f,1);
        textOut("HUD",cx+12,top+10,.66f,.78f,.94f,1.f);textOut(fitText(count,.54f,cw-110),cx+16+textWidth("HUD",.66f),top+13,.54f,h.staged.empty()?.60f:.99f,h.staged.empty()?.72f:.80f,h.staged.empty()?.80f:.42f);
        mini("+",cx+cw-62,top+7,24,24,48001,false,false,true,"Expand the Parametric HUD");mini("x",cx+cw-32,top+7,24,24,48000,false,false,true,"Hide the Parametric HUD");
        zones.push_back({cx,top,cw,headerH,48009});g_zones.insert(g_zones.begin(),zones.begin(),zones.end());return;
    }
    const float groupH=22,rowH=44,footerH=h.result.empty()?50.f:88.f;std::vector<std::pair<std::string,int>> items; // group headers (-1) and rows
    {std::string group="\x01";for(size_t r=0;r<rows.size();++r){const auto s=hudSpec(g_doc.params[rows[r]]);if(s.group!=group){group=s.group;items.push_back({group,-1});}items.push_back({"",(int)r});}}
    float needed=0;for(const auto& item:items)needed+=item.second<0?groupH:rowH;
    const float bodyMax=std::max(rowH,bottom-top-headerH-footerH-8);const bool overflow=needed>bodyMax;
    h.visible=(int)std::max(1.f,std::floor((bodyMax-(overflow?groupH+22:0))/rowH));h.scroll=std::clamp(h.scroll,0,std::max(0,(int)rows.size()-h.visible));
    float bodyH=rows.empty()?66.f:std::min(needed,bodyMax);const float height=headerH+bodyH+footerH+8;
    rect(x+3,top+4,w,height,10,0,0,0,.20f,0,0,0,0,0);
    rect(x,top,w,height,10,.030f,.058f,.078f,.72f,.23f,.47f,.56f,.75f,1);rect(x+12,top+1,w-24,1,0,.39f,.75f,.86f,.35f,0,0,0,0,0);
    textOut("PARAMETRIC HUD",x+12,top+10,.62f,.80f,.94f,1.f);textOut(fitText(count,.50f,w-textWidth("PARAMETRIC HUD",.62f)-96),x+20+textWidth("PARAMETRIC HUD",.62f),top+13,.50f,h.staged.empty()?.56f:.99f,h.staged.empty()?.68f:.80f,h.staged.empty()?.76f:.42f);
    mini("-",x+w-62,top+7,24,24,48001,false,false,true,"Collapse the Parametric HUD");mini("x",x+w-32,top+7,24,24,48000,false,false,true,"Hide the Parametric HUD (staged values are kept)");
    float y=top+headerH;
    if(rows.empty()){
        textOut(fitText("No parameters are exposed yet.",.56f,w-24),x+12,y+4,.56f,.62f,.72f,.80f);textOut(fitText("Use +HUD on a Parameters row, or:",.56f,w-24),x+12,y+22,.56f,.62f,.72f,.80f);
        mini("Show a parameter...",x+12,y+40,std::min(170.f,w-24),24,48005,false,false,!g_doc.params.empty(),g_doc.params.empty()?"Create a named parameter first":"Expose an existing parameter to the HUD");y+=66;
    }else{
        const float bodyTop=y,bodyBottom=y+bodyH,listBottom=overflow?bodyBottom-22:bodyBottom;int skipped=0;std::string currentGroup="\x01";
        for(const auto& item:items){
            if(item.second<0){currentGroup=item.first;continue;}
            const int r=item.second;if(r<h.scroll){++skipped;continue;}
            const bool firstOfGroup=r==h.scroll||hudSpec(g_doc.params[rows[(size_t)r-1]]).group!=currentGroup;
            if(firstOfGroup){if(y+groupH+rowH>listBottom+1)break;std::string title=currentGroup.empty()?"PARAMETERS":currentGroup;for(auto& c:title)c=(char)std::toupper((unsigned char)c);textOut(fitText(title,.48f,w-24),x+12,y+4,.48f,.45f,.70f,.80f);rect(x+12,y+groupH-3,w-24,1,0,.20f,.33f,.40f,.6f,0,0,0,0,0);y+=groupH;}
            if(y+rowH>listBottom+1)break;
            const size_t index=rows[(size_t)r];const auto& p=g_doc.params[index];const auto s=hudSpec(p);const bool driven=parametricHudDriven(p);const double* staged=parametricHudStagedValue(p.name);const double shown=staged?*staged:p.value;
            const bool outside=(p.hud.find("min")&&p.value<s.min)||(p.hud.find("max")&&p.value>s.max);
            if(staged){rect(x+6,y+1,w-12,rowH-4,6,.30f,.20f,.04f,.55f,.98f,.70f,.26f,.55f,1);rect(x+6,y+5,3,rowH-12,1,.99f,.72f,.27f,1,0,0,0,0,0);}
            const float plusX=x+w-12-26,valueW=std::min(96.f,w*.32f),valueX=plusX-4-valueW,minusX=valueX-4-26,labelW=(driven?x+w-12-valueW:minusX)-(x+14)-6;
            textOut(fitText(s.label.empty()?p.name:s.label,.62f,labelW),x+14,y+5,.62f,.86f,.92f,.96f);
            std::string second=driven?"= "+p.expr+"  /  driven":parametricHudRange(p,s);if(staged)second="was "+hudFormat(p.value,s)+"  /  "+second;if(outside&&!staged)second="outside range  /  "+second;
            textOut(fitText(second,.48f,w-28),x+14,y+25,.48f,staged?.99f:outside?.98f:.53f,staged?.78f:outside?.48f:.65f,staged?.44f:outside?.42f:.73f);
            const std::string value=hudFormat(shown,s);
            if(driven){const float vx=x+w-12-valueW;textOut(fitText(value,.60f,valueW),vx+valueW-std::min(valueW,textWidth(value,.60f)),y+6,.60f,.66f,.76f,.84f);}
            else{
                const int base=48100+4*r;const bool hover=pointer.x>=valueX&&pointer.x<valueX+valueW&&pointer.y>=y+3&&pointer.y<y+25;
                mini("-",minusX,y+3,26,22,base,false,false,true,"Decrease "+p.name+" by "+hudFormat(s.step,s));
                rect(valueX,y+3,valueW,22,5,staged?.20f:.05f,staged?.14f:.09f,staged?.03f:.12f,hover?.98f:.85f,staged?.99f:.28f,staged?.72f:.45f,staged?.27f:.53f,hover?.95f:.6f,1);
                const float scale=std::min(.60f,(valueW-8)/std::max(1.f,textWidth(value,1.f)));textOut(value,valueX+(valueW-textWidth(value,scale))*.5f,y+6,scale,staged?.99f:.90f,staged?.84f:.96f,staged?.52f:.99f);
                zones.push_back({valueX,y+3,valueW,22,base+1});if(hover)hint="Type a value for "+p.name+" ("+parametricHudRange(p,s)+")";
                mini("+",plusX,y+3,26,22,base+2,false,false,true,"Increase "+p.name+" by "+hudFormat(s.step,s));
            }
            y+=rowH;
        }
        if(overflow){const int last=std::min((int)rows.size(),h.scroll+h.visible);const std::string more=std::to_string(h.scroll+1)+"-"+std::to_string(last)+" of "+std::to_string(rows.size());
            mini("^",x+w-62,bodyBottom-20,24,20,48006,false,false,h.scroll>0,"Scroll up");mini("v",x+w-32,bodyBottom-20,24,20,48007,false,false,h.scroll+h.visible<(int)rows.size(),"Scroll down");textOut(more,x+14,bodyBottom-18,.48f,.50f,.62f,.70f);}
        (void)bodyTop;(void)skipped;y=bodyBottom;
    }
    y+=4;rect(x+12,y,w-24,1,0,.20f,.33f,.40f,.7f,0,0,0,0,0);y+=8;
    const float bw=(w-24-12)/3;
    mini(h.staged.empty()?"Rebuild":("Rebuild ("+std::to_string(h.staged.size())+")").c_str(),x+12,y,bw,28,48002,!h.staged.empty(),false,!h.staged.empty(),"Apply every staged value in one atomic, revision-checked transaction");
    mini("Auto",x+18+bw,y,bw,28,48003,false,h.autoRebuild,true,h.autoRebuild?"Auto on: each change rebuilds at once":"Auto off: changes are staged until Rebuild");
    mini("Reset",x+24+2*bw,y,bw,28,48004,false,false,!h.staged.empty(),"Discard staged values; the model is unchanged");
    y+=36;
    if(!h.result.empty()){const float scale=.50f,width=w-24;std::string first=h.result,rest;
        if(textWidth(first,scale)>width){const std::string cut=fitText(h.result,scale,width);const size_t n=cut.size()>=3?cut.size()-3:0;size_t space=h.result.rfind(' ',n);if(space==std::string::npos||space<n/2)space=n;
            first=h.result.substr(0,space);rest=fitText(h.result.substr(std::min(h.result.size(),space+(space<h.result.size()&&h.result[space]==' '?1:0))),scale,width);}
        const float r=h.resultError?.98f:.55f,g=h.resultError?.52f:.90f,b=h.resultError?.46f:.70f;textOut(first,x+12,y,scale,r,g,b);if(!rest.empty())textOut(rest,x+12,y+17,scale,r,g,b);}
    zones.push_back({x,top,w,height,48009});g_zones.insert(g_zones.begin(),zones.begin(),zones.end());
}
