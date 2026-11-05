// Line icons, batch A (see action_icon_kit.inl for the shared style and motifs).
// Each entry replaces a raster atlas cell or a placeholder glyph for one action id.
namespace icon_set_a {
using namespace icon_kit;
// Square vertex grip: the picked points of a sketch entity.
inline void grip(ActionIcon& i,float x,float y,float s=.11f){rect(i,x-s*.5f,y-s*.5f,s,s);}
// Small "#" grid: the precise, typed-coordinate variant of a sketch tool.
inline void hashMark(ActionIcon& i,float cx,float cy,float s){const float g=s*.2f,h=s*.5f;line(i,cx-g,cy-h,cx-g,cy+h);line(i,cx+g,cy-h,cx+g,cy+h);line(i,cx-h,cy-g,cx+h,cy-g);line(i,cx-h,cy+g,cx+h,cy+g);}
// One sine period from a to b displaced in screen y: the edge of a free-form surface.
inline Point wavePoint(float ax,float ay,float bx,float by,float amp,float t,float phase=0){return {ax+(bx-ax)*t,ay+(by-ay)*t+amp*std::sin(phase+t*6.2831853f)};}
inline void waveLine(ActionIcon& i,float ax,float ay,float bx,float by,float amp,float phase=0,int steps=16){Point p=wavePoint(ax,ay,bx,by,amp,0,phase);for(int s=1;s<=steps;++s){const Point q=wavePoint(ax,ay,bx,by,amp,(float)s/steps,phase);line(i,p.first,p.second,q.first,q.second);p=q;}}
// Free-form surface patch: wavy top and bottom edges, straight sides; the top is shifted by skew.
inline void surfacePatch(ActionIcon& i,float x0,float y0,float x1,float y1,float skew,float amp){waveLine(i,x0+skew,y0,x1+skew,y0,amp);waveLine(i,x0,y1,x1,y1,amp);line(i,x0+skew,y0,x0,y1);line(i,x1+skew,y0,x1,y1);}
inline void bez2(ActionIcon& i,Point a,Point c,Point b,int steps=12){Point p=a;for(int s=1;s<=steps;++s){const float t=(float)s/steps,u=1-t;const Point q{u*u*a.first+2*u*t*c.first+t*t*b.first,u*u*a.second+2*u*t*c.second+t*t*b.second};line(i,p.first,p.second,q.first,q.second);p=q;}}
inline void bez3(ActionIcon& i,Point a,Point b,Point c,Point d,int steps=18){Point p=a;for(int s=1;s<=steps;++s){const float t=(float)s/steps,u=1-t,k0=u*u*u,k1=3*u*u*t,k2=3*u*t*t,k3=t*t*t;const Point q{k0*a.first+k1*b.first+k2*c.first+k3*d.first,k0*a.second+k1*b.second+k2*c.second+k3*d.second};line(i,p.first,p.second,q.first,q.second);p=q;}}
// Two-stroke arrow head at tip, pointing along (dx,dy).
inline void arrowHead(ActionIcon& i,float tx,float ty,float dx,float dy,float head=.13f){const float l=std::max(1e-4f,std::hypot(dx,dy));dx/=l;dy/=l;constexpr float c=.866f,s=.5f;line(i,tx,ty,tx-head*(dx*c-dy*s),ty-head*(dy*c+dx*s));line(i,tx,ty,tx-head*(dx*c+dy*s),ty-head*(dy*c-dx*s));}
// Elliptic arc from a0 to a1 (degrees, y down) with a head at a1.
inline void arcArrow(ActionIcon& i,float cx,float cy,float rx,float ry,float a0,float a1,float head=.13f){
    arc(i,cx,cy,rx,ry,a0,a1);constexpr float k=3.14159265f/180;const float a=a1*k,d=a1>a0?1.f:-1.f;
    arrowHead(i,cx+rx*std::cos(a),cy+ry*std::sin(a),-rx*std::sin(a)*d,ry*std::cos(a)*d,head);
}
// Prism standing on a plane() footprint: top face centred at (cx,cy), thickness t below it.
inline void slab(ActionIcon& i,float cx,float cy,float w,float h,float t){
    plane(i,cx,cy,w,h);const float k=h*.45f,l=cx-w*.5f-k,r=cx+w*.5f-k,tr=cx+w*.5f+k,b=cy+h*.5f,top=cy-h*.5f;
    line(i,l,b,l,b+t);line(i,r,b,r,b+t);line(i,tr,top,tr,top+t);poly(i,{{l,b+t},{r,b+t},{tr,top+t}});
}
// Mouse pointer with its tip at (x,y).
inline void cursorArrow(ActionIcon& i,float x,float y,float s){closed(i,{{x,y},{x,y+s*.80f},{x+s*.22f,y+s*.62f},{x+s*.36f,y+s*.92f},{x+s*.50f,y+s*.85f},{x+s*.36f,y+s*.56f},{x+s*.62f,y+s*.56f}});}
// Open-end wrench in the box (x,y,s): jaw at the upper right, handle to the lower left.
inline void wrench(ActionIcon& i,float x,float y,float s){
    const float cx=x+s*.68f,cy=y+s*.32f,r=s*.22f,h=r*.40f,w=r*.30f,len=s*.70f;
    auto at=[&](float p,float q){return Point{cx+(p+q)*.7071f,cy+(q-p)*.7071f};}; // p toward the jaw, q across it
    auto seg=[&](Point a,Point b){line(i,a.first,a.second,b.first,b.second);};
    auto ring=[&](float t0,float t1){constexpr float k=3.14159265f/180;const int n=10;for(int s2=0;s2<n;++s2){const float a=(t0+(t1-t0)*s2/n)*k,b=(t0+(t1-t0)*(s2+1)/n)*k;seg(at(r*std::cos(a),r*std::sin(a)),at(r*std::cos(b),r*std::sin(b)));}};
    const float jaw=std::asin(h/r)*57.29578f,neck=std::asin(w/r)*57.29578f;ring(jaw,180-neck);ring(180+neck,360-jaw);
    const float jp=r*std::cos(jaw*.01745329f),np=-r*std::cos(neck*.01745329f);
    seg(at(jp,h),at(r*.05f,h));seg(at(jp,-h),at(r*.05f,-h));seg(at(r*.05f,h),at(r*.05f,-h));
    seg(at(np,w),at(-len,w));seg(at(np,-w),at(-len,-w));
    for(int s2=0;s2<6;++s2){const float a=(90+180.f*s2/6)*.01745329f,b=(90+180.f*(s2+1)/6)*.01745329f;seg(at(-len+w*std::cos(a),w*std::sin(a)),at(-len+w*std::cos(b),w*std::sin(b)));}
}
inline void warnSign(ActionIcon& i,float cx,float cy,float s){closed(i,{{cx,cy-s*.5f},{cx+s*.56f,cy+s*.46f},{cx-s*.56f,cy+s*.46f}});line(i,cx,cy-s*.16f,cx,cy+s*.14f);dot(i,cx,cy+s*.30f,.03f);}
// Segment between two circles of radius r (a bone between joints).
inline void bone(ActionIcon& i,Point a,Point b,float r){const float dx=b.first-a.first,dy=b.second-a.second,l=std::max(1e-4f,std::hypot(dx,dy));line(i,a.first+dx/l*r,a.second+dy/l*r,b.first-dx/l*r,b.second-dy/l*r);}
inline ActionIcon mirroredX(const ActionIcon& a){ActionIcon m;m.reserve(a.size());for(const auto& s:a)m.push_back({1-s.ax,s.ay,1-s.bx,s.by});return m;}
inline void append(ActionIcon& to,const ActionIcon& from){to.insert(to.end(),from.begin(),from.end());}
}
static void addActionIconsSetA(std::map<int,ActionIcon>& r){
    using namespace icon_kit;using namespace icon_set_a;
    // Document and history.
    {auto& x=r[16];closed(x,{{.14f,.14f},{.72f,.14f},{.86f,.28f},{.86f,.86f},{.14f,.86f}});poly(x,{{.30f,.14f},{.30f,.36f},{.64f,.36f},{.64f,.14f}});poly(x,{{.28f,.86f},{.28f,.56f},{.72f,.56f},{.72f,.86f}});}
    {auto& x=r[17];poly(x,{{.12f,.78f},{.12f,.20f},{.36f,.20f},{.44f,.28f},{.78f,.28f},{.78f,.44f}});closed(x,{{.12f,.78f},{.26f,.44f},{.90f,.44f},{.76f,.78f}});}
    {auto& x=r[18];arc(x,.56f,.54f,.28f,.28f,-90,90);line(x,.56f,.82f,.30f,.82f);arrow(x,.56f,.26f,.14f,.26f,.15f);}
    r[19]=mirroredX(r[18]);
    {auto& x=r[458];closed(x,{{.20f,.10f},{.60f,.10f},{.80f,.30f},{.80f,.90f},{.20f,.90f}});poly(x,{{.60f,.10f},{.60f,.30f},{.80f,.30f}});for(float y:{.48f,.62f})line(x,.32f,y,.68f,y);line(x,.32f,.76f,.56f,.76f);}
    {auto& x=r[81];poly(x,{{.50f,.22f},{.16f,.22f},{.16f,.84f},{.78f,.84f},{.78f,.50f}});arrow(x,.88f,.12f,.44f,.56f,.18f);} // into the box (Export points out)
    {auto& x=r[756];line(x,.20f,.12f,.20f,.76f);for(float y:{.20f,.48f,.76f}){line(x,.20f,y,.32f,y);rect(x,.32f,y-.08f,.16f,.16f);line(x,.58f,y,.86f,y);}} // feature tree
    {auto& x=r[493];rect(x,.12f,.40f,.50f,.48f);poly(x,{{.24f,.40f},{.24f,.28f},{.74f,.28f},{.74f,.76f},{.62f,.76f}});poly(x,{{.36f,.28f},{.36f,.16f},{.86f,.16f},{.86f,.64f},{.74f,.64f}});} // stacked versions
    {auto& x=r[580];line(x,.12f,.18f,.70f,.18f);line(x,.12f,.34f,.58f,.34f);line(x,.12f,.50f,.34f,.50f);warnSign(x,.64f,.66f,.48f);}
    // Design inputs.
    // Named parameters: "x = [value]" rows (HUD 48000 already uses sliders).
    auto assign=[](ActionIcon& x,float y,float right,float h){cross(x,.20f,y,.16f);line(x,.35f,y-.05f,.49f,y-.05f);line(x,.35f,y+.05f,.49f,y+.05f);rect(x,.59f,y-h*.5f,right-.59f,h);};
    {auto& x=r[67];assign(x,.30f,.88f,.20f);assign(x,.70f,.88f,.20f);}
    {auto& x=r[577];assign(x,.18f,.86f,.16f);assign(x,.44f,.86f,.16f);pencil(x,.62f,.66f,.25f);}
    {auto& x=r[710];poly(x,{{.24f,.26f},{.10f,.50f},{.24f,.74f}});poly(x,{{.76f,.26f},{.90f,.50f},{.76f,.74f}});const Point j[3]{{.36f,.68f},{.50f,.32f},{.64f,.64f}};for(const auto& p:j)circle(x,p.first,p.second,.07f);bone(x,j[0],j[1],.07f);bone(x,j[1],j[2],.07f);} // code brackets around a joint chain
    {auto& x=r[401];rect(x,.12f,.12f,.76f,.76f);line(x,.12f,.30f,.88f,.30f);for(float y:{.48f,.70f}){line(x,.22f,y,.34f,y);rect(x,.44f,y-.07f,.34f,.14f);}} // inspector rows
    {auto& x=r[480];rect(x,.14f,.14f,.26f,.26f);circle(x,.73f,.27f,.13f);closed(x,{{.27f,.60f},{.41f,.86f},{.13f,.86f}});plus(x,.73f,.73f,.28f);} // palette of tools
    {auto& x=r[456];rect(x,.12f,.40f,.76f,.46f);line(x,.12f,.56f,.88f,.56f);poly(x,{{.36f,.40f},{.40f,.24f},{.60f,.24f},{.64f,.40f}});rect(x,.44f,.50f,.12f,.12f);} // toolbox
    {auto& x=r[161];rect(x,.14f,.12f,.56f,.70f);for(float y:{.30f,.46f,.62f}){dot(x,.25f,y,.03f);line(x,.34f,y,.60f,y);}pencil(x,.58f,.56f,.32f);}
    // Sketch setup: the plane motif with add / draw / edit / finish marks.
    {auto& x=r[60];plane(x,.50f,.46f,.46f,.36f);plus(x,.78f,.78f,.24f);}
    {auto& x=r[570];plane(x,.46f,.70f,.50f,.30f);pencil(x,.48f,.18f,.40f);curve(x,{.20f,.78f},{.28f,.66f},{.38f,.72f},{.48f,.58f});}
    {auto& x=r[610];plane(x,.46f,.46f,.44f,.36f);curve(x,{.22f,.54f},{.34f,.40f},{.48f,.50f},{.62f,.36f});pencil(x,.62f,.60f,.30f);}
    {auto& x=r[611];plane(x,.46f,.46f,.44f,.36f);curve(x,{.22f,.54f},{.34f,.40f},{.48f,.50f},{.62f,.36f});check(x,.76f,.78f,.30f);}
    r[578]=r[611];
    {auto& x=r[71];plane(x,.50f,.72f,.56f,.26f);arrow(x,.50f,.10f,.50f,.70f,.15f);poly(x,{{.50f,.60f},{.60f,.60f},{.60f,.70f}});} // look along the plane normal
    {auto& x=r[70];arrow(x,.16f,.86f,.16f,.12f,.13f);arrow(x,.14f,.84f,.88f,.84f,.13f);grip(x,.60f,.40f,.12f);dashed(x,.53f,.40f,.16f,.40f,3);dashed(x,.60f,.47f,.60f,.84f,3);} // point with x/y coordinates
    {auto& x=r[73];poly(x,{{.14f,.30f},{.40f,.80f},{.88f,.56f}});poly(x,{{.335f,.676f},{.461f,.613f},{.525f,.737f}});grip(x,.14f,.30f,.12f);grip(x,.88f,.56f,.12f);}
    {auto& x=r[591];line(x,.14f,.40f,.14f,.86f);line(x,.86f,.40f,.86f,.86f);arrow(x,.50f,.70f,.15f,.70f,.12f);arrow(x,.50f,.70f,.85f,.70f,.12f);rect(x,.32f,.16f,.36f,.28f);}
    r[576]=r[591];r[659]=r[591];
    // Interactive sketch tools: shape plus vertex grips.
    {auto& x=r[571];line(x,.18f,.80f,.82f,.20f);grip(x,.18f,.80f,.12f);grip(x,.82f,.20f,.12f);}
    {auto& x=r[572];poly(x,{{.12f,.76f},{.36f,.28f},{.62f,.70f},{.88f,.26f}});for(auto p:{Point{.12f,.76f},Point{.36f,.28f},Point{.62f,.70f},Point{.88f,.26f}})grip(x,p.first,p.second,.12f);}
    {auto& x=r[573];rect(x,.16f,.26f,.68f,.48f);grip(x,.16f,.26f,.12f);grip(x,.84f,.74f,.12f);}
    {auto& x=r[574];circle(x,.48f,.52f,.32f);grip(x,.48f,.52f,.11f);line(x,.53f,.49f,.757f,.36f);grip(x,.757f,.36f,.11f);}
    {auto& x=r[575];arc(x,.50f,.66f,.38f,.38f,180,360);grip(x,.12f,.66f,.12f);grip(x,.69f,.33f,.12f);grip(x,.88f,.66f,.12f);}
    // Precise (typed) sketch tools: the same shapes with a "#" grid mark.
    {auto& x=r[543];line(x,.14f,.70f,.60f,.20f);grip(x,.14f,.70f);grip(x,.60f,.20f);hashMark(x,.79f,.78f,.26f);}
    {auto& x=r[544];arc(x,.40f,.58f,.28f,.28f,180,360);grip(x,.12f,.58f);grip(x,.54f,.3375f);grip(x,.68f,.58f);hashMark(x,.79f,.79f,.26f);}
    {auto& x=r[545];line(x,.12f,.62f,.32f,.62f);arc(x,.32f,.40f,.22f,.22f,90,-40);grip(x,.12f,.62f);grip(x,.49f,.26f);hashMark(x,.79f,.78f,.26f);}
    {auto& x=r[61];rect(x,.12f,.18f,.50f,.38f);grip(x,.12f,.18f);grip(x,.62f,.56f);hashMark(x,.79f,.79f,.26f);}
    {auto& x=r[62];circle(x,.38f,.40f,.26f);grip(x,.38f,.40f,.10f);line(x,.42f,.38f,.605f,.27f);grip(x,.605f,.27f,.10f);hashMark(x,.79f,.79f,.26f);}
    {auto& x=r[63];curve(x,{.12f,.62f},{.26f,.24f},{.46f,.54f},{.64f,.16f});for(auto p:{Point{.12f,.62f},Point{.26f,.24f},Point{.46f,.54f},Point{.64f,.16f}})grip(x,p.first,p.second,.10f);hashMark(x,.79f,.78f,.26f);}
    // 3D curves: the shape rises off a sketch plane.
    {auto& x=r[547];plane(x,.50f,.76f,.56f,.22f);arc(x,.50f,.76f,.28f,.50f,180,360);grip(x,.22f,.76f,.10f);grip(x,.64f,.33f,.10f);grip(x,.78f,.76f,.10f);}
    {auto& x=r[548];plane(x,.46f,.76f,.56f,.22f);line(x,.18f,.76f,.46f,.76f);arc(x,.46f,.30f,.34f,.46f,90,0);grip(x,.18f,.76f,.10f);grip(x,.80f,.30f,.10f);}
    {auto& x=r[538];plane(x,.46f,.76f,.56f,.22f);curve(x,{.20f,.76f},{.36f,.50f},{.54f,.58f},{.72f,.20f});dashed(x,.72f,.26f,.72f,.74f,3);grip(x,.20f,.76f,.10f);grip(x,.72f,.20f,.10f);}
    // References into a sketch: projected flat, or linked in 3D.
    {auto& x=r[536];cube(x,.50f,.24f,.36f);arrow(x,.50f,.46f,.50f,.68f,.10f);plane(x,.50f,.78f,.54f,.24f);closed(x,{{.34f,.78f},{.50f,.71f},{.66f,.78f},{.50f,.85f}});} // the body's outline dropped onto the sketch plane
    {auto& x=r[537];cube(x,.44f,.46f,.60f);grip(x,.44f,.76f,.12f);grip(x,.74f,.61f,.12f);} // a model edge becomes linked sketch geometry
    // Solid features.
    {auto& x=r[65];slab(x,.46f,.62f,.52f,.24f,.16f);arrow(x,.46f,.62f,.46f,.12f,.15f);}
    r[101]=r[65];
    {auto& x=r[66];slab(x,.46f,.62f,.52f,.24f,.16f);plane(x,.46f,.62f,.24f,.12f);arrow(x,.46f,.10f,.46f,.58f,.15f);}
    {auto& x=r[687];slab(x,.38f,.62f,.46f,.24f,.16f);arrow(x,.38f,.62f,.38f,.12f,.15f);cursorArrow(x,.66f,.14f,.30f);}
    {auto& x=r[104];poly(x,{{.48f,.16f},{.16f,.16f},{.16f,.86f},{.84f,.86f},{.84f,.52f}});arc(x,.48f,.52f,.36f,.36f,-90,0);dashed(x,.52f,.16f,.84f,.16f,2);dashed(x,.84f,.18f,.84f,.48f,2);}
    {auto& x=r[106];line(x,.10f,.86f,.90f,.86f);poly(x,{{.24f,.86f},{.52f,.20f},{.86f,.20f},{.86f,.86f}});dashed(x,.24f,.14f,.24f,.82f,4);arrowHead(x,.24f,.14f,0,-1,.10f);arc(x,.24f,.86f,.56f,.56f,-90,-67,8);} // drafted wall against the pull direction
    {auto& x=r[26];line(x,.12f,.24f,.48f,.24f);arc(x,.48f,.58f,.34f,.34f,-90,0);line(x,.82f,.58f,.82f,.86f);line(x,.12f,.48f,.48f,.48f);arc(x,.48f,.58f,.10f,.10f,-90,0);line(x,.58f,.58f,.58f,.86f);arc(x,.12f,.36f,.05f,.12f,0,360,16);arc(x,.70f,.86f,.12f,.05f,0,360,16);}
    {auto& x=r[750];constexpr int n=96;Point p{};for(int s=0;s<=n;++s){const float t=(float)s/n,a=t*3*6.2831853f;const Point q{.5f-.28f*std::cos(a),.18f+.62f*t+.08f*std::sin(a)};if(s)line(x,p.first,p.second,q.first,q.second);p=q;}}
    // Sheet metal: plate and bent-sheet motifs.
    {auto& x=r[120];slab(x,.50f,.48f,.46f,.36f,.10f);}
    {auto& x=r[121];sheet(x,.12f,.12f,.76f,.76f);}
    {auto& x=r[394];line(x,.10f,.74f,.62f,.74f);line(x,.10f,.65f,.62f,.65f);arc(x,.62f,.57f,.17f,.17f,90,-90,16);arc(x,.62f,.57f,.08f,.08f,90,-90,10);line(x,.62f,.40f,.30f,.40f);line(x,.62f,.49f,.30f,.49f);line(x,.10f,.65f,.10f,.74f);line(x,.30f,.40f,.30f,.49f);} // return hem: the sheet edge folded back on itself
    {auto& x=r[494];const Point p[4]{{.10f,.62f},{.34f,.52f},{.34f,.32f},{.58f,.22f}};const float dx=.26f,dy=.15f;for(int k=0;k<3;++k){line(x,p[k].first,p[k].second,p[k+1].first,p[k+1].second);line(x,p[k].first+dx,p[k].second+dy,p[k+1].first+dx,p[k+1].second+dy);}for(const auto& q:p)line(x,q.first,q.second,q.first+dx,q.second+dy);} // offset step
    {auto& x=r[495];plane(x,.44f,.64f,.52f,.30f);line(x,.615f,.445f,.265f,.835f);grip(x,.615f,.445f,.12f);grip(x,.265f,.835f,.12f);arcArrow(x,.70f,.58f,.20f,.20f,20,-85,.12f);} // bend the blank along a sketch line
    {auto& x=r[124];slab(x,.50f,.46f,.46f,.36f,.09f);arc(x,.50f,.46f,.13f,.07f,0,360,16);}
    {auto& x=r[397];slab(x,.50f,.52f,.46f,.36f,.09f);arc(x,.50f,.52f,.14f,.07f,0,180,10);arc(x,.50f,.52f,.14f,.14f,180,360,14);} // raised emboss
    {auto& x=r[468];slab(x,.50f,.48f,.46f,.36f,.09f);for(auto c:{Point{.50f,.40f},Point{.66f,.40f},Point{.36f,.56f},Point{.52f,.56f}})arc(x,c.first,c.second,.065f,.04f,0,360,12);}
    {auto& x=r[108];ActionIcon half;sheet(half,.06f,.24f,.34f,.52f);x=half;append(x,mirroredX(half));dashed(x,.5f,.12f,.5f,.88f,5);}
    {auto& x=r[320];plane(x,.50f,.66f,.52f,.28f);dashed(x,.626f,.52f,.374f,.80f,3);arcArrow(x,.60f,.50f,.26f,.26f,-100,-15,.12f);} // the flange lies flat
    {auto& x=r[321];sheet(x,.10f,.20f,.62f,.66f);arcArrow(x,.72f,.61f,.18f,.18f,25,-80,.12f);} // the flange folds back up
    // Surfaces.
    {auto& x=r[151];waveLine(x,.14f,.80f,.70f,.80f,.05f);waveLine(x,.14f,.42f,.70f,.42f,.05f);line(x,.14f,.42f,.14f,.80f);line(x,.70f,.42f,.70f,.80f);arrow(x,.42f,.42f,.42f,.10f,.14f);} // open profile pulled into a surface
    {auto& x=r[150];closed(x,{{.10f,.52f},{.24f,.44f},{.24f,.80f},{.10f,.88f}});curve(x,{.17f,.66f},{.40f,.50f},{.62f,.58f},{.86f,.24f});arrowHead(x,.86f,.24f,.24f,-.34f,.14f);} // profile along a path
    {auto& x=r[76];plane(x,.50f,.76f,.56f,.20f);arc(x,.50f,.24f,.20f,.08f,0,360,20);bez2(x,{.13f,.86f},{.26f,.50f},{.30f,.24f});bez2(x,{.87f,.66f},{.72f,.44f},{.70f,.24f});}
    r[49403]=r[76];
    {auto& x=r[49402];for(int k=0;k<4;++k){const float t=k/3.f;const Point a=wavePoint(.12f,.24f,.88f,.18f,.06f,t),b=wavePoint(.12f,.80f,.88f,.84f,.05f,t,3.14159f);line(x,a.first,a.second,b.first,b.second);}waveLine(x,.12f,.24f,.88f,.18f,.06f);waveLine(x,.12f,.80f,.88f,.84f,.05f,3.14159f);} // straight rulings between two rails
    {auto& x=r[78];waveLine(x,.12f,.30f,.80f,.18f,.05f);waveLine(x,.20f,.82f,.88f,.70f,.05f);waveLine(x,.16f,.56f,.84f,.44f,.05f);line(x,.12f,.30f,.20f,.82f);line(x,.80f,.18f,.88f,.70f);bez2(x,{.46f,.24f},{.56f,.50f},{.54f,.76f});}
    {auto& x=r[49404];const Point a{.14f,.30f},b{.78f,.16f},c{.88f,.72f},d{.24f,.86f};bez2(x,a,{.44f,.12f},b);bez2(x,b,{.92f,.42f},c);bez2(x,c,{.56f,.90f},d);bez2(x,d,{.10f,.58f},a);line(x,.24f,.58f,.40f,.32f);line(x,.34f,.76f,.64f,.30f);line(x,.56f,.78f,.80f,.44f);} // boundary loop filled
    {auto& x=r[49500];const Point p[4]{{.12f,.80f},{.28f,.22f},{.72f,.22f},{.88f,.80f}};bez3(x,p[0],p[1],p[2],p[3]);for(int k=0;k<3;++k)dashed(x,p[k].first,p[k].second,p[k+1].first,p[k+1].second,3);for(const auto& q:p)circle(x,q.first,q.second,.06f);} // control polygon over the curve
    {auto& x=r[49501];bez2(x,{.12f,.90f},{.50f,.42f},{.88f,.90f});dashed(x,.12f,.90f,.456f,.475f,3);dashed(x,.88f,.90f,.544f,.475f,3);circle(x,.50f,.42f,.07f);arrow(x,.50f,.34f,.50f,.10f,.13f);} // drag one pole by a delta
    {auto& x=r[49503];plane(x,.50f,.74f,.56f,.24f);circle(x,.50f,.26f,.13f);line(x,.50f,.39f,.50f,.72f);arc(x,.50f,.74f,.08f,.035f,0,360,10);} // pin glued at a surface hit point
    {auto& x=r[49505];arcArrow(x,.50f,.50f,.34f,.34f,230,-50,.15f);dot(x,.50f,.50f,.05f);}
    // Trims: the kept part is solid, the removed part dashed.
    {auto& x=r[49401];const float ox=.46f,oy=.54f;auto at=[&](float p,float q){return Point{ox+(p+q)*.7071f,oy+(q-p)*.7071f};}; // scissors over a surface, like Repair's wrench
        for(float side:{1.f,-1.f}){const Point loop=at(-.30f,.15f*side),tip=at(.40f,-.12f*side),o=at(0,0);circle(x,loop.first,loop.second,.10f);const float dx=o.first-loop.first,dy=o.second-loop.second,l=std::hypot(dx,dy);poly(x,{{loop.first+dx/l*.10f,loop.second+dy/l*.10f},o,tip});}
        surfacePatch(x,.58f,.62f,.88f,.86f,.04f,.025f);}
    {auto& x=r[492];closed(x,{{.70f,.095f},{.70f,.355f},{.296f,.805f},{.296f,.545f}});poly(x,{{.66f,.40f},{.442f,.40f},{.118f,.76f},{.336f,.76f}});dashed(x,.66f,.40f,.882f,.40f,2);dashed(x,.882f,.40f,.558f,.76f,3);dashed(x,.558f,.76f,.336f,.76f,2);}
    {auto& x=r[497];closed(x,{{.50f,.30f},{.66f,.14f},{.66f,.70f},{.50f,.86f}});line(x,.12f,.64f,.58f,.37f);line(x,.20f,.78f,.58f,.56f);arc(x,.16f,.71f,.05f,.08f,0,360,12);dashed(x,.62f,.34f,.90f,.18f,3);dashed(x,.62f,.54f,.90f,.38f,3);}
    {auto& x=r[49400];ActionIcon half;bez2(half,{.42f,.20f},{.26f,.14f},{.14f,.30f});bez2(half,{.14f,.30f},{.22f,.52f},{.14f,.74f});bez2(half,{.14f,.74f},{.26f,.88f},{.42f,.82f});line(half,.42f,.82f,.42f,.20f);x=half;append(x,mirroredX(half));dashed(x,.5f,.10f,.5f,.90f,5);}
    {auto& x=r[79];waveLine(x,.12f,.40f,.88f,.40f,.08f);waveLine(x,.12f,.62f,.88f,.62f,.08f);line(x,.12f,.40f,.12f,.62f);line(x,.88f,.40f,.88f,.62f);}
    {auto& x=r[830];wrench(x,.06f,.06f,.84f);surfacePatch(x,.56f,.62f,.88f,.86f,.04f,.025f);}
    {auto& x=r[49810];plane(x,.50f,.50f,.44f,.40f);line(x,.58f,.30f,.748f,.468f);line(x,.384f,.384f,.616f,.616f);line(x,.252f,.532f,.42f,.70f);} // hatched section plane
}
