#pragma once
#include "ark/core.hpp"
#include <algorithm>

namespace ark {
// Display-only free orbit. CAD coordinates are never modified.
struct CameraFrame { Vec3 outward{0,0,1},right{1,0,0},up{0,1,0}; };
inline CameraFrame cameraFrame(double yaw,double pitch,double roll=0){
    CameraFrame f;f.outward={std::cos(pitch)*std::sin(yaw),std::sin(pitch),std::cos(pitch)*std::cos(yaw)};
    Vec3 base{std::cos(yaw),0,-std::sin(yaw)},up=f.outward.cross(base);
    f.right=base*std::cos(roll)+up*std::sin(roll);f.up=f.outward.cross(f.right);return f;
}
inline CameraFrame rotateCameraFrame(CameraFrame frame,Vec3 axis,double angle){
    if(axis.lensq()<1e-24||angle==0)return frame;
    Quat rotation=Quat::fromAxisAngle(axis,angle);
    frame.outward=rotation.rotate(frame.outward).normalized();frame.right=rotation.rotate(frame.right).normalized();
    frame.up=frame.outward.cross(frame.right).normalized();frame.right=frame.up.cross(frame.outward).normalized();return frame;
}
inline CameraFrame orbitCameraPixels(CameraFrame frame,double dx,double dy,double radiansPerPixel,bool roll=false){
    // Inverse camera rotation makes the visible model follow the pointer.
    // Relative deltas have no virtual-sphere rim, pitch clamp or turn limit.
    if(roll)return rotateCameraFrame(frame,frame.outward,(dx-dy)*radiansPerPixel);
    double distance=std::hypot(dx,dy);if(distance==0)return frame;
    return rotateCameraFrame(frame,frame.up*dx+frame.right*dy,-distance*radiansPerPixel);
}
inline void cameraAngles(const CameraFrame& frame,double& yaw,double& pitch,double& roll){
    yaw=std::atan2(frame.outward.x,frame.outward.z);pitch=std::asin(std::clamp(frame.outward.y,-1.,1.));
    auto base=cameraFrame(yaw,pitch);roll=std::atan2(frame.right.dot(base.up),frame.right.dot(base.right));
}
}
