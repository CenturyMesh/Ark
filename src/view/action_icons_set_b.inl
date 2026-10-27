// Line icons, batch B (see action_icon_kit.inl for the shared style and motifs).
// Each entry replaces a raster atlas cell or a placeholder glyph for one action id.
static void addActionIconsSetB(std::map<int,ActionIcon>& r){
    using namespace icon_kit;
    using Seg=std::pair<Point,Point>;using Outline=std::vector<Point>;
    constexpr float deg=3.14159265f/180;
    auto lerp=[](Point a,Point b,float t){return Point{a.first+(b.first-a.first)*t,a.second+(b.second-a.second)*t};};
    auto seg=[](ActionIcon& i,Point a,Point b){line(i,a.first,a.second,b.first,b.second);};
    // n strokes across the quad p0..p3, parallel to its p0-p3 side (a filled face).
    auto hatch=[&](ActionIcon& i,Point p0,Point p1,Point p2,Point p3,int n){for(int k=1;k<=n;++k){const float t=(float)k/(n+1);seg(i,lerp(p0,p1,t),lerp(p3,p2,t));}};
    // Isometric box from its back-top corner (x,y): a runs right-down, b left-down, c is the height.
    struct Box{std::vector<Seg> edges;Outline hull;};
    auto box=[](float x,float y,float a,float b,float c){
        const Point t{x,y},rt{x+a,y+a*.5f},f{x+a-b,y+(a+b)*.5f},l{x-b,y+b*.5f};auto down=[c](Point p){return Point{p.first,p.second+c};};
        Box o;o.hull={t,rt,down(rt),down(f),down(l),l};for(size_t k=0;k<6;++k)o.edges.push_back({o.hull[k],o.hull[(k+1)%6]});
        o.edges.push_back({l,f});o.edges.push_back({f,rt});o.edges.push_back({f,down(f)});return o;
    };
    // Hidden lines: keep the parts of a segment that do not pass behind the convex outline `front`.
    auto clipOut=[&](const Seg& e,const Outline& front,std::vector<Seg>& out){
        const Point a=e.first,b=e.second;float area=0;for(size_t k=0;k<front.size();++k){const auto& p=front[k];const auto& q=front[(k+1)%front.size()];area+=p.first*q.second-q.first*p.second;}
        const float s=area>0?1.f:-1.f,dx=b.first-a.first,dy=b.second-a.second;float t0=0,t1=1;
        for(size_t k=0;k<front.size()&&t0<t1;++k){const auto& p=front[k];const auto& q=front[(k+1)%front.size()];const float ex=q.first-p.first,ey=q.second-p.second;
            const float at=s*(ex*(a.second-p.second)-ey*(a.first-p.first)),slope=s*(ex*dy-ey*dx);
            if(std::abs(slope)<1e-6f){if(at<=1e-4f)t1=-1;continue;}const float t=-at/slope;if(slope>0)t0=std::max(t0,t);else t1=std::min(t1,t);}
        if(t0>=t1-1e-4f){out.push_back(e);return;}if(t0>1e-4f)out.push_back({a,lerp(a,b,t0)});if(t1<1-1e-4f)out.push_back({lerp(a,b,t1),b});
    };
    auto behind=[&](ActionIcon& i,Point a,Point b,std::initializer_list<const Outline*> fronts){
        std::vector<Seg> parts{{a,b}};for(const auto* f:fronts){std::vector<Seg> next;for(const auto& e:parts)clipOut(e,*f,next);parts.swap(next);}
        for(const auto& e:parts)seg(i,e.first,e.second);
    };
    auto draw=[&](ActionIcon& i,const Box& b,std::initializer_list<const Outline*> fronts){for(const auto& e:b.edges)behind(i,e.first,e.second,fronts);};
    // Page with a folded corner.
    auto page=[](ActionIcon& i,float x,float y,float w,float h,float k){poly(i,{{x+w-k,y},{x,y},{x,y+h},{x+w,y+h},{x+w,y+k},{x+w-k,y}});poly(i,{{x+w-k,y},{x+w-k,y+k},{x+w,y+k}});};
    // Spur gear outline: root radius r0, tip radius r1, n teeth, first tooth at `phase` degrees.
    auto gear=[&](ActionIcon& i,float cx,float cy,float r0,float r1,int n,float phase){
        const float rad[4]={r0,r1,r1,r0},off[4]={-.30f,-.16f,.16f,.30f};Outline p;
        for(int t=0;t<n;++t)for(int j=0;j<4;++j){const float a=(phase+(t+off[j])*360.f/n)*deg;p.push_back({cx+rad[j]*std::cos(a),cy+rad[j]*std::sin(a)});}
        for(size_t t=0;t<p.size();++t)seg(i,p[t],p[(t+1)%p.size()]);
    };
    // 45-degree hatch (rising to the right) clipped to an axis-aligned rectangle.
    auto hatchRect=[](ActionIcon& i,float x0,float y0,float x1,float y1,float step){
        for(float k=x0+y0+step*.5f;k<x1+y1;k+=step){const float a=std::max(x0,k-y1),b=std::min(x1,k-y0);if(b-a>.02f)line(i,a,k-a,b,k-b);}
    };
    // Cubic Bezier, optionally offset along its normal (cables, ribbons, smooth patches).
    auto bez=[](Point a,Point b,Point c,Point d,float t){const float u=1-t;return Point{u*u*u*a.first+3*u*u*t*b.first+3*u*t*t*c.first+t*t*t*d.first,u*u*u*a.second+3*u*u*t*b.second+3*u*t*t*c.second+t*t*t*d.second};};
    auto tangent=[&](Point a,Point b,Point c,Point d,float t){const Point p0=bez(a,b,c,d,std::max(0.f,t-.01f)),p1=bez(a,b,c,d,std::min(1.f,t+.01f));const float dx=p1.first-p0.first,dy=p1.second-p0.second,l=std::max(1e-5f,std::hypot(dx,dy));return Point{dx/l,dy/l};};
    auto along=[&](ActionIcon& i,Point a,Point b,Point c,Point d,float offset,int steps=16){
        Point prev{};for(int s=0;s<=steps;++s){const float t=(float)s/steps;const Point p=bez(a,b,c,d,t),n=tangent(a,b,c,d,t),q{p.first-n.second*offset,p.second+n.first*offset};if(s)seg(i,prev,q);prev=q;}
    };
    // Ellipse with radii along / across the direction u (a ring threaded on a path).
    auto ring=[&](ActionIcon& i,Point c,Point u,float ra,float rc){Point prev{};for(int s=0;s<=16;++s){const float a=s*22.5f*deg;const Point p{c.first+u.first*ra*std::cos(a)-u.second*rc*std::sin(a),c.second+u.second*ra*std::cos(a)+u.first*rc*std::sin(a)};if(s)seg(i,prev,p);prev=p;}};

    // Selection filters: one cube; hatch marks a face, a doubled line with end dots marks an edge or curve.
    const Point cT{.5f,.12f},cR{.88f,.31f},cBR{.88f,.69f},cB{.5f,.88f},cBL{.12f,.69f},cL{.12f,.31f},cC{.5f,.5f};
    {auto& x=r[170];cube(x,.5f,.5f,.60f);closed(x,{cT,cR,cBR,cB,cBL,cL});} // Body: whole outline doubled
    {auto& x=r[171];cube(x,.5f,.5f,.76f);hatch(x,cL,cC,cR,cT,3);}
    {auto& x=r[172];cube(x,.5f,.5f,.76f);line(x,.80f,.39f,.80f,.69f);dot(x,.88f,.31f,.045f);dot(x,.88f,.69f,.045f);}
    {auto& x=r[173];cube(x,.5f,.5f,.76f);curve(x,{.557f,.757f},{.652f,.538f},{.728f,.652f},{.823f,.434f});dot(x,.557f,.757f,.045f);dot(x,.823f,.434f,.045f);}

    // Assembly: a part stacked on another part.
    {auto& x=r[27];const Box top=box(.5f,.12f,.18f,.18f,.18f),base=box(.5f,.24f,.30f,.30f,.26f);draw(x,top,{});draw(x,base,{&top.hull});}
    // Mate: a pin aligned over its hole.
    {auto& x=r[94];closed(x,{{.12f,.70f},{.5f,.52f},{.88f,.70f},{.5f,.88f}});arc(x,.5f,.70f,.14f,.07f,0,360);
        arc(x,.5f,.16f,.14f,.07f,0,360);line(x,.36f,.16f,.36f,.40f);line(x,.64f,.16f,.64f,.40f);arc(x,.5f,.40f,.14f,.07f,0,180,12);dashed(x,.36f,.48f,.36f,.70f,2);dashed(x,.64f,.48f,.64f,.70f,2);}
    {auto& x=r[49700];gear(x,.38f,.60f,.21f,.28f,8,0);circle(x,.38f,.60f,.07f);gear(x,.67f,.31f,.13f,.19f,6,45);circle(x,.67f,.31f,.045f);} // Transmission
    {auto& x=r[210];closed(x,{{.12f,.88f},{.12f,.14f},{.88f,.14f},{.88f,.88f},{.74f,.88f},{.74f,.28f},{.26f,.28f},{.26f,.88f}});line(x,.26f,.48f,.46f,.28f);line(x,.74f,.48f,.54f,.28f);} // Frame with knee braces
    {auto& x=r[496];rect(x,.10f,.12f,.80f,.16f);closed(x,{{.30f,.54f},{.30f,.88f},{.54f,.88f},{.54f,.54f}});arrow(x,.42f,.50f,.42f,.30f,.12f);} // Trim / extend a member to a boundary
    {auto& x=r[498];rect(x,.10f,.64f,.80f,.18f);poly(x,{{.42f,.64f},{.42f,.14f},{.58f,.14f},{.58f,.64f}});line(x,.42f,.46f,.24f,.64f);line(x,.58f,.46f,.76f,.64f);line(x,.42f,.55f,.33f,.64f);line(x,.58f,.55f,.67f,.64f);} // Weld: T-joint fillet beads
    {auto& x=r[211];rect(x,.10f,.20f,.80f,.60f);circle(x,.28f,.38f,.07f);circle(x,.72f,.62f,.07f);poly(x,{{.35f,.38f},{.52f,.38f},{.675f,.565f}});circle(x,.28f,.64f,.04f);circle(x,.72f,.36f,.04f);} // Board / pads
    {auto& x=r[212];rect(x,.10f,.62f,.14f,.18f);line(x,.24f,.71f,.42f,.71f);arc(x,.42f,.59f,.12f,.12f,90,0,8);line(x,.54f,.59f,.54f,.41f);arc(x,.66f,.41f,.12f,.12f,180,270,8);line(x,.66f,.29f,.76f,.29f);rect(x,.76f,.20f,.14f,.18f);} // Rounded route

    // Direct edit.
    {auto& x=r[650];cube(x,.5f,.62f,.56f);hatch(x,{.22f,.48f},{.5f,.62f},{.78f,.48f},{.5f,.34f},2);arrow(x,.5f,.30f,.5f,.06f,.12f);} // Push / pull a face
    {auto& x=r[651];cube(x,.40f,.60f,.56f);for(auto d:{std::pair{1.f,0.f},std::pair{-1.f,0.f},std::pair{0.f,1.f},std::pair{0.f,-1.f}})arrow(x,.72f,.30f,.72f+d.first*.17f,.30f+d.second*.17f,.07f);}
    {auto& x=r[652];const Point a{.10f,.40f},b{.56f,.16f},c{.90f,.50f},d{.44f,.84f},ab=lerp(a,b,.5f),bc=lerp(b,c,.5f),cd=lerp(c,d,.5f),da=lerp(d,a,.5f),m=lerp(ab,cd,.5f);
        closed(x,{a,b,c,d});seg(x,ab,cd);seg(x,da,bc);seg(x,a,m);seg(x,m,c);seg(x,ab,bc);seg(x,da,cd);} // Mesh patch
    {auto& x=r[653];closed(x,{{.10f,.70f},{.40f,.54f},{.90f,.64f},{.60f,.84f}});poly(x,{{.10f,.70f},{.52f,.36f},{.90f,.64f}});line(x,.52f,.36f,.60f,.84f);line(x,.52f,.36f,.40f,.54f);dot(x,.52f,.36f,.045f);arrow(x,.52f,.28f,.52f,.06f,.11f);} // Pull vertex
    {auto& x=r[654];closed(x,{{.12f,.62f},{.44f,.16f},{.88f,.38f},{.56f,.84f}});line(x,.44f,.16f,.56f,.84f);dot(x,.44f,.16f,.05f);dot(x,.56f,.84f,.05f);} // Mesh edge
    {auto& x=r[655];rect(x,.10f,.30f,.80f,.40f);line(x,.28f,.38f,.26f,.62f);line(x,.40f,.38f,.38f,.62f);line(x,.20f,.46f,.46f,.46f);line(x,.18f,.55f,.44f,.55f);line(x,.64f,.38f,.64f,.62f);} // Value field
    r[656]=r[49502]; // Apply
    {auto& x=r[657];cross(x,.5f,.5f,.62f);} // Cancel
    r[49504]=r[657];
    {auto& x=r[658];const Point a{.10f,.64f},b{.50f,.44f},c{.68f,.53f},d{.28f,.73f},e{.74f,.10f},f{.92f,.19f};closed(x,{a,b,c,d});hatch(x,a,b,c,d,2);
        along(x,b,{.64f,.37f},{.74f,.26f},e,0,10);along(x,c,{.82f,.46f},{.92f,.35f},f,0,10);seg(x,e,f);} // Tangent patch: smooth surface leaving a face

    // Physical properties.
    // 45-degree chord of a circle at signed distance `off` towards the lower right, at most `half` long each side.
    auto chord=[&](ActionIcon& i,float cx,float cy,float rad,float off,float half){const float h=std::min(half,std::sqrt(std::max(0.f,rad*rad-off*off))),mx=cx+off*.7071f,my=cy+off*.7071f;line(i,mx-h*.7071f,my+h*.7071f,mx+h*.7071f,my-h*.7071f);};
    {auto& x=r[187];circle(x,.5f,.5f,.38f);arc(x,.42f,.42f,.36f,.36f,-15,105,12);arc(x,.5f,.5f,.24f,.24f,195,255,6);chord(x,.5f,.5f,.38f,.25f,1);chord(x,.5f,.5f,.38f,.32f,1);} // Material: glossy sample ball
    {auto& x=r[188];circle(x,.5f,.5f,.34f);line(x,.08f,.5f,.92f,.5f);line(x,.5f,.08f,.5f,.92f);for(float o:{.10f,.20f,.30f}){chord(x,.5f,.5f,.34f,-o,o);chord(x,.5f,.5f,.34f,o,o);}} // Mass / CG symbol

    // Simulation.
    {auto& x=r[754];cube(x,.5f,.5f,.76f);seg(x,cL,cR);seg(x,cL,cB);seg(x,cC,cBR);} // Sample mesh: triangulated body
    {auto& x=r[755];rect(x,.12f,.32f,.48f,.48f);rect(x,.40f,.14f,.48f,.48f);hatchRect(x,.40f,.32f,.60f,.62f,.10f);} // Collision: overlap hatched

    // Drawing sheet with a view and a title block (replaces the generic document).
    {auto& x=r[860];x.clear();rect(x,.10f,.18f,.80f,.64f);poly(x,{{.56f,.82f},{.56f,.64f},{.90f,.64f}});line(x,.56f,.73f,.90f,.73f);rect(x,.20f,.28f,.26f,.26f);circle(x,.33f,.41f,.07f);}

    // Plastics.
    {auto& x=r[990];sheet(x,.10f,.10f,.80f,.80f);closed(x,{{.72f,.50f},{.50f,.652f},{.72f,.31f}});} // Reinforce: rib in the corner
    {auto& x=r[991];cube(x,.44f,.46f,.66f);check(x,.76f,.78f,.28f);} // Exact part
    {auto& x=r[992];const Box front=box(.74f,.30f,.16f,.32f,.32f),back=box(.42f,.14f,.16f,.32f,.32f);draw(x,front,{});draw(x,back,{&front.hull});} // Separate
    {auto& x=r[993];rect(x,.10f,.12f,.26f,.20f);rect(x,.37f,.40f,.26f,.20f);rect(x,.64f,.68f,.26f,.20f);poly(x,{{.23f,.32f},{.23f,.50f},{.37f,.50f}});poly(x,{{.50f,.60f},{.50f,.78f},{.64f,.78f}});
        poly(x,{{.31f,.45f},{.37f,.50f},{.31f,.55f}});poly(x,{{.58f,.73f},{.64f,.78f},{.58f,.83f}});} // Workflow
    {auto& x=r[999];cube(x,.5f,.5f,.76f);closed(x,{{.5f,.177f},{.766f,.31f},{.5f,.443f},{.234f,.31f}});line(x,.5f,.177f,.5f,.36f);line(x,.5f,.36f,.417f,.40f);line(x,.5f,.36f,.583f,.40f);} // Hollow walls (shell)

    // Electronics.
    {auto& x=r[994];for(float y:{.24f,.50f,.76f}){check(x,.22f,y,.20f);line(x,.40f,y,.88f,y);}} // Approved vendors list
    {auto& x=r[995];rect(x,.10f,.12f,.46f,.34f);circle(x,.22f,.29f,.05f);circle(x,.42f,.29f,.05f);line(x,.27f,.29f,.37f,.29f);line(x,.40f,.46f,.40f,.62f);arc(x,.52f,.62f,.12f,.12f,180,90,8);line(x,.52f,.74f,.74f,.74f);rect(x,.74f,.64f,.14f,.20f);}
    {auto& x=r[996];const Point a{.16f,.84f},b{.16f,.42f},c{.82f,.60f},d{.82f,.20f};along(x,a,b,c,d,.07f);along(x,a,b,c,d,-.07f);line(x,.09f,.84f,.23f,.84f);arc(x,.82f,.20f,.07f,.035f,0,360,16);} // Smooth 3D cable
    {auto& x=r[997];const Point a{.10f,.72f},b{.42f,.72f},c{.46f,.32f},d{.76f,.32f};for(float o:{-.135f,-.045f,.045f,.135f})along(x,a,b,c,d,o);line(x,.10f,.585f,.10f,.855f);rect(x,.76f,.14f,.12f,.36f);} // Flat ribbon
    {auto& x=r[998];const Point a{.12f,.82f},b{.56f,.92f},c{.44f,.14f},d{.86f,.20f};along(x,a,b,c,d,0);dot(x,a.first,a.second,.04f);
        for(float t:{.33f,.67f})ring(x,bez(a,b,c,d,t),tangent(a,b,c,d,t),.045f,.12f);const Point e=bez(a,b,c,d,.9f);arrow(x,e.first,e.second,d.first,d.second,.12f);} // Guide path through rings

    // Manufacturing.
    {auto& x=r[49600];rect(x,.12f,.72f,.76f,.14f);rect(x,.16f,.16f,.08f,.56f);rect(x,.76f,.16f,.08f,.56f);line(x,.24f,.28f,.76f,.28f);line(x,.24f,.16f,.76f,.16f);rect(x,.44f,.28f,.12f,.20f);line(x,.5f,.48f,.5f,.60f);} // Workspace: machine
    {auto& x=r[49601];rect(x,.18f,.16f,.64f,.74f);rect(x,.38f,.10f,.24f,.12f);for(float y:{.40f,.56f,.72f}){dot(x,.30f,y,.03f);line(x,.40f,y,.70f,y);}} // CNC plan
    {auto& x=r[49602];closed(x,{{.38f,.10f},{.62f,.10f},{.56f,.24f},{.44f,.24f}});poly(x,{{.46f,.24f},{.5f,.32f},{.54f,.24f}});
        for(float y:{.46f,.60f,.74f}){line(x,.24f,y,.76f,y);line(x,.24f,y+.12f,.76f,y+.12f);arc(x,.24f,y+.06f,.06f,.06f,90,270,6);arc(x,.76f,y+.06f,.06f,.06f,-90,90,6);}} // Additive layers
    {auto& x=r[49604];const Box bar=box(.36f,.14f,.52f,.24f,.26f);draw(x,bar,{});hatch(x,{.64f,.52f},{.64f,.78f},{.88f,.66f},{.88f,.40f},2);} // Stock: bar with a sawn end
    {auto& x=r[49605];rect(x,.39f,.10f,.22f,.34f);poly(x,{{.39f,.44f},{.39f,.82f},{.45f,.88f},{.55f,.88f},{.61f,.82f},{.61f,.44f}});line(x,.39f,.52f,.61f,.63f);line(x,.39f,.66f,.61f,.77f);} // Tool: end mill
    {auto& x=r[49606];dot(x,.14f,.20f,.035f);line(x,.17f,.20f,.78f,.20f);arc(x,.78f,.30f,.10f,.10f,-90,90,10);line(x,.78f,.40f,.22f,.40f);arc(x,.22f,.50f,.10f,.10f,270,90,10);line(x,.22f,.60f,.78f,.60f);arc(x,.78f,.70f,.10f,.10f,-90,90,10);arrow(x,.78f,.80f,.20f,.80f,.11f);} // Operation: toolpath
    {auto& x=r[49607];arc(x,.5f,.70f,.38f,.38f,180,360);for(float a:{180.f,225.f,270.f,315.f,360.f})line(x,.5f+.38f*std::cos(a*deg),.70f+.38f*std::sin(a*deg),.5f+.29f*std::cos(a*deg),.70f+.29f*std::sin(a*deg));line(x,.5f,.70f,.5f+.26f*std::cos(310*deg),.70f+.26f*std::sin(310*deg));circle(x,.5f,.70f,.05f);} // Rates gauge
    {auto& x=r[49608];page(x,.42f,.12f,.46f,.76f,.16f);line(x,.54f,.36f,.76f,.36f);line(x,.54f,.66f,.76f,.66f);line(x,.54f,.78f,.70f,.78f);arrow(x,.10f,.51f,.62f,.51f,.14f);} // Import G-code
    {auto& x=r[49615];page(x,.12f,.12f,.46f,.76f,.16f);line(x,.22f,.36f,.44f,.36f);line(x,.22f,.66f,.44f,.66f);line(x,.22f,.78f,.38f,.78f);arrow(x,.38f,.51f,.90f,.51f,.14f);} // Export NC
    {auto& x=r[49609];closed(x,{{.10f,.52f},{.38f,.52f},{.38f,.70f},{.62f,.70f},{.62f,.52f},{.90f,.52f},{.90f,.88f},{.10f,.88f}});rect(x,.45f,.12f,.10f,.24f);rect(x,.45f,.36f,.10f,.30f);arrow(x,.64f,.26f,.88f,.26f,.10f);} // Simulate: tool cutting stock
    {auto& x=r[49610];closed(x,{{.14f,.20f},{.14f,.80f},{.54f,.50f}});line(x,.68f,.22f,.68f,.78f);line(x,.84f,.22f,.84f,.78f);}
    {auto& x=r[49611];rect(x,.22f,.22f,.56f,.56f);}
    {auto& x=r[49616];closed(x,{{.20f,.72f},{.90f,.72f},{.80f,.86f},{.10f,.86f}});arc(x,.5f,.72f,.30f,.52f,180,360);for(float y:{.34f,.46f,.58f}){const float dy=(.72f-y)/.52f,h=.30f*std::sqrt(1-dy*dy);line(x,.5f-h,y,.5f+h,y);}} // Slicer: sliced part on a plate
    {auto& x=r[49618];rect(x,.10f,.72f,.80f,.14f);rect(x,.12f,.30f,.16f,.42f);rect(x,.60f,.30f,.16f,.42f);circle(x,.44f,.51f,.16f);line(x,.76f,.51f,.90f,.51f);line(x,.90f,.38f,.90f,.64f);} // Workholding: vise
    {auto& x=r[49800];line(x,.16f,.30f,.16f,.86f);line(x,.84f,.30f,.84f,.86f);line(x,.16f,.66f,.84f,.66f);poly(x,{{.26f,.60f},{.16f,.66f},{.26f,.72f}});poly(x,{{.74f,.60f},{.84f,.66f},{.74f,.72f}});rect(x,.36f,.28f,.28f,.22f);} // Smart dimension
    {auto& x=r[49811];line(x,.10f,.62f,.24f,.62f);line(x,.76f,.62f,.90f,.62f);dashed(x,.30f,.62f,.70f,.62f,3);arrow(x,.14f,.62f,.14f,.22f,.12f);arrow(x,.86f,.62f,.86f,.22f,.12f);} // Section line
    {auto& x=r[49820];rect(x,.14f,.10f,.72f,.80f);line(x,.14f,.28f,.86f,.28f);closed(x,{{.42f,.28f},{.58f,.28f},{.54f,.40f},{.46f,.40f}});line(x,.24f,.78f,.76f,.78f);rect(x,.38f,.56f,.24f,.22f);line(x,.38f,.67f,.62f,.67f);} // Print job: printer
    {auto& x=r[49821];const Outline front{{.10f,.60f},{.42f,.44f},{.74f,.60f},{.42f,.76f}},back{{.26f,.44f},{.58f,.28f},{.90f,.44f},{.58f,.60f}},part{{.42f,.36f},{.54f,.42f},{.54f,.54f},{.42f,.60f},{.30f,.54f},{.30f,.42f}};
        cube(x,.42f,.48f,.24f);for(size_t k=0;k<4;++k){behind(x,front[k],front[(k+1)%4],{&part});behind(x,back[k],back[(k+1)%4],{&front,&part});}} // Build plates
}
