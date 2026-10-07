#include "ark/suspension.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ark {
namespace {
using V=arkjson::Value;

const V& required(const V& object,const char* key){
    const V* found=object.find(key);
    if(!found)throw std::runtime_error(std::string("Suspension needs ")+key);
    return *found;
}
double number(const V& value,const ExprEnv& env,const char* label){
    if(value.t==V::Num){if(std::isfinite(value.num)&&std::abs(value.num)<=1e7)return value.num;}
    else if(value.t==V::Str&&value.str.size()<=2048){
        bool ok=false;const double result=evalExpr(value.str,env,&ok);
        if(ok&&std::isfinite(result)&&std::abs(result)<=1e7)return result;
    }
    throw std::runtime_error(std::string("Invalid suspension expression: ")+label);
}
double number(const V& object,const char* key,const ExprEnv& env){return number(required(object,key),env,key);}
double optional(const V& object,const char* key,const ExprEnv& env,double fallback){
    const V* value=object.find(key);return value?number(*value,env,key):fallback;
}
Vec3 vector(const V& value,const ExprEnv& env,const char* label){
    if(value.t!=V::Arr||value.arr.size()!=3)throw std::runtime_error(std::string(label)+" must be xyz");
    return {number(value.arr[0],env,label),number(value.arr[1],env,label),number(value.arr[2],env,label)};
}
Vec3 vector(const V& object,const char* key,const ExprEnv& env){return vector(required(object,key),env,key);}
V json(Vec3 p){V array=V::makeArr();array.arr={V::makeNum(p.x),V::makeNum(p.y),V::makeNum(p.z)};return array;}
Vec3 unit(Vec3 value,const char* label){
    if(value.len()<=1e-8)throw std::runtime_error(std::string(label)+" is degenerate");
    return value.normalized();
}
Vec3 rotate(Vec3 p,Vec3 axis,double radians){
    const double c=std::cos(radians),s=std::sin(radians);
    return p*c+axis.cross(p)*s+axis*(axis.dot(p)*(1-c));
}
Vec3 around(Vec3 point,Vec3 origin,Vec3 axis,double radians){return origin+rotate(point-origin,axis,radians);}
Vec3 align(Vec3 p,Vec3 from,Vec3 to){
    const Vec3 a=unit(from,"Nominal upright"),b=unit(to,"Solved upright");
    const double cosine=clamp(a.dot(b),-1.,1.);
    if(cosine<-0.999999)throw std::runtime_error("Upright branch reached an antiparallel singularity");
    const Vec3 cross=a.cross(b);const double sine=cross.len();
    if(sine<1e-12)return p;
    return rotate(p,cross/sine,std::atan2(sine,cosine));
}
double root(const std::function<double(double)>& function,double prior,
            double low,double high,const char* label){
    constexpr int samples=240;
    std::vector<double> candidates;
    double previousX=low,previousY=function(low);
    if(!std::isfinite(previousY))throw std::runtime_error(std::string(label)+" is non-finite");
    if(std::abs(previousY)<1e-9)candidates.push_back(low);
    for(int i=1;i<=samples;++i){
        const double x=low+(high-low)*i/samples,y=function(x);
        if(!std::isfinite(y))throw std::runtime_error(std::string(label)+" is non-finite");
        if(std::abs(y)<1e-9)candidates.push_back(x);
        if((previousY<0&&y>0)||(previousY>0&&y<0)){
            double a=previousX,b=x,fa=previousY;
            for(int iteration=0;iteration<64;++iteration){
                const double middle=(a+b)/2,fm=function(middle);
                if(!std::isfinite(fm))throw std::runtime_error(std::string(label)+" is non-finite");
                if((fa<0&&fm<0)||(fa>0&&fm>0)){a=middle;fa=fm;}else b=middle;
            }
            candidates.push_back((a+b)/2);
        }
        previousX=x;previousY=y;
    }
    if(candidates.empty())throw std::runtime_error(std::string(label)+" has no reachable closure on its authored branch");
    const auto closest=std::min_element(candidates.begin(),candidates.end(),
        [&](double a,double b){return std::abs(a-prior)<std::abs(b-prior);});
    const double solution=*closest;
    if(std::abs(function(solution))>1e-5)throw std::runtime_error(std::string(label)+" residual exceeds 1e-5 mm");
    const double step=1e-4;
    const double derivative=(function(std::min(high,solution+step))-function(std::max(low,solution-step)))/
                            (std::min(high,solution+step)-std::max(low,solution-step));
    if(!std::isfinite(derivative)||std::abs(derivative)<1e-4)
        throw std::runtime_error(std::string(label)+" is singular at the selected branch");
    return solution;
}

struct Model {
    Vec3 lowerFront,lowerRear,lowerBall,upperFront,upperRear,upperBall;
    Vec3 tieInner,tieOuter,wheelCenter,wheelAxis,shockChassis,shockArm,rackAxis;
    Vec3 lowerPivot,lowerAxis,upperPivot,upperAxis;
    double uprightLength=0,tieLength=0,lowerFrontLength=0,lowerRearLength=0;
    double upperFrontLength=0,upperRearLength=0;
    double wheelRadius=0,wheelWidth=0,hubRadius=0;
    double springRate=0,springFree=0,springPreload=0,coilDiameter=0,coilTurns=0;
    double springMin=0,springMax=0,jounceMin=0,jounceMax=0,rackMin=0,rackMax=0;
    int side=1;
};
Model parse(const Feature& feature,const ExprEnv& env){
    if(!feature.inputs.empty())throw std::runtime_error("Suspension mechanism does not take feature inputs");
    const V& spec=feature.nativeSpec;
    if(spec.t!=V::Obj)throw std::runtime_error("Suspension native program must be an object");
    const V& hp=required(spec,"hardpoints"),wheel=required(spec,"wheel"),spring=required(spec,"spring"),limits=required(spec,"limits");
    if(hp.t!=V::Obj||wheel.t!=V::Obj||spring.t!=V::Obj||limits.t!=V::Obj)
        throw std::runtime_error("Suspension hardpoints, wheel, spring and limits must be objects");
    Model m;
    const V* side=spec.find("side");
    if(side){
        if(side->t!=V::Str||(side->str!="left"&&side->str!="right"))throw std::runtime_error("Suspension side must be left or right");
        m.side=side->str=="left"?-1:1;
    }
    m.lowerFront=vector(hp,"lower_front",env);m.lowerRear=vector(hp,"lower_rear",env);
    m.lowerBall=vector(hp,"lower_ball",env);m.upperFront=vector(hp,"upper_front",env);
    m.upperRear=vector(hp,"upper_rear",env);m.upperBall=vector(hp,"upper_ball",env);
    m.tieInner=vector(hp,"tie_inner",env);m.tieOuter=vector(hp,"tie_outer",env);
    m.wheelCenter=vector(hp,"wheel_center",env);m.shockChassis=vector(hp,"shock_chassis",env);
    m.shockArm=vector(hp,"shock_arm",env);
    m.wheelAxis=hp.find("wheel_axis")?unit(vector(hp,"wheel_axis",env),"Wheel axis"):Vec3{0.,(double)m.side,0.};
    m.rackAxis=spec.find("rack_axis")?unit(vector(spec,"rack_axis",env),"Rack axis"):Vec3{0,1,0};
    m.lowerPivot=(m.lowerFront+m.lowerRear)*.5;m.upperPivot=(m.upperFront+m.upperRear)*.5;
    m.lowerAxis=unit(m.lowerRear-m.lowerFront,"Lower hinge axis");
    m.upperAxis=unit(m.upperRear-m.upperFront,"Upper hinge axis");
    m.uprightLength=(m.upperBall-m.lowerBall).len();m.tieLength=(m.tieOuter-m.tieInner).len();
    m.lowerFrontLength=(m.lowerBall-m.lowerFront).len();m.lowerRearLength=(m.lowerBall-m.lowerRear).len();
    m.upperFrontLength=(m.upperBall-m.upperFront).len();m.upperRearLength=(m.upperBall-m.upperRear).len();
    if(m.uprightLength<20||m.tieLength<10||m.lowerFrontLength<10||m.lowerRearLength<10||
       m.upperFrontLength<10||m.upperRearLength<10)
        throw std::runtime_error("Suspension hardpoints have degenerate links");
    if(std::abs(m.wheelAxis.dot(unit(m.upperBall-m.lowerBall,"Upright")))>0.8)
        throw std::runtime_error("Wheel axis is too close to the steering axis");
    m.wheelRadius=number(wheel,"radius",env);m.wheelWidth=number(wheel,"width",env);
    m.hubRadius=optional(wheel,"hub_radius",env,m.wheelRadius*.22);
    if(!(m.wheelRadius>20&&m.wheelWidth>5&&m.hubRadius>3&&m.hubRadius<m.wheelRadius*.8))
        throw std::runtime_error("Suspension wheel dimensions are outside the supported display envelope");
    m.springRate=number(spring,"rate_n_per_mm",env);m.springFree=number(spring,"free_length_mm",env);
    m.springPreload=optional(spring,"preload_n",env,0);
    m.coilDiameter=optional(spring,"coil_diameter_mm",env,45);
    m.coilTurns=optional(spring,"turns",env,7);
    m.springMin=optional(spring,"min_length_mm",env,10);
    m.springMax=optional(spring,"max_length_mm",env,1e6);
    if(!(m.springRate>0&&m.springFree>0&&m.springPreload>=0&&m.coilDiameter>12&&
         m.coilTurns>=3&&m.coilTurns<=20&&m.springMin>0&&m.springMax>m.springMin))
        throw std::runtime_error("Suspension spring dimensions, rate or travel are invalid");
    m.jounceMin=number(limits,"jounce_min_mm",env);m.jounceMax=number(limits,"jounce_max_mm",env);
    m.rackMin=number(limits,"rack_min_mm",env);m.rackMax=number(limits,"rack_max_mm",env);
    if(!(m.jounceMin<0&&m.jounceMax>0&&m.jounceMax-m.jounceMin<=300&&
         m.rackMin<0&&m.rackMax>0&&m.rackMax-m.rackMin<=150))
        throw std::runtime_error("Suspension jounce/rack limits must straddle zero within supported ranges");
    return m;
}

struct Pose {
    double lowerAngle=0,upperAngle=0,steerAngle=0;
    Vec3 lowerBall,upperBall,tieInner,tieOuter,wheelCenter,wheelAxis,shockArm;
    double springLength=0,springForce=0,residual=0,camber=0,toe=0;
};
Vec3 upperAt(const Model& m,double angle){return around(m.upperBall,m.upperPivot,m.upperAxis,angle);}
Vec3 lowerAt(const Model& m,double angle){return around(m.lowerBall,m.lowerPivot,m.lowerAxis,angle);}
Vec3 uprightPoint(const Model& m,Vec3 original,Vec3 lower,Vec3 upper,double steer){
    const Vec3 axis=unit(upper-lower,"Solved upright");
    const Vec3 local=align(original-m.lowerBall,m.upperBall-m.lowerBall,upper-lower);
    return lower+rotate(local,axis,steer);
}
Vec3 uprightDirection(const Model& m,Vec3 direction,Vec3 lower,Vec3 upper,double steer){
    const Vec3 axis=unit(upper-lower,"Solved upright");
    return unit(rotate(align(direction,m.upperBall-m.lowerBall,upper-lower),axis,steer),"Solved wheel axis");
}
double tieResidual(const Model& m,Vec3 lower,Vec3 upper,Vec3 inner,double steer){
    return (uprightPoint(m,m.tieOuter,lower,upper,steer)-inner).len()-m.tieLength;
}
Pose solve(const Model& m,double jounce,double rack){
    if(!std::isfinite(jounce)||!std::isfinite(rack)||jounce<m.jounceMin-1e-9||jounce>m.jounceMax+1e-9||
       rack<m.rackMin-1e-9||rack>m.rackMax+1e-9)
        throw std::runtime_error("Suspension state exceeds declared jounce or rack bounds");
    Pose pose;pose.tieInner=m.tieInner+m.rackAxis*rack;
    pose.lowerBall=m.lowerBall;pose.upperBall=m.upperBall;
    auto tie=[&](double angle){return tieResidual(m,pose.lowerBall,pose.upperBall,pose.tieInner,angle);};
    pose.steerAngle=root(tie,0,-1.0,1.0,"Tie rod steering");
    const int steps=std::max(1,(int)std::ceil(std::abs(jounce)/5));
    for(int step=1;step<=steps;++step){
        const double target=m.lowerBall.z+jounce*step/steps;
        pose.lowerAngle=root([&](double angle){return lowerAt(m,angle).z-target;},
                             pose.lowerAngle,-1.15,1.15,"Lower arm jounce");
        pose.lowerBall=lowerAt(m,pose.lowerAngle);
        pose.upperAngle=root([&](double angle){return (upperAt(m,angle)-pose.lowerBall).len()-m.uprightLength;},
                             pose.upperAngle,-1.15,1.15,"Upper arm/upright");
        pose.upperBall=upperAt(m,pose.upperAngle);
        pose.steerAngle=root([&](double angle){return tieResidual(m,pose.lowerBall,pose.upperBall,pose.tieInner,angle);},
                             pose.steerAngle,-1.0,1.0,"Tie rod steering");
    }
    pose.tieOuter=uprightPoint(m,m.tieOuter,pose.lowerBall,pose.upperBall,pose.steerAngle);
    pose.wheelCenter=uprightPoint(m,m.wheelCenter,pose.lowerBall,pose.upperBall,pose.steerAngle);
    pose.wheelAxis=uprightDirection(m,m.wheelAxis,pose.lowerBall,pose.upperBall,pose.steerAngle);
    pose.shockArm=around(m.shockArm,m.lowerPivot,m.lowerAxis,pose.lowerAngle);
    pose.springLength=(pose.shockArm-m.shockChassis).len();
    if(pose.springLength<m.springMin-1e-6||pose.springLength>m.springMax+1e-6)
        throw std::runtime_error("Suspension spring exceeds its declared length limits");
    pose.springForce=std::max(0.,m.springRate*(m.springFree-pose.springLength)+m.springPreload);
    const double residuals[]{
        std::abs((pose.lowerBall-m.lowerFront).len()-m.lowerFrontLength),
        std::abs((pose.lowerBall-m.lowerRear).len()-m.lowerRearLength),
        std::abs((pose.upperBall-m.upperFront).len()-m.upperFrontLength),
        std::abs((pose.upperBall-m.upperRear).len()-m.upperRearLength),
        std::abs((pose.upperBall-pose.lowerBall).len()-m.uprightLength),
        std::abs((pose.tieOuter-pose.tieInner).len()-m.tieLength),
        std::abs(pose.lowerBall.z-(m.lowerBall.z+jounce))
    };
    for(double error:residuals)pose.residual=std::max(pose.residual,error);
    if(pose.residual>1e-5)throw std::runtime_error("Suspension rigid-link closure exceeds 1e-5 mm");
    pose.toe=rad2deg(std::atan2(-m.side*pose.wheelAxis.x,m.side*pose.wheelAxis.y));
    pose.camber=rad2deg(std::atan2(m.side*pose.wheelAxis.z,
                                  std::hypot(pose.wheelAxis.x,pose.wheelAxis.y)));
    return pose;
}

void rod(Mesh& mesh,Vec3 a,Vec3 b,double radius,int sides=14){
    const Vec3 tangent=unit(b-a,"Display rod"),reference=std::abs(tangent.z)<.9?Vec3{0,0,1}:Vec3{1,0,0};
    const Vec3 side=unit(tangent.cross(reference),"Display rod frame"),up=tangent.cross(side);
    const u32 first=(u32)mesh.vertCount();
    for(Vec3 center:{a,b})for(int i=0;i<sides;++i){
        const double angle=K_2PI*i/sides;
        mesh.addVertex(center+(side*std::cos(angle)+up*std::sin(angle))*radius);
    }
    const u32 ca=(u32)mesh.vertCount();mesh.addVertex(a);
    const u32 cb=(u32)mesh.vertCount();mesh.addVertex(b);
    for(int i=0;i<sides;++i){
        const u32 next=(u32)((i+1)%sides),i0=first+(u32)i,i1=first+next,j0=i0+(u32)sides,j1=i1+(u32)sides;
        mesh.addTri(i0,i1,j1);mesh.addTri(i0,j1,j0);
        mesh.addTri(ca,i1,i0);mesh.addTri(cb,j0,j1);
    }
}
void ball(Mesh& mesh,Vec3 center,double radius){
    constexpr int bands=8,slices=12;
    const u32 south=(u32)mesh.vertCount();mesh.addVertex(center+Vec3{0,0,-radius});
    const u32 first=(u32)mesh.vertCount();
    for(int j=1;j<bands;++j){
        const double latitude=-K_HALF_PI+K_PI*j/bands;
        for(int i=0;i<slices;++i){
            const double azimuth=K_2PI*i/slices;
            mesh.addVertex(center+Vec3{std::cos(latitude)*std::cos(azimuth),
                                       std::cos(latitude)*std::sin(azimuth),
                                       std::sin(latitude)}*radius);
        }
    }
    const u32 north=(u32)mesh.vertCount();mesh.addVertex(center+Vec3{0,0,radius});
    for(int i=0;i<slices;++i){const u32 a=first+(u32)i,b=first+(u32)((i+1)%slices);
        mesh.addTri(south,b,a);
    }
    for(int j=0;j<bands-2;++j)for(int i=0;i<slices;++i){
        const u32 a=first+(u32)(j*slices+i),b=first+(u32)(j*slices+(i+1)%slices);
        const u32 c=a+(u32)slices,d=b+(u32)slices;
        mesh.addTri(a,b,d);mesh.addTri(a,d,c);
    }
    const u32 last=first+(u32)((bands-2)*slices);
    for(int i=0;i<slices;++i)mesh.addTri(last+(u32)i,last+(u32)((i+1)%slices),north);
}
void annulus(Mesh& mesh,Vec3 center,Vec3 axis,double halfWidth,double outer,double inner,int sides=32){
    const Vec3 reference=std::abs(axis.z)<.9?Vec3{0,0,1}:Vec3{1,0,0};
    const Vec3 x=unit(axis.cross(reference),"Wheel section"),y=axis.cross(x);
    const u32 first=(u32)mesh.vertCount();
    for(int ring=0;ring<4;++ring){
        const Vec3 at=center+axis*(ring<2?-halfWidth:halfWidth);
        const double radius=ring%2==0?outer:inner;
        for(int i=0;i<sides;++i){const double angle=K_2PI*i/sides;
            mesh.addVertex(at+(x*std::cos(angle)+y*std::sin(angle))*radius);}
    }
    auto face=[&](int ra,int rb,bool invert){
        for(int i=0;i<sides;++i){
            const u32 a=first+(u32)(ra*sides+i),b=first+(u32)(ra*sides+(i+1)%sides);
            const u32 c=first+(u32)(rb*sides+i),d=first+(u32)(rb*sides+(i+1)%sides);
            if(invert){mesh.addTri(a,d,b);mesh.addTri(a,c,d);}else{mesh.addTri(a,b,d);mesh.addTri(a,d,c);}
        }
    };
    face(0,2,false);face(1,3,true);face(0,1,true);face(2,3,false);
}
void tyre(Mesh& mesh,Vec3 center,Vec3 axis,double width,double radius){
    const Vec3 reference=std::abs(axis.z)<.9?Vec3{0,0,1}:Vec3{1,0,0};
    const Vec3 x=unit(axis.cross(reference),"Tyre frame"),y=axis.cross(x);
    struct Section{double axial,radial;};
    const Section profile[]{
        {-.5*width,.68*radius},{-.5*width,.77*radius},
        {-.43*width,.89*radius},{-.30*width,radius},
        {.30*width,radius},{.43*width,.89*radius},
        {.5*width,.77*radius},{.5*width,.68*radius}
    };
    constexpr int sides=48,sections=(int)(sizeof(profile)/sizeof(profile[0]));
    const u32 first=(u32)mesh.vertCount();
    for(const auto& section:profile)for(int i=0;i<sides;++i){
        const double angle=K_2PI*i/sides;
        mesh.addVertex(center+axis*section.axial+
                       (x*std::cos(angle)+y*std::sin(angle))*section.radial);
    }
    for(int section=0;section<sections;++section)for(int i=0;i<sides;++i){
        const u32 a=first+(u32)(section*sides+i),b=first+(u32)(section*sides+(i+1)%sides);
        const u32 c=first+(u32)(((section+1)%sections)*sides+i),d=first+(u32)(((section+1)%sections)*sides+(i+1)%sides);
        mesh.addTri(a,b,d);mesh.addTri(a,d,c);
    }
}
void triangularBracket(Mesh& mesh,Vec3 a,Vec3 b,Vec3 c,double thickness){
    const Vec3 normal=unit((b-a).cross(c-a),"Knuckle bracket profile");
    const Vec3 offset=normal*(thickness*.5);
    const u32 first=(u32)mesh.vertCount();
    for(Vec3 point:{a,b,c})mesh.addVertex(point-offset);
    for(Vec3 point:{a,b,c})mesh.addVertex(point+offset);
    mesh.addTri(first,first+2,first+1);
    mesh.addTri(first+3,first+4,first+5);
    for(u32 i=0;i<3;++i){
        const u32 next=(i+1)%3,lo=first+i,loNext=first+next,hi=lo+3,hiNext=loNext+3;
        mesh.addTri(lo,loNext,hiNext);mesh.addTri(lo,hiNext,hi);
    }
}
void coil(Mesh& mesh,Vec3 from,Vec3 to,double diameter,double turns){
    const Vec3 axis=unit(to-from,"Spring axis"),reference=std::abs(axis.z)<.9?Vec3{0,0,1}:Vec3{1,0,0};
    const Vec3 x=unit(axis.cross(reference),"Spring frame"),y=axis.cross(x);
    const int segments=std::max(48,(int)std::ceil(turns*24));
    constexpr int sides=10;const double wireRadius=2.8;
    const u32 first=(u32)mesh.vertCount();
    for(int i=0;i<=segments;++i){
        const double f=(double)i/segments,angle=K_2PI*turns*f;
        const Vec3 center=from+(to-from)*f+(x*std::cos(angle)+y*std::sin(angle))*diameter*.5;
        const Vec3 tangent=unit(to-from+(x*-std::sin(angle)+y*std::cos(angle))*(K_2PI*turns*diameter*.5),"Spring tangent");
        const Vec3 radial=unit(x*std::cos(angle)+y*std::sin(angle),"Spring radial");
        const Vec3 binormal=unit(tangent.cross(radial),"Spring wire frame");
        for(int side=0;side<sides;++side){const double a=K_2PI*side/sides;
            mesh.addVertex(center+(radial*std::cos(a)+binormal*std::sin(a))*wireRadius);}
    }
    for(int i=0;i<segments;++i)for(int side=0;side<sides;++side){
        const u32 a=first+(u32)(i*sides+side),b=first+(u32)(i*sides+(side+1)%sides);
        const u32 c=a+(u32)sides,d=b+(u32)sides;
        mesh.addTri(a,b,d);mesh.addTri(a,d,c);
    }
    const u32 start=(u32)mesh.vertCount();mesh.addVertex(from+x*diameter*.5);
    const double endAngle=K_2PI*turns;
    const u32 end=(u32)mesh.vertCount();mesh.addVertex(to+(x*std::cos(endAngle)+y*std::sin(endAngle))*diameter*.5);
    for(int side=0;side<sides;++side){
        mesh.addTri(start,first+(u32)((side+1)%sides),first+(u32)side);
        const u32 base=first+(u32)(segments*sides);
        mesh.addTri(end,base+(u32)side,base+(u32)((side+1)%sides));
    }
}
void addPart(SuspensionPreview& preview,std::string name,Mesh mesh,Vec3 color){
    mesh.computeNormals();preview.mesh.append(mesh);
    SuspensionPart part;part.name=std::move(name);part.mesh=std::move(mesh);part.color=color;
    preview.parts.push_back(std::move(part));
}
V report(const Model& m,const Pose& pose,const Pose& baseline,const SuspensionState& state){
    V result=V::makeObj(),points=V::makeObj(),links=V::makeObj(),spring=V::makeObj(),wheel=V::makeObj(),limits=V::makeObj();
    for(const auto& [name,value]:std::vector<std::pair<std::string,Vec3>>{
        {"lower_front",m.lowerFront},{"lower_rear",m.lowerRear},{"lower_ball",pose.lowerBall},
        {"upper_front",m.upperFront},{"upper_rear",m.upperRear},{"upper_ball",pose.upperBall},
        {"tie_inner",pose.tieInner},{"tie_outer",pose.tieOuter},{"shock_chassis",m.shockChassis},
        {"shock_arm",pose.shockArm},{"wheel_center",pose.wheelCenter}})points.set(name,json(value));
    links.set("lower_front_mm",V::makeNum((pose.lowerBall-m.lowerFront).len()));
    links.set("lower_rear_mm",V::makeNum((pose.lowerBall-m.lowerRear).len()));
    links.set("upper_front_mm",V::makeNum((pose.upperBall-m.upperFront).len()));
    links.set("upper_rear_mm",V::makeNum((pose.upperBall-m.upperRear).len()));
    links.set("upright_mm",V::makeNum((pose.upperBall-pose.lowerBall).len()));
    links.set("tie_rod_mm",V::makeNum((pose.tieOuter-pose.tieInner).len()));
    spring.set("length_mm",V::makeNum(pose.springLength));
    spring.set("free_length_mm",V::makeNum(m.springFree));
    spring.set("rate_n_per_mm",V::makeNum(m.springRate));
    spring.set("preload_n",V::makeNum(m.springPreload));
    spring.set("force_n",V::makeNum(pose.springForce));
    spring.set("quasi_static",V::makeBool(true));
    wheel.set("center_mm",json(pose.wheelCenter));wheel.set("axis",json(pose.wheelAxis));
    wheel.set("radius_mm",V::makeNum(m.wheelRadius));wheel.set("width_mm",V::makeNum(m.wheelWidth));
    wheel.set("spin_deg",V::makeNum(state.wheelSpinDeg));
    limits.set("jounce_min_mm",V::makeNum(m.jounceMin));limits.set("jounce_max_mm",V::makeNum(m.jounceMax));
    limits.set("rack_min_mm",V::makeNum(m.rackMin));limits.set("rack_max_mm",V::makeNum(m.rackMax));
    result.set("hardpoints_mm",std::move(points));result.set("link_lengths",std::move(links));
    result.set("spring",std::move(spring));result.set("wheel",std::move(wheel));result.set("limits",std::move(limits));
    result.set("side",V::makeStr(m.side<0?"left":"right"));
    result.set("jounce_mm",V::makeNum(state.jounceMm));result.set("rack_mm",V::makeNum(state.rackMm));
    result.set("toe_deg",V::makeNum(pose.toe));result.set("camber_deg",V::makeNum(pose.camber));
    result.set("bump_steer_deg",V::makeNum(pose.toe-baseline.toe));
    result.set("lower_arm_angle_deg",V::makeNum(rad2deg(pose.lowerAngle)));
    result.set("upper_arm_angle_deg",V::makeNum(rad2deg(pose.upperAngle)));
    result.set("steering_rotation_deg",V::makeNum(rad2deg(pose.steerAngle)));
    result.set("closure_residual_mm_max",V::makeNum(pose.residual));
    result.set("branch_continuity_checked",V::makeBool(true));
    result.set("display_mesh_authoritative",V::makeBool(false));
    result.set("kinematic_constraint_checked",V::makeBool(true));
    result.set("vehicle_dynamics_checked",V::makeBool(false));
    result.set("collision_checked",V::makeBool(false));
    result.set("tire_contact_checked",V::makeBool(false));
    return result;
}
SuspensionPreview display(const Model& m,const Pose& pose,const Pose& baseline,const SuspensionState& state){
    SuspensionPreview preview;preview.report=report(m,pose,baseline,state);
    auto bar=[&](const char* name,Vec3 a,Vec3 b,double radius,Vec3 tint){Mesh mesh;rod(mesh,a,b,radius);addPart(preview,name,std::move(mesh),tint);};
    const Vec3 steel{.52,.57,.63},dark{.12,.15,.18},aluminium{.73,.75,.78},red{.83,.12,.12};
    bar("lower_arm_front",m.lowerFront,pose.lowerBall,12,steel);
    bar("lower_arm_rear",m.lowerRear,pose.lowerBall,12,steel);
    bar("upper_arm_front",m.upperFront,pose.upperBall,10,steel);
    bar("upper_arm_rear",m.upperRear,pose.upperBall,10,steel);
    const Vec3 hubMount=pose.wheelCenter-pose.wheelAxis*m.wheelWidth*.35;
    Mesh knuckle;triangularBracket(knuckle,pose.lowerBall,pose.upperBall,hubMount,22);
    rod(knuckle,hubMount,pose.wheelCenter,17);
    addPart(preview,"knuckle",std::move(knuckle),aluminium);
    bar("tie_rod",pose.tieInner,pose.tieOuter,8,aluminium);
    const Vec3 shockAxis=unit(pose.shockArm-m.shockChassis,"Shock axis");
    const Vec3 split=m.shockChassis+shockAxis*pose.springLength*.58;
    bar("shock_body",m.shockChassis,split,14,dark);
    bar("shock_shaft",split,pose.shockArm,6.5,aluminium);
    Mesh spring;coil(spring,m.shockChassis+shockAxis*12,pose.shockArm-shockAxis*12,m.coilDiameter,m.coilTurns);
    addPart(preview,"coil_spring",std::move(spring),red);
    Mesh joints;for(Vec3 point:{m.lowerFront,m.lowerRear,m.upperFront,m.upperRear,
                                pose.lowerBall,pose.upperBall,pose.tieInner,pose.tieOuter,
                                m.shockChassis,pose.shockArm})ball(joints,point,10);
    addPart(preview,"ball_joints",std::move(joints),aluminium);
    const Vec3 wheelAxis=pose.wheelAxis,center=pose.wheelCenter;
    Mesh tyreMesh;tyre(tyreMesh,center,wheelAxis,m.wheelWidth,m.wheelRadius);
    addPart(preview,"wheel_tyre",std::move(tyreMesh),dark);
    Mesh rim;annulus(rim,center,wheelAxis,m.wheelWidth*.36,m.wheelRadius*.76,m.wheelRadius*.54,48);
    addPart(preview,"wheel_rim",std::move(rim),aluminium);
    Mesh rotor;annulus(rotor,center-wheelAxis*m.wheelWidth*.29,wheelAxis,3,m.wheelRadius*.52,m.hubRadius*.72);
    addPart(preview,"brake_rotor",std::move(rotor),{.4,.43,.45});
    Mesh hub;rod(hub,center-wheelAxis*m.wheelWidth*.34,center+wheelAxis*m.wheelWidth*.34,m.hubRadius);
    addPart(preview,"hub",std::move(hub),{.34,.38,.43});
    const Vec3 reference=std::abs(wheelAxis.z)<.9?Vec3{0,0,1}:Vec3{1,0,0};
    const Vec3 radial=unit(wheelAxis.cross(reference),"Wheel spoke frame"),binormal=wheelAxis.cross(radial);
    const Vec3 spokeCenter=center+wheelAxis*m.wheelWidth*.39;
    Mesh spokes;for(int i=0;i<5;++i){const double a=deg2rad(state.wheelSpinDeg)+K_2PI*i/5;
        const Vec3 direction=radial*std::cos(a)+binormal*std::sin(a);
        rod(spokes,spokeCenter+direction*m.hubRadius*.7,
            spokeCenter+direction*m.wheelRadius*.68,8,12);
    }
    addPart(preview,"spinning_spokes",std::move(spokes),{.85,.87,.9});
    Mesh marker;const double spin=deg2rad(state.wheelSpinDeg);
    ball(marker,center+(radial*std::cos(spin)+binormal*std::sin(spin))*m.wheelRadius*.9+wheelAxis*m.wheelWidth*.5,5);
    addPart(preview,"spin_marker",std::move(marker),{.9,.28,.06});
    preview.mesh.computeNormals();return preview;
}
} // namespace

SuspensionPreview buildSuspensionPreview(const Feature& feature,const ExprEnv& env,
                                          const SuspensionState& state){
    if(!std::isfinite(state.wheelSpinDeg)||std::abs(state.wheelSpinDeg)>1e7)
        throw std::runtime_error("Wheel spin angle must be finite");
    const Model model=parse(feature,env);
    const Pose baseline=solve(model,0,state.rackMm);
    const Pose pose=state.jounceMm==0?baseline:solve(model,state.jounceMm,state.rackMm);
    return display(model,pose,baseline,state);
}

Body evaluateSuspensionFeature(const Feature& feature,const ExprEnv& env){
    const Model model=parse(feature,env);
    // The authored envelope is accepted only when its corners and the nominal
    // branch all close. Runtime previews still validate every requested state.
    for(double jounce:{model.jounceMin,0.,model.jounceMax})
        for(double rack:{model.rackMin,0.,model.rackMax})
            (void)solve(model,jounce,rack);
    const Pose nominal=solve(model,0,0);
    SuspensionPreview preview=display(model,nominal,nominal,{});
    Body body;body.baked=std::make_unique<Mesh>(std::move(preview.mesh));
    body.nativeData=V::makeObj();body.nativeData.set("suspension_report",std::move(preview.report));
    body.nativeData.set("display_only",V::makeBool(true));
    return body;
}

} // namespace ark
