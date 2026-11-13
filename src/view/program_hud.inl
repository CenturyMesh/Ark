// Program controls in the Parametric HUD. Installed parametric programs are read
// with parametric_program{action:"inspect"} (namespace, name, values, local_edits).
// Public values are staged like HUD rows and publish as one human transaction:
// prepare{mode:"configure"} evaluates an isolated exact candidate, then
// doc_apply{candidate_id,expected_revision} commits it. Both calls use runTool's CAD
// worker path, so the viewport keeps painting. A rejection keeps the document, the
// revision and the staged values. Detach and Remove ask for confirmation first.
struct ProgramHudState {
    bool view=false;arkjson::Value rows=arkjson::Value::makeArr();u64 revision=~u64(0),epoch=~u64(0),objectEpoch=~u64(0);double inspectMs=0;std::string inspectError,inspectScope,selected;
    std::vector<std::pair<std::string,double>> staged;std::string stagedNamespace,stagedSha;u64 stagedRevision=~u64(0);
    std::string result,code,codeNote;bool resultError=false;double prepareMs=0,applyMs=0;int scroll=0,visible=0,focus=-1;std::vector<std::string> rowNames;
};
static ProgramHudState g_programHud;
struct ProgramHudLayout {float height=0,headerH=44,rowsH=0,footerH=0;int resultLines=0;bool overflow=false,fits=true;};
namespace program_hud {
using V=arkjson::Value;
constexpr int rowBase=48700,maxRows=64;
static const V& field(const V& v,const char* key){static const V none;const auto* p=v.find(key);return p?*p:none;}
struct Spec {bool lo=false,hi=false,integer=false;double min=0,max=0,step=1;std::string unit;};
// Steps follow the HUD default: 1 for integers, otherwise a power of ten close to a
// twentieth of a bounded range. Units are labels only; values stay in ARK units.
static Spec spec(const V& s){Spec r;const auto& lo=field(s,"min");const auto& hi=field(s,"max");r.lo=lo.t==V::Num;r.hi=hi.t==V::Num;r.min=lo.num;r.max=hi.num;r.integer=field(s,"integer").t==V::Bool&&field(s,"integer").b;r.unit=field(s,"unit").asStr();
    if(!r.integer&&r.lo&&r.hi&&r.max>r.min)r.step=std::pow(10.,std::floor(std::log10((r.max-r.min)/20)));return r;}
// Shortest round-trip decimal, as the program compiler prints values.
static std::string number(double x){if(x==0)x=0;char b[40];if(x==std::floor(x)&&std::fabs(x)<1e15){std::snprintf(b,sizeof b,"%lld",(long long)x);return b;}for(int p=1;p<=17;++p){std::snprintf(b,sizeof b,"%.*g",p,x);if(std::strtod(b,nullptr)==x)break;}return b;}
static std::string format(double x,const Spec& s){return number(x)+(s.unit.empty()?"":" "+s.unit);}
static std::string range(const Spec& s){std::string t=s.lo&&s.hi?number(s.min)+" .. "+format(s.max,s):s.lo?">= "+format(s.min,s):s.hi?"<= "+format(s.max,s):std::string("no limits");return t+(s.integer?"  /  integer":"  /  step "+number(s.step));}
static bool stale(){const auto& p=g_programHud;return p.revision!=g_session.revision||p.epoch!=g_documentEpoch||p.objectEpoch!=g_objectDocumentEpoch;}
static const V* row(const std::string& ns){for(const auto& r:g_programHud.rows.arr)if(field(r,"namespace").asStr()==ns)return &r;return nullptr;}
static const V* selected(){return row(g_programHud.selected);}
static void clearStaged(){auto& p=g_programHud;p.staged.clear();p.stagedNamespace.clear();p.stagedSha.clear();p.stagedRevision=~u64(0);}
static double* stagedValue(const std::string& name){auto& p=g_programHud;if(p.stagedNamespace!=p.selected)return nullptr;for(auto& e:p.staged)if(e.first==name)return &e.second;return nullptr;}
static std::string ownedSummary(const V& r,size_t* total=nullptr){std::string text;size_t all=0;const char* names[][2]={{"features","feature"},{"sketches","sketch"},{"planes","datum"},{"materials","material"}};
    for(const auto& n:names){const size_t k=field(field(r,"owned"),n[0]).arr.size();if(!k)continue;all+=k;text+=(text.empty()?"":", ")+std::to_string(k)+" "+(k==1?n[1]:std::string(n[1])=="sketch"?"sketches":std::string(n[1])=="datum"?"datums":std::string(n[1])+"s");}
    if(total)*total=all;return text.empty()?std::string("no owned objects"):text;}
// inspect reads the bindings and the rows of their owned objects, and each row is
// serialized from its own object. A private session holding only those gives the same
// rows and local_edits without serializing the whole drawing (imported meshes, other
// parts) on the UI thread. If it cannot answer (an owned row that needs other document
// context), the live session answers.
static V inspect(){
    V a=V::makeObj();a.set("action",V::makeStr("inspect"));auto& p=g_programHud;
    if(!g_doc.parametricPrograms.isNull())try{
        AgentSession owned;owned.suppressFailureCapture();owned.suppressIterationHistory();owned.revision=g_session.revision;Document& d=owned.document;d.parametricPrograms=g_doc.parametricPrograms;
        const char* collections[4]={"features","sketches","planes","materials"};std::set<std::string> names[4];for(const auto& e:g_doc.parametricPrograms.arr)for(int c=0;c<4;++c)for(const auto& n:field(field(e,"owned"),collections[c]).arr)names[c].insert(n.asStr());
        for(const auto& f:g_doc.features)if(names[0].count(f.name))d.features.push_back(f);for(const auto& s:g_doc.sketches)if(names[1].count(s.name))d.sketches.push_back(s);
        for(const auto& plane:g_doc.planes)if(names[2].count(plane.name))d.planes.push_back(plane);for(const auto& m:g_doc.materials)if(names[3].count(m.name))d.materials.push_back(m);
        V r=owned.call("parametric_program",a);if(!field(r,"isError").b){p.inspectScope="owned objects";return r;}
    }catch(const std::exception&){}
    p.inspectScope="whole drawing";return g_session.call("parametric_program",a);
}
// Read-only and geometry-free: bindings, values and local edits at the current
// revision. It runs between frames on the UI thread, never inside drawing.
static void refresh(){
    auto& p=g_programHud;if(p.epoch!=g_documentEpoch||p.objectEpoch!=g_objectDocumentEpoch){clearStaged();p.result.clear();p.code.clear();p.resultError=false;p.scroll=0;p.focus=-1;p.selected.clear();}
    V r;const auto started=std::chrono::steady_clock::now();
    try{r=inspect();}catch(const std::exception& e){r=V::makeObj();r.set("isError",V::makeBool(true));V data=V::makeObj();data.set("error",V::makeStr(e.what()));r.set("structuredContent",data);}
    p.inspectMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();p.revision=g_session.revision;p.epoch=g_documentEpoch;p.objectEpoch=g_objectDocumentEpoch;
    if(field(r,"isError").b){p.inspectError=field(field(r,"structuredContent"),"error").asStr();p.rows=V::makeArr();}else{p.inspectError.clear();p.rows=field(field(r,"structuredContent"),"programs");if(p.rows.t!=V::Arr)p.rows=V::makeArr();}
    if(!selected()){p.selected=p.rows.arr.empty()?std::string():field(p.rows.arr.front(),"namespace").asStr();p.scroll=0;p.focus=-1;}
    // Staged values of a removed program or of a parameter it no longer declares cannot be applied.
    if(!p.staged.empty()){const V* owner=row(p.stagedNamespace);if(!owner)clearStaged();else{const auto& values=field(*owner,"values");p.staged.erase(std::remove_if(p.staged.begin(),p.staged.end(),[&](const auto& e){return field(values,e.first.c_str()).t!=V::Num;}),p.staged.end());if(p.staged.empty())clearStaged();}}
}
static void poll(){if(!g_parametricHud.open||g_parametricHud.collapsed||!g_programHud.view||g_dialog||g_busy||g_preparingEdit)return;if(stale())refresh();}
static void note(const std::string& text,bool error){auto& p=g_programHud;p.result=text;p.code.clear();p.resultError=error;g_status=text;}
// Typed rejections keep their code: PROGRAM_* from the message, otherwise error_code.
static std::string programCode(std::string& text){
    const auto at=text.find("PROGRAM_");if(at==std::string::npos)return {};const auto end=text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_",at);std::string code=text.substr(at,end==std::string::npos?std::string::npos:end-at);
    if(const auto prefix=text.find(code+": ");prefix!=std::string::npos)text=text.substr(prefix+code.size()+2);return code;
}
static void reject(const V& response,const char* stage,u64 observed){
    auto& p=g_programHud;const auto& data=field(response,"structuredContent");std::string detail=field(data,"error").asStr();if(detail.empty())detail=g_status;p.code=programCode(detail);
    // A failed exact preview carries doc_preview's report: show the evaluated batch's own
    // error, which can itself be typed (program.clear rejects PROGRAM_DEPENDANTS there).
    if(p.code=="PROGRAM_PREVIEW"){V report;if(arkjson::parse(detail,report)){std::string inner=field(report,"error").asStr();if(inner.empty())inner=field(field(report,"candidate_result"),"error").asStr();if(!inner.empty()){detail=inner;if(auto code=programCode(detail);!code.empty())p.code=code;}}}
    if(p.code.empty())p.code=field(data,"error_code").asStr();if(p.code.empty())p.code="REJECTED";
    p.result=detail;p.resultError=true;p.codeNote=g_session.revision==observed?"model unchanged at revision "+std::to_string(observed):"revision is now "+std::to_string(g_session.revision);
    g_status=std::string(stage)+" rejected / "+p.code+" / "+(g_session.revision==observed?"model and revision "+std::to_string(observed)+" unchanged":"revision is now "+std::to_string(g_session.revision))+": "+detail;g_error=true;
}
// prepare{...} then doc_apply{candidate_id}, both bound to the observed revision.
static bool publish(V args,u64 observed,const char* stage){
    auto& p=g_programHud;args.set("action",V::makeStr("prepare"));args.set("expected_revision",V::makeNum((double)observed));
    auto started=std::chrono::steady_clock::now();const auto prepared=runTool("parametric_program",args);p.prepareMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    if(g_error){reject(prepared,stage,observed);return false;}
    V apply=V::makeObj(),actor=V::makeObj();apply.set("candidate_id",field(field(prepared,"structuredContent"),"candidate_id"));apply.set("expected_revision",V::makeNum((double)observed));actor.set("kind",V::makeStr("human"));actor.set("id",V::makeStr("studio"));apply.set("actor",actor);
    started=std::chrono::steady_clock::now();const auto applied=runTool("doc_apply",apply);p.applyMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    if(g_error){reject(applied,stage,observed);return false;}
    return true;
}
// The UI clamps to the declared range and rounds integers, and says so; agents are rejected instead.
// Staged values belong to the revision the shown rows were read at, not to a newer one.
static void set(const std::string& name,double value,const char* how){
    auto& p=g_programHud;const V* r=selected();if(!r)return;const auto& committed=field(field(*r,"values"),name.c_str());if(committed.t!=V::Num)return;
    if(!p.staged.empty()&&p.stagedNamespace!=p.selected){note("Apply or Reset the staged values of "+p.stagedNamespace+" first",true);return;}
    const Spec s=spec(field(field(*r,"parameters"),name.c_str()));std::string clamp;
    if(s.integer&&value!=std::round(value)){value=std::round(value);clamp="Rounded to the integer "+number(value);}
    if(s.lo&&value<s.min){value=s.integer?std::ceil(s.min):s.min;clamp="Clamped to the program minimum "+format(value,s);}
    if(s.hi&&value>s.max){value=s.integer?std::floor(s.max):s.max;clamp="Clamped to the program maximum "+format(value,s);}
    if(std::fabs(value)<1e-12)value=0;
    if(auto* staged=stagedValue(name)){if(value==committed.num)p.staged.erase(std::remove_if(p.staged.begin(),p.staged.end(),[&](const auto& e){return e.first==name;}),p.staged.end());else *staged=value;}
    else if(value!=committed.num){if(p.staged.empty()){p.stagedRevision=p.revision;p.stagedNamespace=p.selected;p.stagedSha=field(*r,"program_sha256").asStr();}p.staged.push_back({name,value});}
    if(p.staged.empty())clearStaged();p.focus=(int)(std::find(p.rowNames.begin(),p.rowNames.end(),name)-p.rowNames.begin());
    note(clamp.empty()?std::to_string(p.staged.size())+" staged / "+how+" "+name+" = "+format(value,s)+" / Apply prepares and publishes":clamp+" / "+std::to_string(p.staged.size())+" staged",false);
}
static double current(const std::string& name){if(const auto* staged=stagedValue(name))return *staged;const V* r=selected();return r?field(field(*r,"values"),name.c_str()).num:0;}
static void step(const std::string& name,int direction){
    const V* r=selected();if(!r)return;const Spec s=spec(field(field(*r,"parameters"),name.c_str()));const double now=current(name);
    const bool atMax=s.hi&&direction>0&&now>=s.max-1e-12,atMin=s.lo&&direction<0&&now<=s.min+1e-12;
    if(atMax||atMin){note(std::string("Already at the program ")+(atMax?"maximum ":"minimum ")+format(atMax?s.max:s.min,s),false);return;}
    // Next point of the step grid anchored at min (or zero), even from an off-grid value.
    const double origin=s.lo?s.min:0,k=(now-origin)/s.step;set(name,origin+(direction>0?std::floor(k+1e-9)+1:std::ceil(k-1e-9)-1)*s.step,direction>0?"+":"-");
}
static void type(const std::string& name){
    auto& p=g_programHud;const V* r=selected();if(!r)return;const std::string ns=p.selected;const auto& sp=field(field(*r,"parameters"),name.c_str());const Spec s=spec(sp);
    const std::string description=field(sp,"description").asStr();std::string text=number(current(name));const u64 observed=p.revision,epoch=g_documentEpoch,objectEpoch=g_objectDocumentEpoch;
    if(!editText(("Program value: "+ns+" / "+name+" ("+range(s)+")"+(description.empty()?"":" / "+description)).c_str(),text))return;
    // The editor leaves other Studio windows (Design variants) usable: a value typed over
    // an older model is not staged against the newer one; the rows are re-read for review.
    if(g_session.revision!=observed||p.revision!=observed||g_documentEpoch!=epoch||g_objectDocumentEpoch!=objectEpoch){refresh();note("Model changed while editing "+name+" (revision "+std::to_string(observed)+" -> "+std::to_string(g_session.revision)+") / value not staged; review "+ns+" and type it again",true);return;}
    if(p.selected!=ns||!selected()){note("Program selection changed while editing / value not staged",true);return;}
    bool ok=false;const double value=evalExpr(trimField(text),g_doc.buildEnv(),&ok);
    if(!ok||!std::isfinite(value)){note("Enter a finite number (or an expression of document parameters) for "+name,true);return;}
    set(name,value,"typed");
}
static void apply(){
    auto& p=g_programHud;if(p.staged.empty()){note("Nothing staged / change a value first",false);return;}
    const V* r=row(p.stagedNamespace);if(!r){note("Program "+p.stagedNamespace+" is no longer installed / staged values discarded",true);clearStaged();return;}
    // Like the HUD: values staged on an older model are reviewed before they publish.
    if(p.stagedRevision!=g_session.revision){const bool source=field(*r,"program_sha256").asStr()!=p.stagedSha;
        note(std::string(source?"Program source of "+p.stagedNamespace+" changed":"Model changed")+" since these values were staged (revision "+std::to_string(p.stagedRevision)+" -> "+std::to_string(g_session.revision)+"). Review them: Apply again prepares them on the current model, Reset discards them.",true);
        p.stagedRevision=g_session.revision;p.stagedSha=field(*r,"program_sha256").asStr();return;}
    const u64 observed=p.stagedRevision;const std::string ns=p.stagedNamespace;const size_t count=p.staged.size();
    V a=V::makeObj(),values=V::makeObj();for(const auto& e:p.staged)values.set(e.first,V::makeNum(e.second));a.set("mode",V::makeStr("configure"));a.set("namespace",V::makeStr(ns));a.set("values",values);
    if(!publish(a,observed,"Apply"))return;
    clearStaged();refresh();char text[240];std::snprintf(text,sizeof text,"Applied %zu value%s to %s / revision %llu / exact candidate %.0f ms, publish %.0f ms",count,count==1?"":"s",ns.c_str(),(unsigned long long)g_session.revision,p.prepareMs,p.applyMs);note(text,false);
}
// Confirmation lists what the step does; fields are read-only and keyboard reachable.
static bool confirm(bool remove,const V& r){
    const auto ns=field(r,"namespace").asStr();const bool local=field(r,"local_edits").b;
    std::vector<EditorField> fields{{"Program",ns+" / "+field(r,"name").asStr()},{"Owned objects",ownedSummary(r)},{"Local edits",local?(remove?"yes / Remove rejects; Detach keeps them":"yes / kept as ordinary source"):"none"}};
    const char* hint=remove?"Deletes the program-owned objects and the binding in one revision. Undo restores them.":"Keeps the generated objects as ordinary editable source and drops the binding. Undo restores it.";
    bool locked=false;return editFields(remove?"Remove parametric program":"Detach parametric program",hint,fields,false,remove?"Remove program":"Detach program",nullptr,false,[&](const std::vector<HWND>& edits){if(locked)return;locked=true;for(HWND e:edits)SendMessage(e,EM_SETREADONLY,TRUE,0);});
}
static void lifecycle(bool remove){
    auto& p=g_programHud;const V* r=selected();const char* verb=remove?"Remove":"Detach";if(!r){note("No parametric program is selected",true);return;}
    if(!p.staged.empty()){note(std::string("Apply or Reset the staged values before ")+(remove?"removing ":"detaching ")+p.stagedNamespace,true);return;}
    const V binding=*r;const auto ns=field(binding,"namespace").asStr();const auto owned=ownedSummary(binding);const u64 observed=p.revision,epoch=g_documentEpoch,objectEpoch=g_objectDocumentEpoch;
    if(!confirm(remove,binding)){note(std::string(verb)+" cancelled / model unchanged",false);return;}
    // The confirmation describes the observed binding; a newer model is reviewed again, never rebased.
    if(g_documentEpoch!=epoch||g_objectDocumentEpoch!=objectEpoch||g_session.revision!=observed){refresh();note(std::string("Model changed while confirming (revision ")+std::to_string(observed)+" -> "+std::to_string(g_session.revision)+") / "+verb+" not sent; review "+ns+" and try again",true);return;}
    V a=V::makeObj();a.set("mode",V::makeStr(remove?"remove":"detach"));a.set("namespace",V::makeStr(ns));
    if(!publish(a,observed,verb))return;
    refresh();note(remove?"Removed "+ns+": "+owned+" deleted with the binding / revision "+std::to_string(g_session.revision)+" / Undo restores them":"Detached "+ns+": "+owned+" kept as ordinary editable source / revision "+std::to_string(g_session.revision),false);
}
static void cycle(int direction){
    auto& p=g_programHud;const size_t n=p.rows.arr.size();if(n<2)return;if(!p.staged.empty()){note("Apply or Reset the staged values of "+p.stagedNamespace+" first",true);return;}
    size_t i=0;while(i<n&&field(p.rows.arr[i],"namespace").asStr()!=p.selected)++i;p.selected=field(p.rows.arr[(i%n+n+direction)%n],"namespace").asStr();p.scroll=0;p.focus=-1;p.result.clear();p.code.clear();p.resultError=false;
}
static std::vector<std::string> wrap(const std::string& text,float scale,float width,size_t lines){
    std::vector<std::string> out;std::string rest=text;
    while(!rest.empty()&&out.size()+1<lines&&textWidth(rest,scale)>width){const std::string cut=fitText(rest,scale,width);const size_t n=cut.size()>=3?cut.size()-3:0;size_t space=rest.rfind(' ',n);if(space==std::string::npos||space<n/2)space=n;if(!space)break;
        out.push_back(rest.substr(0,space));rest=rest.substr(std::min(rest.size(),space+(space<rest.size()&&rest[space]==' '?1:0)));}
    if(!rest.empty())out.push_back(fitText(rest,scale,width));return out;
}
}
static void programHudPoll(){program_hud::poll();}
static std::string programHudCount(){const size_t n=g_doc.parametricPrograms.arr.size();const auto& p=g_programHud;return std::to_string(n)+(n==1?" program":" programs")+(p.staged.empty()?"":" / "+std::to_string(p.staged.size())+" staged");}
static ProgramHudLayout programHudLayout(float avail){
    using namespace program_hud;auto& p=g_programHud;ProgramHudLayout l;const V* r=stale()?nullptr:selected();const float rowH=44;
    const size_t n=r?std::min<size_t>(field(*r,"parameters").obj.size(),maxRows):0;const float needed=r?std::max(rowH,n*rowH):40.f,minimum=std::min(needed,n>1?rowH+22:needed);
    // The result shrinks before the rows (the status bar keeps the full text); one row stays visible.
    l.resultLines=p.result.empty()?0:p.code.empty()?2:3;auto footer=[&]{return 44+(l.resultLines?l.resultLines*17.f+8:0.f);};
    while(l.resultLines>0&&avail-l.headerH-footer()<minimum)--l.resultLines;l.footerH=footer();
    const float bodyMax=std::max(minimum,avail-l.headerH-l.footerH);l.overflow=needed>bodyMax;
    p.visible=(int)std::max(1.f,std::floor((bodyMax-(l.overflow?22.f:0.f))/rowH));if(p.focus>=0&&p.focus<(int)n){if(p.focus<p.scroll)p.scroll=p.focus;else if(p.focus>=p.scroll+p.visible)p.scroll=p.focus-p.visible+1;}p.scroll=std::clamp(p.scroll,0,std::max(0,(int)n-p.visible));
    l.rowsH=l.overflow?p.visible*rowH+22:needed;l.height=l.headerH+l.rowsH+l.footerH;l.fits=l.height<=avail+.5f;return l;
}
static bool programHudWheel(int delta){auto& p=g_programHud;if(p.rowNames.empty())return false;p.focus=-1;p.scroll=std::clamp(p.scroll+(delta>0?-1:1),0,std::max(0,(int)p.rowNames.size()-std::max(1,p.visible)));return true;}
template<class Mini>static void drawProgramHudTabs(Mini& mini,float x,float y,float w,size_t parameters){
    const size_t programs=g_doc.parametricPrograms.arr.size();const float tw=(w-24-6)/2;
    mini(("Parameters ("+std::to_string(parameters)+")").c_str(),x+12,y+2,tw,24,48010,false,!g_programHud.view,true,"Named document parameters exposed to the HUD");
    mini(("Programs ("+std::to_string(programs)+")").c_str(),x+18+tw,y+2,tw,24,48011,false,g_programHud.view,true,"Installed parametric programs: configure, detach or remove");
}
template<class Rect,class Mini>static void drawProgramHud(Rect rect,Mini& mini,const ProgramHudLayout& l,float x,float y,float w,POINT pointer,std::string& hint,std::vector<Zone>& zones){
    using namespace program_hud;auto& p=g_programHud;const bool fresh=!stale();const V* r=fresh?selected():nullptr;const float rowH=44;p.rowNames.clear();
    const bool otherStaged=!p.staged.empty()&&p.stagedNamespace!=p.selected;
    if(!r){
        const std::string first=!fresh?"Reading program bindings...":!p.inspectError.empty()?"Program bindings unavailable: "+p.inspectError:"No parametric programs are installed.";
        textOut(fitText(first,.56f,w-24),x+12,y+6,.56f,.677f,.705f,.728f);
        if(fresh&&p.inspectError.empty())textOut(fitText("Install one: All tools > Parametric configurator.",.48f,w-24),x+12,y+26,.48f,.577f,.6f,.62f);
    }else{
        // Header: namespace / name, previous / next binding and the 067G configurator.
        const std::string ns=field(*r,"namespace").asStr();const size_t total=p.rows.arr.size();const bool local=field(*r,"local_edits").b;const float navW=total>1?54.f:0.f,varW=64.f;
        textOut(fitText(ns+" / "+field(*r,"name").asStr(),.60f,w-24-navW-varW-6),x+12,y+4,.60f,.874f,.91f,.94f);
        if(total>1){mini("<",x+w-12-varW-6-52,y+2,24,20,48012,false,false,p.staged.empty(),p.staged.empty()?"Previous installed program":"Apply or Reset the staged values first");mini(">",x+w-12-varW-6-26,y+2,24,20,48013,false,false,p.staged.empty(),p.staged.empty()?"Next installed program":"Apply or Reset the staged values first");}
        mini("Variants",x+w-12-varW,y+2,varW,20,48014,false,false,true,"Open Design variants for "+ns+": saved configurations, comparison and exploration");
        size_t owned=0;const std::string summary=ownedSummary(*r,&owned);
        const std::string status=local?"Local edits / Apply and Remove reject; Detach keeps the edits":"No local edits / "+summary+(total>1?"  /  "+std::to_string(total)+" programs":"");
        textOut(fitText(status,.48f,w-24),x+12,y+24,.48f,local?.99f:.55f,local?.74f:.80f,local?.38f:.66f);
        if(local&&pointer.x>=x+12&&pointer.x<x+w-12&&pointer.y>=y+22&&pointer.y<y+40)hint="Owned objects of "+ns+" changed after the last run ("+summary+"). Configure and Remove reject with PROGRAM_LOCAL_EDITS.";
        rect(x+12,y+l.headerH-3,w-24,1,0,.295f,.307f,.333f,.6f,0,0,0,0,0);
        const auto& parameters=field(*r,"parameters");const auto& values=field(*r,"values");for(const auto& kv:parameters.obj)p.rowNames.push_back(kv.first);std::sort(p.rowNames.begin(),p.rowNames.end()); // stored key order is not stable
        float ry=y+l.headerH;const float listBottom=ry+l.rowsH-(l.overflow?22.f:0.f);
        for(int i=p.scroll;i<(int)p.rowNames.size()&&i<maxRows;++i){
            if(ry+rowH>listBottom+1)break;const auto& name=p.rowNames[(size_t)i];const auto& sp=field(parameters,name.c_str());const auto& committed=field(values,name.c_str());const Spec s=spec(sp);const double* staged=stagedValue(name);
            // Updated program source can narrow a range under a staged value; Apply then rejects with PROGRAM_VALUES.
            const bool numeric=committed.t==V::Num;const double shownValue=staged?*staged:committed.num;const bool outside=numeric&&((s.lo&&shownValue<s.min)||(s.hi&&shownValue>s.max));
            if(staged){rect(x+6,ry+1,w-12,rowH-4,6,.30f,.20f,.04f,.55f,.98f,.70f,.26f,.55f,1);rect(x+6,ry+5,3,rowH-12,1,.99f,.72f,.27f,1,0,0,0,0,0);}
            const float plusX=x+w-12-26,valueW=std::min(96.f,w*.32f),valueX=plusX-4-valueW,minusX=valueX-4-26,labelW=(numeric?minusX:x+w-12-valueW)-(x+14)-6;
            textOut(fitText(name,.62f,labelW),x+14,ry+5,.62f,.874f,.91f,.94f);
            std::string second=numeric?range(s):"Not a number / configure it with an agent";if(staged)second="was "+format(committed.num,s)+"  /  "+second;if(outside)second="outside range  /  "+second;
            textOut(fitText(second,.48f,w-28),x+14,ry+25,.48f,outside?.98f:staged?.99f:.53f,outside?.48f:staged?.78f:.65f,outside?.42f:staged?.44f:.73f);
            const std::string shown=numeric?format(shownValue,s):committed.dump();
            if(!numeric){const float vx=x+w-12-valueW;textOut(fitText(shown,.60f,valueW),vx+valueW-std::min(valueW,textWidth(shown,.60f)),ry+6,.60f,.715f,.745f,.769f);}
            else{
                const int base=rowBase+4*i;const bool enabled=!otherStaged;const bool hover=enabled&&pointer.x>=valueX&&pointer.x<valueX+valueW&&pointer.y>=ry+3&&pointer.y<ry+25;const std::string description=field(sp,"description").asStr();
                mini("-",minusX,ry+3,26,22,base,false,false,enabled,"Decrease "+name+(s.integer?" by 1":" by "+format(s.step,s)));
                rect(valueX,ry+3,valueW,22,5,staged?.20f:.05f,staged?.14f:.09f,staged?.03f:.12f,hover?.98f:.85f,staged?.99f:.28f,staged?.72f:.45f,staged?.27f:.53f,hover?.95f:.6f,1);
                const float scale=std::min(.60f,(valueW-8)/std::max(1.f,textWidth(shown,1.f)));textOut(shown,valueX+(valueW-textWidth(shown,scale))*.5f,ry+6,scale,staged?.99f:.90f,staged?.84f:.96f,staged?.52f:.99f);
                zones.push_back({valueX,ry+3,valueW,22,enabled?base+1:48008});if(hover)hint="Type a value for "+name+" ("+range(s)+")"+(description.empty()?"":" / "+description);
                mini("+",plusX,ry+3,26,22,base+2,false,false,enabled,"Increase "+name+(s.integer?" by 1":" by "+format(s.step,s)));
            }
            ry+=rowH;
        }
        if(l.overflow){const int last=std::min((int)p.rowNames.size(),p.scroll+p.visible);const float by=y+l.headerH+l.rowsH-20;
            mini("^",x+w-62,by,24,20,48019,false,false,p.scroll>0,"Scroll up");mini("v",x+w-32,by,24,20,48020,false,false,p.scroll+p.visible<(int)p.rowNames.size(),"Scroll down");textOut(std::to_string(p.scroll+1)+"-"+std::to_string(last)+" of "+std::to_string(p.rowNames.size()),x+14,by+2,.48f,.577f,.6f,.62f);}
    }
    float fy=y+l.headerH+l.rowsH+4;rect(x+12,fy,w-24,1,0,.295f,.307f,.333f,.7f,0,0,0,0,0);fy+=8;const float bw=(w-24-18)/4;const bool any=r!=nullptr,pending=!p.staged.empty();
    mini(pending?("Apply ("+std::to_string(p.staged.size())+")").c_str():"Apply",x+12,fy,bw,28,48015,pending,false,pending&&fresh,"Prepare the staged values as one exact candidate, then publish it at the observed revision");
    mini("Reset",x+18+bw,fy,bw,28,48016,false,false,pending,"Discard staged values; the model is unchanged");
    mini("Detach",x+24+2*bw,fy,bw,28,48017,false,false,any&&!pending,"Keep the generated objects as ordinary editable source and drop the binding (asks first)");
    mini("Remove",x+30+3*bw,fy,bw,28,48018,false,false,any&&!pending,"Delete the program-owned objects and the binding in one revision (asks first)");
    fy+=36;
    if(l.resultLines>0){const float scale=.50f,width=w-24,red=p.resultError?.98f:.55f,green=p.resultError?.52f:.90f,blue=p.resultError?.46f:.70f;int lines=l.resultLines;
        if(!p.code.empty()){textOut(fitText(p.code+"  /  "+p.codeNote,scale,width),x+12,fy,scale,.99f,.45f,.40f);fy+=17;--lines;}
        if(lines>0)for(const auto& line:wrap(p.result,scale,width,(size_t)lines)){textOut(line,x+12,fy,scale,red,green,blue);fy+=17;}}
}
static bool programHudAction(int id){
    using namespace program_hud;auto& p=g_programHud;
    if(id==48010){p.view=false;g_status="Parametric HUD / exposed parameters";return true;}
    if(id==48011){p.view=true;refresh();g_status="Parametric HUD / programs: change values, then Apply";return true;}
    if(!((id>=48012&&id<=48020)||(id>=rowBase&&id<rowBase+4*maxRows)))return false;
    // A click drawn from an older revision is not applied to a newer model.
    if(stale()){refresh();note("Program bindings changed / review them and try again",false);return true;}
    if(id==48012||id==48013){cycle(id==48012?-1:1);return true;}
    // An open configurator on the same namespace keeps its own staged values.
    if(id==48014){if(const V* r=selected()){auto& variants=design_variants_view_067g::state;const auto ns=field(*r,"namespace").asStr();const bool other=variants.ns!=ns;variants.ns=ns;if(variants.window&&other)design_variants_view_067g::refresh();openDesignVariantsWindow();}return true;}
    if(id==48015){apply();return true;}
    if(id==48016){const size_t count=p.staged.size();clearStaged();note(count?"Discarded "+std::to_string(count)+" staged value"+(count==1?"":"s")+" / model unchanged":"Nothing staged",false);return true;}
    if(id==48017||id==48018){lifecycle(id==48018);return true;}
    if(id==48019||id==48020){p.focus=-1;p.scroll=std::clamp(p.scroll+(id==48019?-1:1),0,std::max(0,(int)p.rowNames.size()-std::max(1,p.visible)));return true;}
    const int index=(id-rowBase)/4,part=(id-rowBase)%4;if(index>=(int)p.rowNames.size())return true;const std::string name=p.rowNames[(size_t)index];
    if(part==0)step(name,-1);else if(part==2)step(name,1);else if(part==1)type(name);return true;
}
