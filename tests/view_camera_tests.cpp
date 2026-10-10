#include "ark/view_camera.hpp"
#include <iostream>
#include <stdexcept>
using namespace ark;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    auto frame=cameraFrame(.7,.8);auto original=frame;
    for(int i=0;i<360;++i){double a=i*K_PI/180,b=(i+1)*K_PI/180;frame=orbitTrackball(frame,trackballPoint(std::cos(a),std::sin(a)),trackballPoint(std::cos(b),std::sin(b)));}
    check((frame.right-original.right).len()<1e-10&&(frame.outward-original.outward).len()<1e-10,"360 degree roll returns to original orientation");
    frame=cameraFrame(0,0);for(int i=0;i<4;++i)frame=orbitTrackball(frame,trackballPoint(0,0),trackballPoint(0,1));
    check((frame.outward-Vec3{0,0,1}).len()<1e-10,"four quarter orbits cross both poles without clamping");
    for(int i=0;i<1000;++i){frame=orbitTrackball(frame,trackballPoint(.1,.2),trackballPoint(.13,.18));double yaw,pitch,roll;cameraAngles(frame,yaw,pitch,roll);auto restored=cameraFrame(yaw,pitch,roll);check((restored.right-frame.right).len()<1e-10&&(restored.outward-frame.outward).len()<1e-10,"rendered and picked camera frames agree");}
    check(std::abs(frame.right.dot(frame.up))<1e-12&&std::abs(frame.outward.len()-1)<1e-12,"long drags retain an orthonormal frame");
    std::cout<<"Free trackball camera checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
