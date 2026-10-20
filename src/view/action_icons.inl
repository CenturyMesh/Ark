// Supplemental code-native icons. The same normalized strokes render in the
// OpenGL ribbon/gallery and native Windows menus; no external asset is required.
struct ActionIconStroke {float ax,ay,bx,by;};
using ActionIcon=std::vector<ActionIconStroke>;
static const ActionIcon* actionVectorIcon(int action){
    static const std::map<int,ActionIcon> icons=[] {
        std::map<int,ActionIcon> result;
        auto line=[](ActionIcon& icon,float ax,float ay,float bx,float by){icon.push_back({ax,ay,bx,by});};
        auto poly=[&](ActionIcon& icon,std::initializer_list<std::pair<float,float>> points){auto previous=points.begin();for(auto point=previous+1;point!=points.end();++point){line(icon,previous->first,previous->second,point->first,point->second);previous=point;}};
        auto box=[&](ActionIcon& icon,float x,float y,float w,float h){poly(icon,{{x,y},{x+w,y},{x+w,y+h},{x,y+h},{x,y}});};
        auto arc=[&](ActionIcon& icon,float x,float y,float rx,float ry,float start,float end){constexpr float pi=3.14159265359f;for(int i=0;i<24;++i){float a=(start+(end-start)*i/24)*pi/180,b=(start+(end-start)*(i+1)/24)*pi/180;line(icon,x+rx*std::cos(a),y+ry*std::sin(a),x+rx*std::cos(b),y+ry*std::sin(b));}};
        auto& led=result[980];arc(led,.5f,.39f,.22f,.24f,0,360);poly(led,{{.37f,.59f},{.40f,.78f},{.60f,.78f},{.63f,.59f}});line(led,.42f,.87f,.58f,.87f);
        for(int i=0;i<7;++i){float a=(i*45.f-225.f)*3.14159265f/180;line(led,.5f+.31f*std::cos(a),.39f+.31f*std::sin(a),.5f+.41f*std::cos(a),.39f+.41f*std::sin(a));}
        auto& net=result[981];for(int i=0;i<5;++i){float x=.16f+i*.17f;poly(net,{{x,.12f},{x+.035f,.38f},{x-.025f,.64f},{x,.88f}});poly(net,{{.12f,x},{.38f,x+.035f},{.64f,x-.025f},{.88f,x}});}
        auto& panel=result[982];poly(panel,{{.12f,.29f},{.68f,.12f},{.89f,.30f},{.33f,.49f},{.12f,.29f},{.12f,.68f},{.33f,.87f},{.89f,.67f},{.89f,.30f}});line(panel,.33f,.49f,.33f,.87f);line(panel,.25f,.64f,.76f,.47f);line(panel,.38f,.75f,.76f,.61f);
        auto& timber=result[983];poly(timber,{{.12f,.58f},{.72f,.13f},{.90f,.26f},{.31f,.73f},{.12f,.58f},{.12f,.74f},{.31f,.89f},{.90f,.42f},{.90f,.26f}});line(timber,.31f,.73f,.31f,.89f);line(timber,.39f,.71f,.80f,.39f);
        auto& vent=result[984];box(vent,.12f,.14f,.76f,.72f);for(int row=0;row<3;++row)for(int col=0;col<2;++col)box(vent,.23f+col*.31f,.25f+row*.19f,.19f,.065f);result[985]=vent;
        auto& groove=result[986];poly(groove,{{.12f,.25f},{.34f,.25f},{.34f,.53f},{.54f,.53f},{.54f,.25f},{.88f,.25f},{.88f,.83f},{.12f,.83f},{.12f,.25f}});line(groove,.34f,.53f,.23f,.65f);line(groove,.54f,.53f,.66f,.65f);
        auto& stock=result[987];for(int row=0;row<3;++row){float y=.19f+row*.27f;box(stock,.12f,y,.16f,.16f);line(stock,.39f,y+.02f,.87f,y+.02f);line(stock,.39f,y+.13f,.71f,y+.13f);}
        auto& presence=result[988];for(float x:{.25f,.75f}){arc(presence,x,.30f,.12f,.12f,0,360);poly(presence,{{x-.16f,.78f},{x-.16f,.57f},{x,.47f},{x+.16f,.57f},{x+.16f,.78f}});}line(presence,.37f,.30f,.63f,.30f);
        auto& live=result[58];
        box(live,.12f,.16f,.31f,.31f);box(live,.57f,.53f,.31f,.31f);
        poly(live,{{.27f,.47f},{.27f,.68f},{.57f,.68f}});poly(live,{{.43f,.32f},{.73f,.32f},{.73f,.53f}});
        poly(live,{{.48f,.25f},{.55f,.32f},{.48f,.39f}});poly(live,{{.52f,.61f},{.45f,.68f},{.52f,.75f}});
        auto& provider=result[357];
        box(provider,.15f,.29f,.23f,.42f);box(provider,.62f,.29f,.23f,.42f);
        line(provider,.06f,.50f,.15f,.50f);line(provider,.85f,.50f,.94f,.50f);
        line(provider,.38f,.39f,.51f,.39f);line(provider,.38f,.61f,.51f,.61f);
        poly(provider,{{.58f,.32f},{.58f,.22f},{.68f,.22f}});poly(provider,{{.42f,.68f},{.42f,.78f},{.32f,.78f}});
        auto& measure2=result[585];
        box(measure2,.17f,.14f,.64f,.55f);line(measure2,.17f,.82f,.81f,.82f);
        for(float x:{.17f,.81f})line(measure2,x,.75f,x,.89f);
        poly(measure2,{{.27f,.76f},{.17f,.82f},{.27f,.88f}});poly(measure2,{{.71f,.76f},{.81f,.82f},{.71f,.88f}});
        line(measure2,.89f,.14f,.89f,.69f);line(measure2,.84f,.14f,.94f,.14f);line(measure2,.84f,.69f,.94f,.69f);
        auto& measure3=result[586];
        poly(measure3,{{.17f,.32f},{.48f,.14f},{.80f,.32f},{.48f,.51f},{.17f,.32f},{.17f,.65f},{.48f,.84f},{.80f,.65f},{.80f,.32f}});
        line(measure3,.48f,.51f,.48f,.84f);line(measure3,.17f,.65f,.80f,.32f);
        arc(measure3,.17f,.65f,.04f,.04f,0,360);arc(measure3,.80f,.32f,.04f,.04f,0,360);
        auto& units=result[587];
        box(units,.13f,.24f,.74f,.35f);
        for(int i=0;i<7;++i){float x=.20f+i*.10f;line(units,x,.24f,x,i%2?.37f:.43f);}
        poly(units,{{.22f,.77f},{.37f,.77f},{.37f,.68f}});poly(units,{{.78f,.68f},{.63f,.68f},{.63f,.77f}});line(units,.37f,.77f,.63f,.68f);
        auto& bom=result[590];
        box(bom,.18f,.13f,.64f,.74f);line(bom,.18f,.32f,.82f,.32f);line(bom,.37f,.32f,.37f,.87f);
        for(float y:{.48f,.65f})line(bom,.18f,y,.82f,y);
        for(float y:{.41f,.57f,.74f})line(bom,.46f,y,.72f,y);
        auto& fit=result[549];
        poly(fit,{{.12f,.78f},{.37f,.78f},{.65f,.50f},{.65f,.13f},{.85f,.13f},{.85f,.58f},{.51f,.92f}});
        poly(fit,{{.12f,.57f},{.29f,.57f},{.45f,.41f},{.45f,.13f}});line(fit,.45f,.13f,.65f,.13f);
        line(fit,.30f,.51f,.58f,.79f);line(fit,.12f,.57f,.12f,.78f);
        auto& revolve=result[152];
        arc(revolve,.49f,.48f,.25f,.34f,0,360);arc(revolve,.49f,.48f,.10f,.34f,0,360);line(revolve,.49f,.06f,.49f,.90f);
        arc(revolve,.49f,.53f,.39f,.19f,15,320);poly(revolve,{{.79f,.39f},{.82f,.28f},{.70f,.29f}});
        auto& offset=result[153];
        poly(offset,{{.10f,.64f},{.37f,.76f},{.67f,.63f},{.87f,.76f}});poly(offset,{{.10f,.36f},{.37f,.48f},{.67f,.35f},{.87f,.48f}});
        line(offset,.55f,.42f,.55f,.68f);poly(offset,{{.49f,.50f},{.55f,.42f},{.61f,.50f}});poly(offset,{{.49f,.60f},{.55f,.68f},{.61f,.60f}});
        auto& sew=result[154];
        poly(sew,{{.10f,.19f},{.46f,.29f},{.46f,.80f},{.10f,.70f},{.10f,.19f}});poly(sew,{{.54f,.29f},{.90f,.19f},{.90f,.70f},{.54f,.80f},{.54f,.29f}});
        for(float y:{.39f,.52f,.65f})line(sew,.38f,y,.62f,y-.07f);
        for(int id=770;id<=779;++id){auto& icon=result[id];for(float x:{.12f,.43f,.74f})box(icon,x,.30f,.16f,.22f);if(id==771||id==774||id==775)arc(icon,.5f,.5f,.37f,.32f,10,170);else poly(icon,{{.16f,.72f},{.82f,.72f},{.73f,.63f}});if(id==773){line(icon,.18f,.14f,.83f,.14f);line(icon,.18f,.12f,.18f,.20f);}if(id==775)line(icon,.16f,.56f,.83f,.56f);}
        for(int id=800;id<=816;++id){auto& icon=result[id];poly(icon,{{.15f,.80f},{.15f,.55f},{.70f,.55f},{.70f,.18f},{.85f,.18f}});line(icon,.25f,.68f,.81f,.68f);if(id<=802){line(icon,.23f,.53f,.46f,.30f);line(icon,.50f,.30f,.70f,.50f);}else if(id>=806&&id<=808)arc(icon,.35f,.55f,.17f,.17f,180,270);else if(id>=812&&id<=814)box(icon,.33f,.38f,.18f,.17f);else if(id>=815){box(icon,.12f,.12f,.42f,.32f);line(icon,.12f,.28f,.54f,.28f);}}
        for(int id=840;id<=861;++id){auto& icon=result[id];box(icon,.14f,.08f,.72f,.84f);box(icon,.24f,.20f,.25f,.26f);line(icon,.58f,.21f,.76f,.21f);line(icon,.58f,.33f,.76f,.33f);line(icon,.24f,.66f,.76f,.66f);line(icon,.24f,.79f,.76f,.79f);if(id==847)line(icon,.22f,.52f,.75f,.18f);}
        auto& planes=result[960];poly(planes,{{.10f,.37f},{.52f,.14f},{.90f,.37f},{.48f,.61f},{.10f,.37f}});poly(planes,{{.10f,.54f},{.48f,.78f},{.90f,.54f}});poly(planes,{{.10f,.70f},{.48f,.94f},{.90f,.70f}});
        auto& sketch=result[961];poly(sketch,{{.17f,.78f},{.30f,.25f},{.72f,.35f},{.85f,.76f}});for(auto p:{std::pair{.17f,.78f},std::pair{.30f,.25f},std::pair{.72f,.35f},std::pair{.85f,.76f}})box(sketch,p.first-.055f,p.second-.055f,.11f,.11f);
        auto& grid=result[962];for(float t:{.23f,.5f,.77f}){line(grid,.12f,t,.88f,t);line(grid,t,.12f,t,.88f);}
        auto& edges=result[963];poly(edges,{{.17f,.29f},{.50f,.10f},{.83f,.29f},{.83f,.72f},{.50f,.92f},{.17f,.72f},{.17f,.29f},{.50f,.48f},{.83f,.29f}});line(edges,.50f,.48f,.50f,.92f);
        auto& dockFit=result[964];for(float x:{.14f,.86f})for(float y:{.14f,.86f}){line(dockFit,x,y,x+(x<.5f?.20f:-.20f),y);line(dockFit,x,y,x,y+(y<.5f?.20f:-.20f));}box(dockFit,.36f,.36f,.28f,.28f);
        auto& sheet=result[965];box(sheet,.19f,.08f,.62f,.84f);box(sheet,.30f,.22f,.23f,.25f);arc(sheet,.65f,.34f,.07f,.07f,0,360);line(sheet,.30f,.61f,.70f,.61f);line(sheet,.30f,.74f,.70f,.74f);line(sheet,.55f,.74f,.55f,.90f);
        auto& motions=result[45000];line(motions,.12f,.86f,.90f,.86f);line(motions,.12f,.86f,.12f,.12f);for(int i=0;i<16;++i){auto curve=[](float t){return .74f-.52f*t*t*(3-2*t);};const float a=i/16.f,b=(i+1)/16.f;line(motions,.18f+.64f*a,curve(a),.18f+.64f*b,curve(b));}line(motions,.58f,.16f,.58f,.80f);poly(motions,{{.66f,.18f},{.86f,.30f},{.66f,.42f},{.66f,.18f}});
        return result;
    }();
    auto found=icons.find(action);return found==icons.end()?nullptr:&found->second;
}
static float actionIconDistance(float x,float y,const ActionIconStroke& stroke){
    float dx=stroke.bx-stroke.ax,dy=stroke.by-stroke.ay;
    float t=std::clamp(((x-stroke.ax)*dx+(y-stroke.ay)*dy)/(dx*dx+dy*dy),0.f,1.f);
    return std::hypot(x-stroke.ax-t*dx,y-stroke.ay-t*dy);
}
static HBITMAP actionIconMenuBitmap(int action){
    const auto* icon=actionVectorIcon(action);if(!icon)return nullptr;
    static std::map<int,HBITMAP> bitmaps;auto cached=bitmaps.find(action);if(cached!=bitmaps.end())return cached->second;
    constexpr int size=22;BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size;info.bmiHeader.biHeight=-size;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void* raw=nullptr;HBITMAP bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&raw,nullptr,0);if(!bitmap)return nullptr;
    auto* pixels=static_cast<unsigned char*>(raw);
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){unsigned coverage=0;for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){float px=(x+(sx+.5f)/4)/size,py=(y+(sy+.5f)/4)/size;for(const auto& stroke:*icon)if(actionIconDistance(px,py,stroke)<.031f){++coverage;break;}}
        auto* pixel=pixels+(y*size+x)*4;pixel[0]=(unsigned char)(105*coverage/16);pixel[1]=(unsigned char)(76*coverage/16);pixel[2]=(unsigned char)(40*coverage/16);pixel[3]=(unsigned char)(255*coverage/16);
    }
    bitmaps[action]=bitmap;return bitmap;
}
