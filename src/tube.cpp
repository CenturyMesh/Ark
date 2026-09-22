#include "ark/model.hpp"
namespace ark {
TubeResult buildTube(const Feature& f,const ExprEnv& env){
    TubeResult out;
    auto number=[&](const std::string& s){bool ok=false;f64 x=evalExpr(s,env,&ok);if(!ok||std::fabs(x)>1e6)out.error="Invalid tube expression: "+s;return x;};
    auto p=[&](const char* name,f64 def){auto i=f.p.find(name);return i==f.p.end()?def:number(i->second);};
    bool round=f.get("section","round")=="round";
    if(!round&&f.get("section")!="rect"){out.error="section must be round or rect";return out;}
    f64 od=p("od",30),width=p("width",40),height=p("height",30),wall=p("wall",2),tail=p("tail",100);
    f64 tolerance=p("chord_tol",.15),sidesValue=p("sides",48);
    if(!out.error.empty())return out;
    if(wall<=0||tail<0||tolerance<.001||tolerance>5||sidesValue<12||sidesValue>256||sidesValue!=std::floor(sidesValue)||
       (round?(od<=2*wall):(width<=2*wall||height<=2*wall))){out.error="Invalid tube section, wall, tail or resolution";return out;}
    if(f.bends.size()>64){out.error="At most 64 bends are supported";return out;}
    f64 envelope=round?od/2:std::hypot(width,height)/2;
    TubeFrame frame{{0,0,0},{1,0,0},{0,1,0},{0,0,1},0};Vec3 bendNormal=frame.normal;
    out.frames.push_back(frame);
    auto straight=[&](f64 length){if(length>0){frame.point+=frame.tangent*length;frame.station+=length;out.frames.push_back(frame);}};
    for(auto& bend:f.bends){
        f64 feed=number(bend.feed),radius=number(bend.radius),signedAngle=number(bend.angle),roll=number(bend.roll);
        if(!out.error.empty())return out;
        if(feed<0||radius<=envelope||std::fabs(signedAngle)<.001||std::fabs(signedAngle)>180){out.error="Bend requires nonnegative feed, CLR beyond section envelope and angle in [-180,180] excluding zero";return out;}
        straight(feed);out.bendStarts.push_back(frame.station);
        // Roll selects the next bending plane; it never twists the material frame abruptly.
        bendNormal=Mat4::axisAngle(frame.tangent,deg2rad(roll)).transformDir(bendNormal).normalized();
        Vec3 inward=bendNormal*(signedAngle<0?-1.:1.);
        Vec3 axis=frame.tangent.cross(inward).normalized(),center=frame.point+inward*radius;
        f64 angle=deg2rad(std::fabs(signedAngle));
        f64 step=2*std::acos(clamp(1-tolerance/(radius+envelope),-1.,1.));
        int samples=std::max(2,(int)std::ceil(angle/std::max(step,1e-6)));
        if(samples>4096||out.frames.size()+(size_t)samples>16384){out.error="Tube sampling budget exceeded";return out;}
        TubeFrame start=frame;
        for(int k=1;k<=samples;++k){
            f64 theta=angle*k/samples;Mat4 rotation=Mat4::axisAngle(axis,theta);
            frame.point=center+(start.tangent*std::sin(theta)-inward*std::cos(theta))*radius;
            frame.tangent=rotation.transformDir(start.tangent).normalized();
            frame.normal=rotation.transformDir(start.normal).normalized();
            frame.binormal=frame.tangent.cross(frame.normal).normalized();
            frame.station=start.station+radius*theta;out.frames.push_back(frame);
        }
        bendNormal=Mat4::axisAngle(axis,angle).transformDir(bendNormal).normalized();
        out.bendEnds.push_back(frame.station);
    }
    straight(tail);out.centerlineLength=frame.station;
    if(out.frames.size()<2){out.error="Tube must have positive developed length";return out;}
    std::vector<Vec2> outer,inner;
    if(round){int sides=(int)sidesValue;for(int k=0;k<sides;++k){f64 a=K_2PI*k/sides;outer.push_back({od*.5*std::cos(a),od*.5*std::sin(a)});inner.push_back({(od*.5-wall)*std::cos(a),(od*.5-wall)*std::sin(a)});}
        out.sectionArea=K_PI*(od*od/4-(od/2-wall)*(od/2-wall));
    }else{
        for(auto sign:std::vector<Vec2>{{-1,-1},{1,-1},{1,1},{-1,1}}){outer.push_back({sign.x*width/2,sign.y*height/2});inner.push_back({sign.x*(width/2-wall),sign.y*(height/2-wall)});}
        out.sectionArea=width*height-(width-2*wall)*(height-2*wall);
    }
    u32 n=(u32)outer.size(),rings=(u32)out.frames.size();auto& m=out.mesh;
    m.pos.reserve((size_t)rings*n*6);m.idx.reserve((size_t)rings*n*12);
    for(auto& fr:out.frames)for(auto& profile:{outer,inner})for(auto q:profile)m.addVertex(fr.point+fr.normal*q.x+fr.binormal*q.y);
    for(u32 r=0;r+1<rings;++r)for(u32 k=0;k<n;++k){
        u32 a=r*2*n+k,b=(r+1)*2*n+k,an=r*2*n+(k+1)%n,bn=(r+1)*2*n+(k+1)%n;
        m.addTri(a,an,bn);m.addTri(a,bn,b);
        m.addTri(a+n,bn+n,an+n);m.addTri(a+n,b+n,bn+n);
    }
    for(u32 k=0;k<n;++k){u32 next=(k+1)%n,last=(rings-1)*2*n;
        m.addTri(k,k+n,next+n);m.addTri(k,next+n,next);
        m.addTri(last+k,last+next+n,last+k+n);m.addTri(last+k,last+next,last+next+n);
    }
    m.computeNormals();return out;
}
}
