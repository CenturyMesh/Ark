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
