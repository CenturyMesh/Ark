// Line icons, batch C: commands that used shader glyphs (header, Boolean, utility).
// Same kit and stroke engine as batches A/B, so every command shares one weight.
static void addActionIconsSetC(std::map<int,ActionIcon>& r){
    using namespace icon_kit;
    // Boolean family: two overlapping bodies; the kept region is drawn, the rest is dashed.
    {auto& fuse=r[8];arc(fuse,.38f,.5f,.26f,.26f,62,298);arc(fuse,.62f,.5f,.26f,.26f,-118,118);}
    {auto& carve=r[9];circle(carve,.38f,.5f,.26f);arc(carve,.62f,.5f,.26f,.26f,-118,118,12);dashed(carve,.62f,.24f,.88f,.5f,2);dashed(carve,.88f,.5f,.62f,.76f,2);}
    {auto& keep=r[10];arc(keep,.38f,.5f,.26f,.26f,-62,62);arc(keep,.62f,.5f,.26f,.26f,118,242);arc(keep,.38f,.5f,.26f,.26f,62,298,10);}
    {auto& fit=r[20];for(float x:{.14f,.86f})for(float y:{.14f,.86f}){float sx=x<.5f?1.f:-1.f,sy=y<.5f?1.f:-1.f;line(fit,x,y,x+.2f*sx,y);line(fit,x,y,x,y+.2f*sy);}rect(fit,.34f,.34f,.32f,.32f);}
    {auto& page=r[55];poly(page,{{.64f,.12f},{.24f,.12f},{.24f,.88f},{.76f,.88f},{.76f,.24f},{.64f,.12f},{.64f,.24f},{.76f,.24f}});plus(page,.5f,.58f,.26f);}
    {auto& gear=r[56];constexpr int teeth=8;for(int t=0;t<teeth;++t){float a=t*360.f/teeth;arc(gear,.5f,.5f,.36f,.36f,a-9,a+9,3);arc(gear,.5f,.5f,.27f,.27f,a+13,a+32,4);
        float k=3.14159265f/180;for(float e:{a-9.f,a+9.f}){float c=std::cos(e*k),s=std::sin(e*k);line(gear,.5f+.27f*c,.5f+.27f*s,.5f+.36f*c,.5f+.36f*s);}}circle(gear,.5f,.5f,.11f);}
    {auto& account=r[57];circle(account,.5f,.32f,.16f);arc(account,.5f,.86f,.3f,.32f,180,360);line(account,.2f,.86f,.8f,.86f);}
    {auto& primitive=r[100];cube(primitive,.5f,.5f,.74f);}
    {auto& exportIcon=r[213];poly(exportIcon,{{.46f,.2f},{.16f,.2f},{.16f,.84f},{.8f,.84f},{.8f,.54f}});arrow(exportIcon,.44f,.56f,.84f,.16f,.2f);}
    {auto& loads=r[216];for(float x:{.3f,.5f,.7f})arrow(loads,x,.14f,x,.6f,.12f);line(loads,.14f,.72f,.86f,.72f);for(float x:{.22f,.42f,.62f,.82f})line(loads,x,.72f,x-.08f,.84f);}
    {auto& edit=r[325];pencil(edit,.16f,.16f,.68f);}
    {auto& assistant=r[400];closed(assistant,{{.14f,.18f},{.86f,.18f},{.86f,.66f},{.42f,.66f},{.24f,.84f},{.26f,.66f},{.14f,.66f}});for(float x:{.34f,.5f,.66f})dot(assistant,x,.42f,.03f);}
    {auto& search=r[413];circle(search,.42f,.42f,.26f);line(search,.61f,.61f,.86f,.86f);}
    // Tools: wrench beside a screwdriver, the generic toolbox for agent/engineering tools.
    {auto& tools=r[457];arc(tools,.32f,.3f,.17f,.17f,30,330,14);poly(tools,{{.46f,.22f},{.38f,.3f},{.46f,.38f}});poly(tools,{{.46f,.38f},{.78f,.7f},{.72f,.76f},{.4f,.44f}});line(tools,.82f,.14f,.62f,.42f);closed(tools,{{.56f,.44f},{.66f,.5f},{.4f,.86f},{.3f,.8f}});}
    {auto& more=r[459];for(float x:{.24f,.5f,.76f})circle(more,x,.5f,.06f);}
    {auto& library=r[484];rect(library,.14f,.2f,.14f,.64f);rect(library,.32f,.2f,.14f,.64f);closed(library,{{.52f,.26f},{.65f,.22f},{.82f,.8f},{.69f,.84f}});line(library,.1f,.88f,.9f,.88f);}
    {auto& grid=r[579];for(float v:{.3f,.5f,.7f}){line(grid,v,.14f,v,.86f);line(grid,.14f,v,.86f,v);}}
    {auto& library=r[49960];for(float x:{.16f,.56f})for(float y:{.16f,.56f})rect(library,x,y,.28f,.28f);}
    {auto& config=r[49920];for(float y:{.25f,.5f,.75f})line(config,.14f,y,.86f,y);for(auto p:{std::pair<float,float>{.34f,.25f},{.66f,.5f},{.46f,.75f}})rect(config,p.first-.06f,p.second-.08f,.12f,.16f);}
    {auto& compare=r[49921];cube(compare,.28f,.5f,.38f);cube(compare,.72f,.5f,.38f);}
    {auto& explore=r[49922];line(explore,.16f,.84f,.16f,.16f);line(explore,.16f,.84f,.86f,.84f);poly(explore,{{.25f,.68f},{.43f,.49f},{.61f,.58f},{.81f,.23f}});for(auto p:{std::pair<float,float>{.25f,.68f},{.43f,.49f},{.61f,.58f},{.81f,.23f}})circle(explore,p.first,p.second,.04f);}
    {auto& compare=r[49940];rect(compare,.14f,.16f,.44f,.54f);rect(compare,.42f,.3f,.44f,.54f);}
    {auto& repair=r[49941];cube(repair,.4f,.4f,.5f);pencil(repair,.5f,.5f,.35f);}
    {auto& hide=r[49942];rect(hide,.18f,.22f,.64f,.56f);line(hide,.15f,.85f,.85f,.15f);}
    // Configurations: one part shown as three stacked variants with a selected front.
    {auto& configs=r[44080];rect(configs,.34f,.14f,.52f,.4f);poly(configs,{{.24f,.62f},{.24f,.24f},{.34f,.24f}});poly(configs,{{.14f,.72f},{.14f,.34f},{.24f,.34f}});
        poly(configs,{{.14f,.72f},{.14f,.86f},{.66f,.86f},{.66f,.54f}});check(configs,.6f,.34f,.22f);}
}
