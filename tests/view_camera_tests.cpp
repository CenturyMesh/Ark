#include "ark/view_camera.hpp"
#include <iostream>
#include <stdexcept>
using namespace ark;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool same(CameraFrame a,CameraFrame b){return (a.outward-b.outward).len()<1e-9&&(a.right-b.right).len()<1e-9&&(a.up-b.up).len()<1e-9;}
CameraFrame rendered(CameraFrame frame){double yaw,pitch,roll;cameraAngles(frame,yaw,pitch,roll);return cameraFrame(yaw,pitch,roll);}
int main(){try{
    const double scale=2*K_PI/720.;auto front=cameraFrame(0,0);Vec3 visiblePoint{0,0,1};
    auto right=orbitCameraPixels(front,12,0,scale),down=orbitCameraPixels(front,0,12,scale);
    check(visiblePoint.dot(right.right)>0,"drag right moves visible front geometry right");
    check(-visiblePoint.dot(down.up)>0,"drag down moves visible front geometry down");
    for(auto start:{front,cameraFrame(.7,.8,.4),cameraFrame(-1.1,-1.2,-.9)})for(int axis=0;axis<3;++axis)for(int sign:{-1,1}){
        auto frame=start;for(int step=1;step<=2160;++step){
            frame=rendered(orbitCameraPixels(frame,axis==1?0:sign,axis==1?sign:0,scale,axis==2));
            if(step%720==0)check(same(frame,start),"three continuous full turns return to start in either direction on each axis");
            if(step==900)check(!same(frame,start),"rotation keeps moving after crossing the old virtual rim");
        }
    }
    auto frame=front;for(int i=0;i<1500;++i)frame=rendered(orbitCameraPixels(frame,2,1,scale));
    for(int i=0;i<1500;++i)frame=rendered(orbitCameraPixels(frame,-2,-1,scale));
    check(same(frame,front),"diagonal drag followed by inverse drag is reversible");
    check(std::abs(frame.right.dot(frame.up))<1e-12&&std::abs(frame.outward.len()-1)<1e-12,"long drags retain an orthonormal frame");
    std::cout<<"Free orbit direction, signed multi-turn and pole-crossing checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
