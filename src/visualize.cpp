#include "ark/exploded_view.hpp"
#include "ark/agent.hpp"
#include "ark/bom.hpp"
#include "ark/history_authoring.hpp"
#include "ark/media_044b.hpp"
#include "ark/motion_graph.hpp"
#include "ark/snapshot_032d.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
namespace ark {
namespace {
using V=arkjson::Value;
void need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
// 5x7 ASCII glyphs (32..126), column bytes, least significant bit at the top; bit 7 is a descender row.
const unsigned char kFont[95][5]={
{0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x56,0x20,0x50},{0x00,0x08,0x07,0x03,0x00},
{0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x2A,0x1C,0x7F,0x1C,0x2A},{0x08,0x08,0x3E,0x08,0x08},{0x00,0x80,0x70,0x30,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x00,0x60,0x60,0x00},{0x20,0x10,0x08,0x04,0x02},
{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x72,0x49,0x49,0x49,0x46},{0x21,0x41,0x49,0x4D,0x33},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x31},{0x41,0x21,0x11,0x09,0x07},
{0x36,0x49,0x49,0x49,0x36},{0x46,0x49,0x49,0x29,0x1E},{0x00,0x00,0x14,0x00,0x00},{0x00,0x40,0x34,0x00,0x00},{0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x59,0x09,0x06},
{0x3E,0x41,0x5D,0x59,0x4E},{0x7C,0x12,0x11,0x12,0x7C},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x41,0x3E},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x41,0x51,0x73},
{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x1C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
{0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x26,0x49,0x49,0x49,0x32},{0x03,0x01,0x7F,0x01,0x03},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
{0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x59,0x49,0x4D,0x43},{0x00,0x7F,0x41,0x41,0x41},{0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x41,0x7F},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
{0x00,0x03,0x07,0x08,0x00},{0x20,0x54,0x54,0x78,0x40},{0x7F,0x28,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x28},{0x38,0x44,0x44,0x28,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x00,0x08,0x7E,0x09,0x02},{0x18,0xA4,0xA4,0x9C,0x78},
{0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x40,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x78,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
{0xFC,0x18,0x24,0x24,0x18},{0x18,0x24,0x24,0x18,0xFC},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x24},{0x04,0x04,0x3F,0x44,0x24},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
{0x44,0x28,0x10,0x28,0x44},{0x4C,0x90,0x90,0x90,0x7C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x77,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x02,0x01,0x02,0x04,0x02}};
void blend(Image& img,int x,int y,const double c[3],double a){if(x<0||y<0||x>=img.w||y>=img.h||a<=0)return;a=std::min(a,1.);u8* p=&img.px[((size_t)y*img.w+x)*3];for(int k=0;k<3;++k)p[k]=(u8)std::clamp(std::lround(p[k]*(1-a)+c[k]*255*a),0l,255l);}
void fillRect(Image& img,double x,double y,double w,double h,const double c[3],double a=1){for(int py=std::max(0,(int)std::floor(y));py<std::min(img.h,(int)std::ceil(y+h));++py)for(int px=std::max(0,(int)std::floor(x));px<std::min(img.w,(int)std::ceil(x+w));++px)blend(img,px,py,c,a);}
void line(Image& img,double ax,double ay,double bx,double by,double width,const double c[3]){
    const double dx=bx-ax,dy=by-ay,len2=dx*dx+dy*dy,r=width*.5;
    for(int y=std::max(0,(int)std::floor(std::min(ay,by)-r-1));y<=std::min(img.h-1,(int)std::ceil(std::max(ay,by)+r+1));++y)for(int x=std::max(0,(int)std::floor(std::min(ax,bx)-r-1));x<=std::min(img.w-1,(int)std::ceil(std::max(ax,bx)+r+1));++x){
        const double px=x+.5,py=y+.5,t=len2>0?std::clamp(((px-ax)*dx+(py-ay)*dy)/len2,0.,1.):0.,ex=ax+t*dx-px,ey=ay+t*dy-py;blend(img,x,y,c,std::clamp(r+.5-std::sqrt(ex*ex+ey*ey),0.,1.));}
}
void disk(Image& img,double cx,double cy,double radius,const double fill[3],const double border[3],double borderWidth){
    for(int y=std::max(0,(int)std::floor(cy-radius-2));y<=std::min(img.h-1,(int)std::ceil(cy+radius+2));++y)for(int x=std::max(0,(int)std::floor(cx-radius-2));x<=std::min(img.w-1,(int)std::ceil(cx+radius+2));++x){
        const double d=std::hypot(x+.5-cx,y+.5-cy),outer=std::clamp(radius+.5-d,0.,1.),inner=std::clamp(radius-borderWidth+.5-d,0.,1.);blend(img,x,y,fill,inner);blend(img,x,y,border,outer-inner);}
}
std::string fitChars(const std::string& text,size_t chars){std::string out;for(char c:text)out+=(c>=32&&c<127)?c:'?';if(out.size()>chars)out=out.substr(0,chars>2?chars-2:0)+"..";return out;}
double srgbToLinear(double v){v=std::clamp(v,0.,1.);return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);}
double linearToSrgb(double v){v=std::max(v,0.);v=v*(1+v/4)/(1+v);return v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055;}
V vec(Vec3 p){V a=V::makeArr();for(double x:{p.x,p.y,p.z})a.arr.push_back(V::makeNum(x));return a;}
Vec3 readVector(const V& value,const char* name){need(value.t==V::Arr&&value.arr.size()==3,std::string("VISUALIZE_INVALID: camera ")+name+" must contain three finite numbers");double c[3];for(size_t i=0;i<3;++i){need(value.arr[i].t==V::Num&&std::isfinite(value.arr[i].num)&&std::fabs(value.arr[i].num)<=1e9,std::string("VISUALIZE_INVALID: camera ")+name+" must contain three finite numbers");c[i]=value.arr[i].num;}return {c[0],c[1],c[2]};}
struct RenderTri {Vec3 p[3],n[3],face;int owner=-1;size_t material=0;float metallic=0,roughness=.55f;};
struct Look {Vec3 dir[3],tint[3];double power[3]{},ambient=.4,rim=.2,outline=.3;bool creases=false;};
Look lookPreset(const std::string& name,Vec3 right,Vec3 up,Vec3 back){
    auto L=[&](double a,double b,double c){return (right*a+up*b+back*c).normalized();};Look k;k.tint[0]={1,.97,.92};k.tint[1]={.86,.92,1};k.tint[2]={1,1,1};
    if(name=="soft"){k.dir[0]=L(-.3,.8,.6);k.dir[1]=L(.6,.3,.7);k.dir[2]=L(0,.5,-.8);k.power[0]=.95;k.power[1]=.70;k.power[2]=.25;k.ambient=.62;k.rim=.10;k.outline=.18;}
    else if(name=="dramatic"){k.dir[0]=L(-.9,.55,.25);k.dir[1]=L(.6,-.2,.4);k.dir[2]=L(.55,.5,-.7);k.power[0]=2.3;k.power[1]=.12;k.power[2]=1.3;k.ambient=.10;k.rim=.35;k.outline=.30;}
    else if(name=="technical"){k.dir[0]=L(-.4,.7,.6);k.dir[1]=L(.5,.3,.7);k.dir[2]=L(0,.6,-.7);k.power[0]=.85;k.power[1]=.55;k.power[2]=0;k.ambient=.55;k.rim=0;k.outline=1;k.creases=true;}
    else{k.dir[0]=L(-.5,.8,.6);k.dir[1]=L(.7,.2,.5);k.dir[2]=L(0,.4,-.9);k.power[0]=1.45;k.power[1]=.45;k.power[2]=.65;k.ambient=.32;k.rim=.18;k.outline=.30;}
    return k;
}
const char* kResolutions[]={"640x400","1024x640","1280x720","1280x800","1600x900","1920x1080"};
}

int visualizeTextWidth(const std::string& text,int scale){return text.empty()?0:(int)text.size()*6*scale-scale;}
int drawVisualizeText(Image& img,const std::string& text,int x,int y,int scale,const u8 rgb[3]){
    const double c[3]={rgb[0]/255.,rgb[1]/255.,rgb[2]/255.};const int start=x;
    for(unsigned char ch:text){const auto& glyph=kFont[ch>=32&&ch<127?ch-32:'?'-32];for(int col=0;col<5;++col)for(int row=0;row<8;++row)if(glyph[col]>>row&1)fillRect(img,x+col*scale,y+row*scale,scale,scale,c);x+=6*scale;}
    return x-start;
}
std::vector<VisualizeBomItem> visualizeBomItems(const Document& d,u64 revision){
    std::vector<VisualizeBomItem> out;const V bom=inspectBom(d,revision);const auto* items=bom.find("items");if(!items||items->t!=V::Arr)return out;
    for(const auto& row:items->arr){VisualizeBomItem item;item.item=(int)out.size()+1;item.feature=(u64)row.find("feature")->num;item.name=row.find("name")->asStr();item.quantity=(int)row.find("quantity")->asNum();
        if(const auto* meta=row.find("metadata"))if(const auto* pn=meta->find("part_number"))item.partNumber=pn->asStr();out.push_back(std::move(item));}
    return out;
}
VisualizeOverlayLayout layoutVisualizeOverlay(int width,int height,const std::vector<VisualizeBomItem>& items,const std::vector<VisualizeAnchor>& anchors,const double silhouette[4],bool balloons,bool table){
    VisualizeOverlayLayout out;const int s=width<800?1:width<1500?2:3;out.scale=s;const double margin=10.*s,pad=3.*s,footer=11.*s;
    double leftLimit=margin,rightLimit=width-margin;
    if(table&&!items.empty()){
        const size_t nameChars=(size_t)std::clamp((int)(width*.30/(6*s)),10,40);size_t longest=4,digits=4;for(const auto& item:items){const std::string name=item.partNumber.empty()?item.name:item.name+" ("+item.partNumber+")";longest=std::max(longest,std::min(nameChars,name.size()));digits=std::max(digits,std::to_string(item.item).size());}
        out.columns={digits*6.*s+2*pad+2*s,longest*6.*s+2*pad+2*s,3*6.*s+2*pad+2*s};out.rowH=8.*s+2*pad;out.tableW=out.columns[0]+out.columns[1]+out.columns[2];
        const size_t capacity=(size_t)std::max(1.,std::floor((height-2*margin-footer)/out.rowH)-2);const size_t shown=std::min(items.size(),capacity);out.hiddenRows=items.size()-shown;
        out.rows.push_back({"ITEM","NAME","QTY"});for(size_t i=0;i<shown;++i){const auto& item=items[i];out.rows.push_back({std::to_string(item.item),fitChars(item.partNumber.empty()?item.name:item.name+" ("+item.partNumber+")",nameChars),std::to_string(item.quantity)});}
        if(out.hiddenRows)out.rows.push_back({"","+"+std::to_string(out.hiddenRows)+" more items",""});
        out.tableH=out.rowH*out.rows.size();out.tableX=width-margin-out.tableW;out.tableY=height-margin-footer-out.tableH;
        // The corner covering the least of the model silhouette; bottom right on ties.
        double least=std::numeric_limits<double>::infinity();const double sx0=silhouette[0],sy0=silhouette[1],sx1=silhouette[2],sy1=silhouette[3];
        for(int corner=0;corner<4;++corner){const double tx=corner&1?margin:width-margin-out.tableW,ty=corner&2?margin:height-margin-footer-out.tableH;
            const double overlap=sx1>sx0&&sy1>sy0?std::max(0.,std::min(tx+out.tableW,sx1)-std::max(tx,sx0))*std::max(0.,std::min(ty+out.tableH,sy1)-std::max(ty,sy0)):0;if(overlap<least-1e-9){least=overlap;out.tableX=tx;out.tableY=ty;}}
        if(out.tableX>width/2.)rightLimit=out.tableX-margin;else leftLimit=out.tableX+out.tableW+margin;
    }
    if(!balloons||anchors.empty())return out;
    size_t digits=1;for(const auto& a:anchors)digits=std::max(digits,std::to_string(a.item).size());const double r=std::max(7.*s,(digits*6.*s)/2+4.*s);
    const double x0=leftLimit+r,x1=std::max(x0,rightLimit-r),y0=margin+r,y1=std::max(y0,height-margin-footer-r);
    double sx0=silhouette[0],sy0=silhouette[1],sx1=silhouette[2],sy1=silhouette[3];if(!(sx1>sx0&&sy1>sy0)){sx0=sx1=anchors[0].x;sy0=sy1=anchors[0].y;for(const auto& a:anchors){sx0=std::min(sx0,a.x);sx1=std::max(sx1,a.x);sy0=std::min(sy0,a.y);sy1=std::max(sy1,a.y);}}
    const double cx=(sx0+sx1)/2,cy=(sy0+sy1)/2,ex=(sx1-sx0)/2+3*r,ey=(sy1-sy0)/2+3*r;
    struct Slot {size_t index;double angle;};std::vector<Slot> slots;
    for(size_t i=0;i<anchors.size();++i){const double dx=anchors[i].x-cx,dy=anchors[i].y-cy;slots.push_back({i,std::hypot(dx,dy)>1e-6?std::atan2(dy,dx):-K_PI/2+2*K_PI*(double)i/anchors.size()});}
    std::sort(slots.begin(),slots.end(),[](const Slot& a,const Slot& b){return a.angle<b.angle;});const size_t n=slots.size();
    const double spacing=std::min(2*K_PI/n,2.6*r/std::max((ex+ey)/2,1.));
    for(int iteration=0;iteration<200&&n>1;++iteration)for(size_t i=0;i<n;++i){auto& a=slots[i];auto& b=slots[(i+1)%n];double gap=b.angle-a.angle;if(i+1==n)gap+=2*K_PI;if(gap<spacing){const double push=(spacing-gap)*.5;a.angle-=push;b.angle+=push;}}
    for(const auto& slot:slots){VisualizeBalloon b;const auto& a=anchors[slot.index];b.item=a.item;b.ax=a.x;b.ay=a.y;b.radius=r;// On the silhouette box grown by 3 radii, along the anchor's bearing from the model centre.
        const double c=std::cos(slot.angle),sn=std::sin(slot.angle),t=std::min(std::fabs(c)>1e-9?ex/std::fabs(c):1e300,std::fabs(sn)>1e-9?ey/std::fabs(sn):1e300);b.bx=std::clamp(cx+t*c,x0,x1);b.by=std::clamp(cy+t*sn,y0,y1);out.balloons.push_back(b);}
    for(int iteration=0;iteration<60;++iteration){bool moved=false;
        for(size_t i=0;i<out.balloons.size();++i)for(size_t j=i+1;j<out.balloons.size();++j){auto& a=out.balloons[i];auto& b=out.balloons[j];double dx=b.bx-a.bx,dy=b.by-a.by,d=std::hypot(dx,dy);if(d>=2.25*r)continue;
            if(d<1e-6){dx=0;dy=1;d=1;}const double push=(2.25*r-d)*.5+.5;a.bx=std::clamp(a.bx-dx/d*push,x0,x1);a.by=std::clamp(a.by-dy/d*push,y0,y1);b.bx=std::clamp(b.bx+dx/d*push,x0,x1);b.by=std::clamp(b.by+dy/d*push,y0,y1);moved=true;}
        if(!moved)break;}
    return out;
}
void drawVisualizeOverlay(Image& img,const VisualizeOverlayLayout& layout,bool darkBackground){
    const int s=layout.scale;const double ink[3]={.10,.12,.14},leader[3]={darkBackground?.86:.12,darkBackground?.89:.14,darkBackground?.93:.17},paper[3]={1,1,1},header[3]={.88,.90,.92};const u8 text[3]={26,30,35};
    for(const auto& b:layout.balloons){const double dx=b.ax-b.bx,dy=b.ay-b.by,d=std::hypot(dx,dy);if(d>b.radius){line(img,b.bx+dx/d*b.radius,b.by+dy/d*b.radius,b.ax,b.ay,std::max(1.,.8*s),leader);disk(img,b.ax,b.ay,1.6*s,leader,leader,0);}}
    for(const auto& b:layout.balloons){disk(img,b.bx,b.by,b.radius,paper,darkBackground?leader:ink,std::max(1.,.9*s));const std::string label=std::to_string(b.item);drawVisualizeText(img,label,(int)std::lround(b.bx-visualizeTextWidth(label,s)/2.),(int)std::lround(b.by-3.5*s),s,text);}
    if(layout.rows.empty())return;
    fillRect(img,layout.tableX,layout.tableY,layout.tableW,layout.tableH,paper);fillRect(img,layout.tableX,layout.tableY,layout.tableW,layout.rowH,header);
    const double w=std::max(1.,.8*s);double y=layout.tableY;
    for(size_t row=0;row<layout.rows.size();++row){double x=layout.tableX;for(size_t c=0;c<3;++c){const auto& cell=layout.rows[row][c];const int tw=visualizeTextWidth(cell,s);const double tx=c==1?x+3*s+s:x+(layout.columns[c]-tw)/2;drawVisualizeText(img,cell,(int)std::lround(tx),(int)std::lround(y+3*s),s,text);x+=layout.columns[c];}
        y+=layout.rowH;line(img,layout.tableX,y,layout.tableX+layout.tableW,y,row==0?w*1.6:w,ink);}
    line(img,layout.tableX,layout.tableY,layout.tableX+layout.tableW,layout.tableY,w*1.6,ink);double x=layout.tableX;
    for(size_t c=0;c<=3;++c){line(img,x,layout.tableY,x,layout.tableY+layout.tableH,c==0||c==3?w*1.6:w,ink);if(c<3)x+=layout.columns[c];}
}
arkjson::Value visualizeRender(Document& d,u64 revision,const V& args,Image& image){
    need(args.t==V::Obj,"Arguments must be an object");
    for(const auto& field:args.obj){static const std::set<std::string> allowed{"expected_revision","exploded_view","explode_factor","motion","motion_time_s","view","camera","projection","lighting","background","resolution","samples","balloons","bom_table","title","media_time_s"};need(allowed.count(field.first)!=0,"VISUALIZE_INVALID: unknown visualize_render field: "+field.first);}
    if(const auto* expected=args.find("expected_revision"))need(expected->t==V::Num&&expected->num==(double)revision,"Revision conflict; the current revision is in this result. Inspect only what your edit depends on and retry");
    auto text=[&](const char* key,const std::string& fallback,std::initializer_list<const char*> choices){const auto* p=args.find(key);if(!p)return fallback;need(p->t==V::Str,std::string("VISUALIZE_INVALID: ")+key+" must be text");for(auto c:choices)if(p->str==c)return p->str;throw std::runtime_error(std::string("VISUALIZE_INVALID: unsupported ")+key+" "+p->str);};
    auto flag=[&](const char* key){const auto* p=args.find(key);if(!p)return false;need(p->t==V::Bool,std::string("VISUALIZE_INVALID: ")+key+" must be boolean");return p->b;};
    const std::string lighting=text("lighting","studio",{"studio","soft","dramatic","technical"}),background=text("background","light",{"light","dark","white"}),projection=text("projection","perspective",{"perspective","orthographic"});
    const std::string resolution=text("resolution","1024x640",{kResolutions[0],kResolutions[1],kResolutions[2],kResolutions[3],kResolutions[4],kResolutions[5]});const int W=std::stoi(resolution),H=std::stoi(resolution.substr(resolution.find('x')+1));
    int samples=4;if(const auto* p=args.find("samples")){need(p->t==V::Num&&(p->num==1||p->num==4||p->num==9||p->num==16),"VISUALIZE_INVALID: samples must be 1, 4, 9 or 16 per pixel");samples=(int)p->num;}
    const bool balloons=flag("balloons"),table=flag("bom_table");std::string title;if(const auto* p=args.find("title")){need(p->t==V::Str&&p->str.size()<=96,"VISUALIZE_INVALID: title must be text of at most 96 characters");title=p->str;}
    SnapshotAppearanceOptions044B appearance;V appearanceEvidence=V::makeObj();appearance.receipt=&appearanceEvidence;if(const auto* p=args.find("media_time_s")){need(p->t==V::Num&&std::isfinite(p->num)&&p->num>=0&&p->num<=86400,"VISUALIZE_INVALID: media_time_s must be 0..86400 seconds");appearance.mediaTimeS=p->num;}
    // Occurrence poses: solved placements, then an optional baked frame, then exploded display offsets.
    std::vector<AssemblyPlacement> placements;std::string error;
    if(!d.assembly.empty())need(resolveAssembly(d,placements,error),"VISUALIZE_INVALID: assembly solve failed: "+error);
    else{const auto consumed=consumedFeatureInputs(d);for(const auto& b:d.evalBodies)if(!consumed.count(b.id)){const auto* f=d.find(b.id);if(f&&!isCurveFeature(f->type))placements.push_back({0,b.id,f->name,Mat4{},true,0});}}
    V evidence=V::makeObj(),explodeEvidence,motionEvidence;std::vector<std::pair<std::string,double>> factors;
    if(const auto* motionArg=args.find("motion")){
        need(motionArg->t==V::Str,"VISUALIZE_INVALID: motion must name a baked motion");const auto* motion=findMotion(d,motionArg->str);need(motion,"MOTION_NOT_FOUND: "+motionArg->str);
        double time=0;if(const auto* p=args.find("motion_time_s")){need(p->t==V::Num&&std::isfinite(p->num)&&p->num>=0&&p->num<=600,"VISUALIZE_INVALID: motion_time_s must be 0..600");time=p->num;}
        BakedMotionFrames frames;need(decodeBakedMotion(*motion,frames,error),error);AssemblyManipulationProgram program;need(buildAssemblyManipulation(d,program,error),"VISUALIZE_MOTION_STALE: assembly solve failed: "+error);
        const auto* bake=motion->find("bake");std::string reasons;if(bake->find("source_digest")->str!=motionSourceDigest(d,program.baseline))reasons+="assembly_changed";if(bake->find("graph_digest")->str!=motionGraphDigest(*motion->find("graph")))reasons+=reasons.empty()?"graph_changed":", graph_changed";
        need(reasons.empty(),"VISUALIZE_MOTION_STALE: baked motion "+motionArg->str+" is stale ("+reasons+"); run motion.bake again before rendering it");
        const size_t k=std::min(frames.frameCount-1,(size_t)std::floor(std::min(time,frames.duration)*frames.fps+1e-6));
        for(auto& p:placements)for(size_t i=0;i<frames.nodes.size();++i)if(frames.nodes[i]==p.node&&frames.copies[i]==p.copy){p.world=frames.poses[k*frames.nodes.size()+i];break;}
        motionEvidence=V::makeObj();V channels=V::makeArr();motionEvidence.set("name",*motionArg);motionEvidence.set("time_s",V::makeNum(time));motionEvidence.set("frame",V::makeNum((double)k));motionEvidence.set("frame_time_s",V::makeNum(std::min(frames.duration,(double)k/frames.fps)));motionEvidence.set("fps",V::makeNum(frames.fps));motionEvidence.set("pose",V::makeStr("nearest earlier baked frame; float32 display pose"));
        for(size_t c=0;c<frames.explodeViews.size();++c)if(!frames.explodeViews[c].empty()){const double f=std::clamp((double)frames.coordinates[k*frames.channels.size()+c],0.,1.);factors.push_back({frames.explodeViews[c],f});V row=V::makeObj();row.set("channel",V::makeStr(frames.channels[c]));row.set("explode_view",V::makeStr(frames.explodeViews[c]));row.set("factor",V::makeNum(f));channels.arr.push_back(row);}
        motionEvidence.set("explode_channels",channels);
    }else need(!args.find("motion_time_s"),"VISUALIZE_INVALID: motion_time_s requires motion");
    if(const auto* viewArg=args.find("exploded_view")){need(viewArg->t==V::Str,"VISUALIZE_INVALID: exploded_view must name an exploded view");double f=1;if(const auto* p=args.find("explode_factor")){need(p->t==V::Num&&std::isfinite(p->num)&&p->num>=0&&p->num<=1,"VISUALIZE_INVALID: explode_factor must be within [0,1]");f=p->num;}
        for(const auto& existing:factors)need(existing.first!=viewArg->str,"VISUALIZE_EXPLODE_CONFLICT: motion "+motionEvidence.find("name")->str+" already drives exploded view "+viewArg->str+"; omit exploded_view or render without motion");factors.push_back({viewArg->str,f});}
    else need(!args.find("explode_factor"),"VISUALIZE_INVALID: explode_factor requires exploded_view");
    if(!factors.empty()){explodeEvidence=V::makeArr();std::vector<u64> ids;for(const auto& p:placements)ids.push_back(p.node);
        for(const auto& [name,f]:factors){const auto* view=findExplodedView(d,name);need(view,"EXPLODE_NOT_FOUND: "+name);ExplodedViewPlan plan;need(compileExplodedView(d,*view,plan,error),error);const auto offsets=explodedOffsets(d,plan,ids,f);size_t moved=0;
            for(size_t i=0;i<placements.size();++i)if(offsets[i].lensq()>0){placements[i].world=Mat4::translation(offsets[i])*placements[i].world;++moved;}
            V row=V::makeObj();row.set("view",V::makeStr(name));row.set("factor",V::makeNum(f));row.set("moved_occurrences",V::makeNum((double)moved));if(!plan.missing.empty())row.set("missing_instances",V::makeNum((double)plan.missing.size()));explodeEvidence.arr.push_back(row);}}
    // Display triangles with per-triangle authored appearance (044B textures, patterns, stamps).
    std::vector<RenderTri> tris;std::vector<SnapshotMaterial> materials;std::unordered_map<u64,Mesh> meshes;std::unordered_map<u64,std::vector<SnapshotMaterial>> styles;AABB bounds;
    struct Owner {u64 node=0,feature=0;int copy=0;std::string name;size_t pixels=0;double sx=0,sy=0,best=1e300;int ax=-1,ay=-1;};std::vector<Owner> owners;
    for(const auto& p:placements){if(!p.visible)continue;const Body* body=nullptr;for(const auto& b:d.evalBodies)if(b.id==p.feature){body=&b;break;}if(!body)continue;
        auto mesh=meshes.find(p.feature);if(mesh==meshes.end())mesh=meshes.emplace(p.feature,body->tessellate(d.tessOpt)).first;const Mesh& m=mesh->second;if(!m.triCount())continue;
        auto style=styles.find(p.feature);if(style==styles.end())style=styles.emplace(p.feature,snapshotBodyMaterials(d,*body,m,&appearance)).first;
        need(tris.size()+m.triCount()<=2000000,"VISUALIZE_CAPACITY: more than 2000000 display triangles; hide occurrences or render a smaller selection");
        const size_t first=materials.size();materials.insert(materials.end(),style->second.begin(),style->second.end());placeSnapshotMaterials044B(materials,first,p.world);
        const auto* feature=d.find(p.feature);const Appearance look=feature?resolvedAppearance(d,*feature):Appearance{};const bool normals=m.nrm.size()==m.pos.size();
        owners.push_back({p.node,p.feature,p.copy,p.name});
        for(size_t t=0;t<m.triCount();++t){RenderTri tri;for(int k=0;k<3;++k){const size_t v=(size_t)m.idx[t*3+k]*3;tri.p[k]=p.world.transformPoint({m.pos[v],m.pos[v+1],m.pos[v+2]});if(normals)tri.n[k]=p.world.transformDir({m.nrm[v],m.nrm[v+1],m.nrm[v+2]}).normalized();bounds.grow(tri.p[k]);}
            tri.face=(tri.p[1]-tri.p[0]).cross(tri.p[2]-tri.p[0]);if(tri.face.lensq()<1e-24)continue;tri.face=tri.face.normalized();
            // Smooth normals averaged across a sharp CAD edge (box corners) fall back to the face normal per vertex.
            for(auto& n:tri.n)if(!normals||n.lensq()<.5||std::fabs(n.dot(tri.face))<.866)n=tri.face;else if(n.dot(tri.face)<0)n=-n;
            tri.owner=(int)owners.size()-1;tri.material=first+t;tri.metallic=(float)look.metallic;tri.roughness=(float)std::clamp(look.roughness,.05,1.);tris.push_back(tri);}}
    need(!tris.empty()&&bounds.valid(),"VISUALIZE_EMPTY: no visible display triangles to render");
    // BOM numbering and the table reserve their space before the camera fits the model.
    std::vector<VisualizeBomItem> items;std::unordered_map<u64,int> itemOf;if(balloons||table){items=visualizeBomItems(d,revision);for(const auto& item:items)itemOf[item.feature]=item.item;}
    const double noSilhouette[4]={0,0,0,0};const auto reserve=layoutVisualizeOverlay(W,H,items,{},noSilhouette,false,table);const int s=reserve.scale;
    const double regionX1=table&&!reserve.rows.empty()?reserve.tableX-10.*s:W,regionY0=title.empty()?0:14.*s,regionY1=H-11.*s;
    // Camera: snapshot presets (Z up) or a custom direction/up; auto-fit unless distance_mm is given.
    Vec3 direction{1,-1,.8},upHint{0,0,1},target=bounds.center();double fov=30;std::string view="iso";bool fixedDistance=false;double distance=0;
    if(const auto* p=args.find("view")){need(p->t==V::Str,"VISUALIZE_INVALID: view must be iso, front, right, top or custom");view=p->str;}
    if(const auto* custom=args.find("camera")){need(view=="custom"||!args.find("view"),"VISUALIZE_INVALID: camera requires view custom or an omitted view");view="custom";need(custom->t==V::Obj,"VISUALIZE_INVALID: camera must be an object");
        for(const auto& field:custom->obj)need(field.first=="direction"||field.first=="up"||field.first=="target_mm"||field.first=="distance_mm"||field.first=="fov_deg","VISUALIZE_INVALID: unknown camera field: "+field.first);
        need(custom->find("direction")&&custom->find("up"),"VISUALIZE_INVALID: camera requires direction and up");direction=readVector(*custom->find("direction"),"direction");upHint=readVector(*custom->find("up"),"up");
        if(const auto* t=custom->find("target_mm"))target=readVector(*t,"target_mm");if(const auto* dist=custom->find("distance_mm")){need(dist->t==V::Num&&std::isfinite(dist->num)&&dist->num>0&&dist->num<=1e9,"VISUALIZE_INVALID: camera distance_mm must be positive and finite");distance=dist->num;fixedDistance=true;}
        if(const auto* f=custom->find("fov_deg")){need(f->t==V::Num&&std::isfinite(f->num)&&f->num>=5&&f->num<=120,"VISUALIZE_INVALID: camera fov_deg must be within 5..120");fov=f->num;}}
    else if(view=="front"){direction={0,-1,0};upHint={0,0,1};}else if(view=="right"){direction={1,0,0};upHint={0,0,1};}else if(view=="top"){direction={0,0,1};upHint={0,1,0};}
    else need(view=="iso","VISUALIZE_INVALID: view must be iso, front, right, top or custom");
    need(direction.lensq()>1e-20&&upHint.lensq()>1e-20,"VISUALIZE_INVALID: camera direction and up must be nonzero");direction=direction.normalized();need(direction.cross(upHint.normalized()).lensq()>1e-10,"VISUALIZE_INVALID: camera direction and up cannot be parallel");
    const Vec3 right=upHint.cross(direction).normalized(),up=direction.cross(right),forward=-direction;const bool ortho=projection=="orthographic";
    const double cx=regionX1/2,cy=(regionY0+regionY1)/2,margin=balloons?.70:.90,halfW=regionX1/2*margin,halfH=(regionY1-regionY0)/2*margin,fy=(H/2.)/std::tan(fov*K_PI/360);double orthoScale=1;
    {double need0=0,maxX=1e-9,maxY=1e-9,nearest=1e300;for(int corner=0;corner<8;++corner){const Vec3 c{corner&1?bounds.mx.x:bounds.mn.x,corner&2?bounds.mx.y:bounds.mn.y,corner&4?bounds.mx.z:bounds.mn.z};const Vec3 rel=c-target;const double x=rel.dot(right),y=rel.dot(up),z=rel.dot(forward);
            need0=std::max({need0,fy*std::fabs(x)/halfW-z,fy*std::fabs(y)/halfH-z});maxX=std::max(maxX,std::fabs(x));maxY=std::max(maxY,std::fabs(y));nearest=std::min(nearest,z);}
        if(!fixedDistance)distance=std::max(need0,-nearest+bounds.diagonal()*.05+1e-3);if(ortho){orthoScale=std::min(halfW/maxX,halfH/maxY);if(!fixedDistance)distance=bounds.diagonal()*2+1;}}
    const Vec3 eye=target+direction*distance;const double nearPlane=ortho?-1e300:std::max(1e-6,distance*1e-4);
    auto project=[&](Vec3 p,double& x,double& y,double& z){const Vec3 rel=p-eye;z=rel.dot(forward);if(!ortho&&z<=nearPlane)return false;if(ortho){x=cx+orthoScale*rel.dot(right);y=cy-orthoScale*rel.dot(up);}else{x=cx+fy*rel.dot(right)/z;y=cy-fy*rel.dot(up)/z;}return true;};
    // Progressive supersampling: jittered passes over one deferred G-buffer, averaged in display space.
    const Look look=lookPreset(lighting,right,up,direction);const Vec3 bgTop=background=="dark"?Vec3{.115,.140,.155}:background=="white"?Vec3{1,1,1}:Vec3{.93,.94,.96},bgBottom=background=="dark"?Vec3{.070,.086,.096}:background=="white"?Vec3{1,1,1}:Vec3{.80,.83,.87};
    const size_t pixels=(size_t)W*H;std::vector<int> triAt(pixels),ownerAt(pixels,-1);std::vector<float> depth(pixels),w0(pixels),w1(pixels);std::vector<double> accum(pixels*3,0.);std::vector<Vec3> color(pixels);
    std::vector<size_t> opaque,translucent;for(size_t t=0;t<tris.size();++t)(materials[tris[t].material].opacity>=1?opaque:translucent).push_back(t);
    auto raster=[&](size_t t,double jx,double jy,auto&& write){const auto& tri=tris[t];double sx[3],sy[3],sz[3];for(int k=0;k<3;++k)if(!project(tri.p[k],sx[k],sy[k],sz[k]))return;
        const double area=(sx[1]-sx[0])*(sy[2]-sy[0])-(sx[2]-sx[0])*(sy[1]-sy[0]);if(std::fabs(area)<1e-12)return;
        const int minX=std::max(0,(int)std::floor(std::min({sx[0],sx[1],sx[2]})-1)),maxX=std::min(W-1,(int)std::ceil(std::max({sx[0],sx[1],sx[2]})+1)),minY=std::max(0,(int)std::floor(std::min({sy[0],sy[1],sy[2]})-1)),maxY=std::min(H-1,(int)std::ceil(std::max({sy[0],sy[1],sy[2]})+1));
        for(int y=minY;y<=maxY;++y)for(int x=minX;x<=maxX;++x){const double px=x+.5+jx,py=y+.5+jy,e0=((sx[1]-px)*(sy[2]-py)-(sx[2]-px)*(sy[1]-py))/area,e1=((sx[2]-px)*(sy[0]-py)-(sx[0]-px)*(sy[2]-py))/area,e2=1-e0-e1;if(e0<-1e-9||e1<-1e-9||e2<-1e-9)continue;
            double z,a,b;if(ortho){z=e0*sz[0]+e1*sz[1]+e2*sz[2];a=e0;b=e1;}else{const double iz=e0/sz[0]+e1/sz[1]+e2/sz[2];z=1/iz;a=e0/sz[0]*z;b=e1/sz[1]*z;}write(x,y,z,a,b);}};
    auto shade=[&](size_t t,double a,double b,Vec3& out)->double{const auto& tri=tris[t];const auto& m=materials[tri.material];const double c=1-a-b;const Vec3 P=tri.p[0]*a+tri.p[1]*b+tri.p[2]*c;Vec3 N=(tri.n[0]*a+tri.n[1]*b+tri.n[2]*c).normalized();
        const Vec3 Vd=ortho?direction:(eye-P).normalized();Vec3 face=tri.face;if(face.dot(Vd)<0)face=-face;if(N.lensq()<.5)N=face;if(N.dot(face)<0)N=-N;if(N.dot(Vd)<0)N=(N-Vd*(2*N.dot(Vd))).normalized();
        const Vec3 srgb=m.shader?m.shader->shade(P,N,m.color):m.color;const Vec3 base{srgbToLinear(srgb.x),srgbToLinear(srgb.y),srgbToLinear(srgb.z)};const double metal=tri.metallic,rough=tri.roughness;
        const Vec3 f0=Vec3{.04,.04,.04}*(1-metal)+base*metal;const double shin=std::clamp(2/(rough*rough*rough*rough+1e-4)-2,2.,2048.);const double hemi=N.z*.5+.5;
        Vec3 lit=Vec3{base.x*(.55+.45*hemi),base.y*(.53+.47*hemi),base.z*(.50+.50*hemi)}*(look.ambient*(1-.6*metal));
        for(int k=0;k<3;++k){if(look.power[k]<=0)continue;const Vec3 L=look.dir[k];const double ndl=std::max(N.dot(L),0.);if(ndl<=0)continue;const Vec3 Hh=(L+Vd).normalized();const double spec=std::pow(std::max(N.dot(Hh),0.),shin)*(shin+8)/(8*K_PI)*.25;
            const Vec3 term=base*((1-metal)*ndl)+f0*(spec*ndl);lit+=Vec3{term.x*look.tint[k].x,term.y*look.tint[k].y,term.z*look.tint[k].z}*look.power[k];}
        lit+=Vec3{1,1,1}*(std::pow(1-std::max(N.dot(Vd),0.),3)*look.rim);lit+=m.emission*std::max(m.emissionIntensity,0.);
        out={linearToSrgb(lit.x),linearToSrgb(lit.y),linearToSrgb(lit.z)};return std::clamp(m.opacity,0.,1.);};
    const int grid=(int)std::lround(std::sqrt((double)samples));const double outline[3]={.07,.08,.09};
    for(int pass=0;pass<samples;++pass){const double jx=grid>1?((pass%grid)+.5)/grid-.5:0,jy=grid>1?((pass/grid)+.5)/grid-.5:0;
        std::fill(triAt.begin(),triAt.end(),-1);std::fill(depth.begin(),depth.end(),std::numeric_limits<float>::infinity());
        for(size_t t:opaque)raster(t,jx,jy,[&](int x,int y,double z,double a,double b){const size_t i=(size_t)y*W+x;if(z<depth[i]){depth[i]=(float)z;triAt[i]=(int)t;w0[i]=(float)a;w1[i]=(float)b;}});
        for(int y=0;y<H;++y){const double v=(double)y/std::max(H-1,1);const Vec3 bg=bgTop*(1-v)+bgBottom*v;for(int x=0;x<W;++x){const size_t i=(size_t)y*W+x;if(triAt[i]<0)color[i]=bg;else shade((size_t)triAt[i],w0[i],w1[i],color[i]);}}
        if(look.outline>0)for(int y=0;y<H;++y)for(int x=0;x<W;++x){const size_t i=(size_t)y*W+x;const int a=triAt[i];
            for(const size_t j:{x+1<W?i+1:i,y+1<H?i+W:i}){if(j==i)continue;const int b=triAt[j];if(a<0&&b<0)continue;const int oa=a<0?-1:tris[(size_t)a].owner,ob=b<0?-1:tris[(size_t)b].owner;bool edge=oa!=ob;
                if(!edge&&a>=0&&b>=0)edge=std::fabs(depth[i]-depth[j])>.02*std::max(depth[i],depth[j])||(look.creases&&a!=b&&tris[(size_t)a].face.dot(tris[(size_t)b].face)<std::cos(35*K_PI/180));
                if(!edge)continue;const size_t k=a<0?j:b<0?i:depth[i]<=depth[j]?i:j;const double alpha=oa!=ob?look.outline:look.outline*.7;color[k]=color[k]*(1-alpha)+Vec3{outline[0],outline[1],outline[2]}*alpha;}}
        if(!translucent.empty()){std::vector<std::pair<double,size_t>> order;for(size_t t:translucent){const Vec3 c=(tris[t].p[0]+tris[t].p[1]+tris[t].p[2])/3.;order.push_back({(c-eye).dot(forward),t});}std::sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first>b.first;});
            for(const auto& [z,t]:order)raster(t,jx,jy,[&](int x,int y,double zz,double a,double b){const size_t i=(size_t)y*W+x;if(zz>=depth[i])return;Vec3 c;const double alpha=shade(t,a,b,c);color[i]=color[i]*(1-alpha)+c*alpha;});}
        if(pass==0)for(size_t i=0;i<pixels;++i)ownerAt[i]=triAt[i]<0?-1:tris[(size_t)triAt[i]].owner;
        for(size_t i=0;i<pixels;++i){accum[i*3]+=color[i].x;accum[i*3+1]+=color[i].y;accum[i*3+2]+=color[i].z;}}
    image.w=W;image.h=H;image.px.resize(pixels*3);for(size_t i=0;i<pixels*3;++i)image.px[i]=(u8)std::clamp(std::lround(accum[i]/samples*255),0l,255l);
    // Balloon anchors: the visible pixel of each occurrence nearest its visible centroid.
    double silhouette[4]={1e300,1e300,-1e300,-1e300};
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){const int o=ownerAt[(size_t)y*W+x];if(o<0)continue;auto& owner=owners[(size_t)o];++owner.pixels;owner.sx+=x+.5;owner.sy+=y+.5;silhouette[0]=std::min(silhouette[0],x+.5);silhouette[1]=std::min(silhouette[1],y+.5);silhouette[2]=std::max(silhouette[2],x+.5);silhouette[3]=std::max(silhouette[3],y+.5);}
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){const int o=ownerAt[(size_t)y*W+x];if(o<0)continue;auto& owner=owners[(size_t)o];const double dx=x+.5-owner.sx/owner.pixels,dy=y+.5-owner.sy/owner.pixels,dd=dx*dx+dy*dy;if(dd<owner.best){owner.best=dd;owner.ax=x;owner.ay=y;}}
    std::vector<VisualizeAnchor> anchors;V balloonRows=V::makeArr(),hidden=V::makeArr(),bomRows=V::makeArr();
    if(balloons)for(const auto& item:items){const Owner* chosen=nullptr;for(const auto& owner:owners)if(owner.feature==item.feature&&owner.pixels&&(!chosen||owner.pixels>chosen->pixels))chosen=&owner;
        if(!chosen){hidden.arr.push_back(V::makeNum(item.item));continue;}anchors.push_back({item.item,chosen->ax+.5,chosen->ay+.5});}
    const auto layout=layoutVisualizeOverlay(W,H,items,anchors,silhouette,balloons,table);drawVisualizeOverlay(image,layout,background=="dark");
    const bool dark=background=="dark";const u8 caption[3]={(u8)(dark?205:60),(u8)(dark?212:66),(u8)(dark?220:74)};const int footScale=std::max(1,s-1);
    drawVisualizeText(image,"ARK Visualize / display tessellation, not geometric authority",(int)(10.*s),H-(int)(10.*s)-8*footScale,footScale,caption);if(!title.empty())drawVisualizeText(image,title,(int)(10.*s),(int)(6.*s),s,caption);
    for(const auto& b:layout.balloons){V row=V::makeObj();row.set("item",V::makeNum(b.item));for(const auto& owner:owners)if(owner.pixels&&itemOf.count(owner.feature)&&itemOf[owner.feature]==b.item&&std::fabs(owner.ax+.5-b.ax)<1e-9&&std::fabs(owner.ay+.5-b.ay)<1e-9){row.set("node",V::makeNum((double)owner.node));row.set("copy",V::makeNum(owner.copy));row.set("name",V::makeStr(owner.name));break;}
        V anchor=V::makeArr(),at=V::makeArr();anchor.arr={V::makeNum(b.ax),V::makeNum(b.ay)};at.arr={V::makeNum(b.bx),V::makeNum(b.by)};row.set("anchor_px",anchor);row.set("balloon_px",at);balloonRows.arr.push_back(row);}
    for(const auto& item:items){V row=V::makeObj();row.set("item",V::makeNum(item.item));row.set("name",V::makeStr(item.name));if(!item.partNumber.empty())row.set("part_number",V::makeStr(item.partNumber));row.set("quantity",V::makeNum(item.quantity));row.set("feature",V::makeNum((double)item.feature));bomRows.arr.push_back(row);}
    evidence.set("revision",V::makeNum((double)revision));evidence.set("current_revision",V::makeNum((double)revision));evidence.set("view",V::makeStr(view));evidence.set("width",V::makeNum(W));evidence.set("height",V::makeNum(H));
    evidence.set("authority",V::makeStr("display_mesh"));evidence.set("capture_source",V::makeStr("software_visualize"));evidence.set("render_label",V::makeStr("Visualization of display tessellation; never geometric, clearance or manufacturing authority"));
    evidence.set("lighting",V::makeStr(lighting));evidence.set("background",V::makeStr(background));evidence.set("projection",V::makeStr(projection));evidence.set("samples_per_pixel",V::makeNum(samples));evidence.set("triangles",V::makeNum((double)tris.size()));evidence.set("occurrences",V::makeNum((double)owners.size()));
    V viewState=V::makeObj();viewState.set("target_mm",vec(target));viewState.set("direction_target_to_eye",vec(direction));viewState.set("up",vec(up));viewState.set("distance_mm",V::makeNum(distance));viewState.set("fov_deg",V::makeNum(fov));evidence.set("view_state",viewState);
    evidence.set("exploded",explodeEvidence.isNull()?V{}:explodeEvidence);evidence.set("motion",motionEvidence);evidence.set("appearance_surfaces",appearanceEvidence);evidence.set("media_time_s",V::makeNum(appearance.mediaTimeS));
    evidence.set("emission",V::makeStr("steady emission added without bloom"));evidence.set("balloons",balloonRows);if(!hidden.arr.empty())evidence.set("balloons_not_visible",hidden);if(balloons||table){evidence.set("bom",bomRows);evidence.set("bom_numbering",V::makeStr("item numbers follow bom_inspect item order"));}
    if(table&&layout.hiddenRows)evidence.set("bom_rows_not_shown",V::makeNum((double)layout.hiddenRows));if(!title.empty())evidence.set("title",V::makeStr(title));evidence.set("document_unchanged",V::makeBool(true));evidence.set("committed",V::makeBool(true));
    return evidence;
}
void appendExplodedViewTools(V& tools){
    auto add=[&](const char* name,const char* description,const char* schemaText){V tool=V::makeObj(),schema,annotations=V::makeObj();arkjson::parse(schemaText,schema);tool.set("name",V::makeStr(name));tool.set("description",V::makeStr(description));annotations.set("readOnlyHint",V::makeBool(true));tool.set("inputSchema",schema);tool.set("annotations",annotations);tools.arr.push_back(tool);};
    add("explode_info","Read-only exploded view inspection. No arguments lists views with step counts, missing instances and the motions that drive them. name returns resolved steps (unit world direction, evaluated distance_mm, slice and factor_range); name with factor returns each moved occurrence's display offset_mm. propose{axis?,scope?,gap_mm?,instances?,name?} suggests an uncommitted explode.set from display bounding boxes. Exploded positions are display transforms only; author with doc_apply explode.set/explode.rename/explode.remove (agent_onboard exploded_views).",
        R"({"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":128},"factor":{"type":"number","minimum":0,"maximum":1},"propose":{"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":128},"axis":{"type":"string","enum":["auto","+X","-X","+Y","-Y","+Z","-Z"]},"scope":{"type":"string","enum":["leaf","top"]},"gap_mm":{"type":"number","minimum":0,"maximum":100000},"instances":{"type":"array","minItems":1,"maxItems":256,"items":{"type":["integer","string"]}}},"additionalProperties":false},"expected_revision":{"type":"integer","minimum":0}},"additionalProperties":false})");
    add("visualize_render","Read-only software render of the assembly as a PNG image (inline). exploded_view with explode_factor (default 1) applies display offsets; motion with motion_time_s shows the nearest baked frame and any explode_view channel of that motion (a stale bake rejects). balloons:true attaches BOM item numbers to visible occurrences; bom_table:true draws item, name and quantity. lighting studio|soft|dramatic|technical, background light|dark|white, resolution preset, samples 1|4|9|16 per pixel, view presets or custom camera as in snapshot. A visualization of display tessellation: never geometric or manufacturing authority. Use only when the user asks for images or assembly drawings.",
        R"({"type":"object","properties":{"exploded_view":{"type":"string","minLength":1,"maxLength":128},"explode_factor":{"type":"number","minimum":0,"maximum":1},"motion":{"type":"string","minLength":1,"maxLength":128},"motion_time_s":{"type":"number","minimum":0,"maximum":600},"view":{"type":"string","enum":["iso","front","right","top","custom"]},"camera":{"type":"object","properties":{"direction":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"up":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"target_mm":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"distance_mm":{"type":"number","exclusiveMinimum":0},"fov_deg":{"type":"number","minimum":5,"maximum":120}},"required":["direction","up"],"additionalProperties":false},"projection":{"type":"string","enum":["perspective","orthographic"]},"lighting":{"type":"string","enum":["studio","soft","dramatic","technical"]},"background":{"type":"string","enum":["light","dark","white"]},"resolution":{"type":"string","enum":["640x400","1024x640","1280x720","1280x800","1600x900","1920x1080"]},"samples":{"type":"integer","enum":[1,4,9,16]},"balloons":{"type":"boolean"},"bom_table":{"type":"boolean"},"title":{"type":"string","maxLength":96},"media_time_s":{"type":"number","minimum":0,"maximum":86400},"expected_revision":{"type":"integer","minimum":0}},"additionalProperties":false})");
}
}
