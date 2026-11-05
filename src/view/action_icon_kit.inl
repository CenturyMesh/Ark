// Shared vocabulary for ARK's code-native action icons (ribbon, gallery, menus).
// One style for every command:
// - outline strokes only, no fills or colour; the renderer sets one stroke weight and
//   tints with the theme (neutral, orange when active);
// - y grows downward in a 0..1 box; keep content inside 0.10..0.90; the renderer fits
//   the stroke bounds into one optical box, so draw proportions, not padding;
// - at most ~14 strokes, no detail closer than 0.08, so the 22 px menu bitmap reads;
// - reuse the motifs below so related commands look related: cube() is a solid body,
//   plane() a sketch/datum plane, sheet() a bent sheet, curve() a free curve, and the
//   badges (pencil, plus, cross, check) mark edit/add/cancel/apply at the lower right.
namespace icon_kit {
inline void line(ActionIcon& i,float ax,float ay,float bx,float by){i.push_back({ax,ay,bx,by});}
inline void poly(ActionIcon& i,std::initializer_list<std::pair<float,float>> p){auto a=p.begin();for(auto b=a+1;b<p.end();a=b++)line(i,a->first,a->second,b->first,b->second);}
inline void closed(ActionIcon& i,std::initializer_list<std::pair<float,float>> p){poly(i,p);if(p.size()>2)line(i,(p.end()-1)->first,(p.end()-1)->second,p.begin()->first,p.begin()->second);}
inline void rect(ActionIcon& i,float x,float y,float w,float h){closed(i,{{x,y},{x+w,y},{x+w,y+h},{x,y+h}});}
// Angles in degrees: 0 points along +x, 90 points down (screen y).
inline void arc(ActionIcon& i,float cx,float cy,float rx,float ry,float a0,float a1,int segments=24){
    constexpr float k=3.14159265f/180;for(int s=0;s<segments;++s){float a=(a0+(a1-a0)*s/segments)*k,b=(a0+(a1-a0)*(s+1)/segments)*k;line(i,cx+rx*std::cos(a),cy+ry*std::sin(a),cx+rx*std::cos(b),cy+ry*std::sin(b));}
}
inline void circle(ActionIcon& i,float cx,float cy,float r){arc(i,cx,cy,r,r,0,360,28);}
inline void dot(ActionIcon& i,float cx,float cy,float r=.035f){arc(i,cx,cy,r,r,0,360,8);}
// Shaft from a to b with a two-stroke head at b.
inline void arrow(ActionIcon& i,float ax,float ay,float bx,float by,float head=.13f){
    line(i,ax,ay,bx,by);float dx=bx-ax,dy=by-ay,l=std::max(1e-4f,std::hypot(dx,dy));dx/=l;dy/=l;
    constexpr float c=.866f,s=.5f;line(i,bx,by,bx-head*(dx*c-dy*s),by-head*(dy*c+dx*s));line(i,bx,by,bx-head*(dx*c+dy*s),by-head*(dy*c-dx*s));
}
inline void dashed(ActionIcon& i,float ax,float ay,float bx,float by,int dashes=4){for(int d=0;d<dashes;++d){float t0=(float)d/dashes,t1=t0+.55f/dashes;line(i,ax+(bx-ax)*t0,ay+(by-ay)*t0,ax+(bx-ax)*t1,ay+(by-ay)*t1);}}
// Isometric solid: centre (cx,cy), width s; the body motif for parts and features.
inline void cube(ActionIcon& i,float cx,float cy,float s){
    float h=s*.5f,q=s*.25f;closed(i,{{cx,cy-h},{cx+h,cy-q},{cx+h,cy+q},{cx,cy+h},{cx-h,cy+q},{cx-h,cy-q}});
    poly(i,{{cx-h,cy-q},{cx,cy},{cx+h,cy-q}});line(i,cx,cy,cx,cy+h);
}
// Sketch or datum plane: parallelogram centred at (cx,cy).
inline void plane(ActionIcon& i,float cx,float cy,float w,float h){float k=h*.45f;closed(i,{{cx-w*.5f+k,cy-h*.5f},{cx+w*.5f+k,cy-h*.5f},{cx+w*.5f-k,cy+h*.5f},{cx-w*.5f-k,cy+h*.5f}});}
// Bent sheet seen in isometric: a flat leg and an upright flange sharing a bend line.
inline void sheet(ActionIcon& i,float x,float y,float w,float h){
    closed(i,{{x,y+h*.62f},{x+w*.55f,y+h*.38f},{x+w,y+h*.62f},{x+w*.45f,y+h}});
    closed(i,{{x+w*.55f,y+h*.38f},{x+w*.55f,y},{x+w,y+h*.24f},{x+w,y+h*.62f}});
}
// Free curve through four points (Catmull-Rom sampled).
using Point=std::pair<float,float>;
inline void curve(ActionIcon& i,Point a,Point b,Point c,Point d,int steps=6){
    auto cr=[](float p0,float p1,float p2,float p3,float t){float t2=t*t,t3=t2*t;return .5f*(2*p1+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t2+(-p0+3*p1-3*p2+p3)*t3);};
    const Point spans[3][4]={{a,a,b,c},{a,b,c,d},{b,c,d,d}};
    for(const auto& p:spans){Point prev=p[1];for(int s=1;s<=steps;++s){const float t=(float)s/steps;const Point q{cr(p[0].first,p[1].first,p[2].first,p[3].first,t),cr(p[0].second,p[1].second,p[2].second,p[3].second,t)};line(i,prev.first,prev.second,q.first,q.second);prev=q;}}
}
// Badges at the lower right; s is the badge size (about 0.26).
inline void pencil(ActionIcon& i,float x,float y,float s){closed(i,{{x,y+s},{x+s*.18f,y+s*.62f},{x+s*.82f,y},{x+s,y+s*.18f},{x+s*.38f,y+s*.82f}});line(i,x+s*.18f,y+s*.62f,x+s*.38f,y+s*.82f);}
inline void plus(ActionIcon& i,float cx,float cy,float s){line(i,cx-s*.5f,cy,cx+s*.5f,cy);line(i,cx,cy-s*.5f,cx,cy+s*.5f);}
inline void cross(ActionIcon& i,float cx,float cy,float s){line(i,cx-s*.5f,cy-s*.5f,cx+s*.5f,cy+s*.5f);line(i,cx+s*.5f,cy-s*.5f,cx-s*.5f,cy+s*.5f);}
inline void check(ActionIcon& i,float cx,float cy,float s){poly(i,{{cx-s*.5f,cy},{cx-s*.12f,cy+s*.38f},{cx+s*.5f,cy-s*.38f}});}
}
