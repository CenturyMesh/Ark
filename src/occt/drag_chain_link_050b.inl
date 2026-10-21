// 050B drag chain link: one exact solid in link-local coordinates. X runs from the rear pin
// (x=0) to the front pin (x=pitch), Y across the width, Z across the height, centred on the
// pin line. Each side wall has two plate layers of t=(W-Wi)/4: the rear lobe and neck use the
// outer layer, the front lobe and neck the inner layer (0.1 mm running clearance), so the
// next link's rear lobe nests outside this link's front lobe. A full-height centre plate
// spans both layers over 0.4*pitch and carries the two crossbars that close the cable space.
TopoDS_Shape dragChainLink050b(const V& request){
    for(const auto& field:request.obj)if(field.first!="op"&&field.first!="pitch"&&field.first!="outer_width"&&field.first!="outer_height"&&field.first!="inner_width"&&field.first!="inner_height")throw std::runtime_error("Unknown drag_chain_link field: "+field.first);
    const double p=positive(request,"pitch"),w=positive(request,"outer_width"),h=positive(request,"outer_height"),wi=positive(request,"inner_width"),hi=positive(request,"inner_height");
    if(p>1000||w>1000||h>1000)throw std::runtime_error("drag_chain_link dimensions are limited to 1000 mm");
    const double t=(w-wi)/4,bar=(h-hi)/2;if(wi>=w||hi>=h||t<0.25||bar<0.25)throw std::runtime_error("drag_chain_link needs (outer_width-inner_width)/4 and (outer_height-inner_height)/2 of at least 0.25 mm");
    const double gap=std::min(0.1,0.25*t),lobe=std::min(0.28*p,0.45*h),y=w/2;
    auto box=[](double x,double yy,double z,double dx,double dy,double dz){return BRepPrimAPI_MakeBox(gp_Pnt(x,yy,z),dx,dy,dz).Shape();};
    auto pin=[](double x,double yy,double r,double length){return BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(x,yy,0),gp_Dir(0,1,0)),r,length).Shape();};
    std::vector<TopoDS_Shape> parts;
    for(double side:{1.,-1.}){
        const double outer=side>0?y-t:-y,inner=side>0?y-2*t:-y+t+gap; // lower Y bound of each layer
        parts.push_back(box(.3*p,side>0?y-2*t:-y,-h/2,.4*p,2*t,h));               // centre plate, both layers
        parts.push_back(box(0,outer,-lobe,.5*p,t,2*lobe));parts.push_back(pin(0,outer,lobe,t));                // rear neck and lobe, outer layer
        parts.push_back(box(.5*p,inner,-lobe,.5*p,t-gap,2*lobe));parts.push_back(pin(p,inner,lobe,t-gap));      // front neck and lobe, inner layer
    }
    parts.push_back(box(.3*p,-(y-t),hi/2,.4*p,w-2*t,bar));parts.push_back(box(.3*p,-(y-t),-h/2,.4*p,w-2*t,bar)); // crossbars
    TopoDS_Shape result=parts.front();
    for(size_t i=1;i<parts.size();++i){BRepAlgoAPI_Fuse algorithm(result,parts[i]);if(!algorithm.IsDone()||algorithm.HasErrors())throw std::runtime_error("drag_chain_link union failed");result=algorithm.Shape();}
    requireValidIntermediate(result);return result;
}
