#pragma once
#include "ark/core.hpp"
#include <algorithm>

namespace ark {
// Display-only trackball orientation. CAD coordinates are never modified.
struct CameraFrame { Vec3 outward{0,0,1},right{1,0,0},up{0,1,0}; };
inline CameraFrame cameraFrame(double yaw,double pitch,double roll=0){
    CameraFrame f;f.outward={std::cos(pitch)*std::sin(yaw),std::sin(pitch),std::cos(pitch)*std::cos(yaw)};
    Vec3 base{std::cos(yaw),0,-std::sin(yaw)},up=f.outward.cross(base);
    f.right=base*std::cos(roll)+up*std::sin(roll);f.up=f.outward.cross(f.right);return f;
}
inline Vec3 trackballPoint(double x,double y){
    double length=x*x+y*y;if(length<1-1e-14)return {x,y,std::sqrt(1-length)};
    return Vec3{x,y,0}.normalized();
}
inline CameraFrame orbitTrackball(CameraFrame frame,Vec3 from,Vec3 to){
    Vec3 axis=from.cross(to);double sine=axis.len(),cosine=std::clamp(from.dot(to),-1.,1.);
    if(sine<1e-12)return frame;
    axis=axis*(1/sine);Vec3 world=frame.right*axis.x+frame.up*axis.y+frame.outward*axis.z;
    Quat rotation=Quat::fromAxisAngle(world,std::atan2(sine,cosine));
    frame.outward=rotation.rotate(frame.outward).normalized();frame.right=rotation.rotate(frame.right).normalized();
    frame.up=frame.outward.cross(frame.right).normalized();frame.right=frame.up.cross(frame.outward).normalized();return frame;
}
inline void cameraAngles(const CameraFrame& frame,double& yaw,double& pitch,double& roll){
    yaw=std::atan2(frame.outward.x,frame.outward.z);pitch=std::asin(std::clamp(frame.outward.y,-1.,1.));
    auto base=cameraFrame(yaw,pitch);roll=std::atan2(frame.right.dot(base.up),frame.right.dot(base.right));
}
}
