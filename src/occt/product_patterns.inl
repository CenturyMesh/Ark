// Authored, exact local-XY vents and timber stock. No sampled geometry selects cuts.
static V productPatternNative(const V& request){
    const auto op=word(request,"op");
    const std::set<std::string> common{"op","width","depth","height","density"};
    const std::set<std::string> vent{"rows","columns","pitch_x","pitch_y","opening_width","opening_depth","x","y","kind","input_brep","input_topology"};
    const std::set<std::string> wood{"grain_axis","joinery"};
    for(const auto& field:request.obj)if(!common.count(field.first)&&!(op=="vent"?vent:wood).count(field.first))throw std::runtime_error("Unknown "+op+" field: "+field.first);
    const double width=positive(request,"width"),depth=positive(request,"depth"),height=positive(request,"height");
    if(std::max({width,depth,height})>1e5)throw std::runtime_error("Pattern stock dimension exceeds 100000 mm");
    BRepPrimAPI_MakeBox box(width,depth,height);TopoDS_Shape result=box.Shape();std::vector<Ref> refs;
    add(refs,"x_min","face",box.BackFace());add(refs,"x_max","face",box.FrontFace());
    add(refs,"y_min","face",box.LeftFace());add(refs,"y_max","face",box.RightFace());
    add(refs,"z_min","face",box.BottomFace());add(refs,"z_max","face",box.TopFace());
    if(request.find("input_brep")){
        if(op!="vent")throw std::runtime_error("Wood stock has no source input");
        result=readBrep(word(request,"input_brep"));shapeReport(result,true);refs=inputRefs(request,"input_brep","input_topology",result);
        const auto extents=bounds(result);
        if(symmetricDifference(result,box.Shape())>1e-6)throw std::runtime_error("Vent source must be the declared pristine rectangular stock; use sheet.vent for panel-owned openings");
        const double expected[6]={0,0,0,width,depth,height};
        for(size_t i=0;i<6;++i)if(std::abs(extents[i]-expected[i])>1e-6)throw std::runtime_error("Vent input must occupy the declared local XY stock bounds");
    }
    V receipt=V::makeObj();receipt.set("operation",V::makeStr(op));receipt.set("manufacturing_qualified",V::makeBool(false));
    std::vector<TopoDS_Shape> tools;
    if(op=="vent"){
        const double nr=scalar(request,"rows"),nc=scalar(request,"columns");
        if(nr<1||nc<1||nr!=std::floor(nr)||nc!=std::floor(nc)||nr*nc>128)throw std::runtime_error("Vent grid needs integer rows/columns and at most 128 openings");
        const double a=positive(request,"opening_width"),b=positive(request,"opening_depth"),px=positive(request,"pitch_x"),py=positive(request,"pitch_y");
        const double x=scalar(request,"x"),y=scalar(request,"y");const auto kind=word(request,"kind");
        if(kind!="rectangle"&&kind!="circle"&&kind!="slot")throw std::runtime_error("Vent kind must be rectangle, circle or slot");
        if((nc>1&&px<=a)||(nr>1&&py<=b)||x-a*.5<=0||y-b*.5<=0||x+(nc-1)*px+a*.5>=width||y+(nr-1)*py+b*.5>=depth)throw std::runtime_error("Vent openings overlap or touch a stock edge");
        if(kind=="circle"&&a!=b)throw std::runtime_error("Circle opening width/depth must agree");
        if(kind=="slot"&&a<=b)throw std::runtime_error("Slot width must exceed its diameter");
        for(int row=0;row<(int)nr;++row)for(int col=0;col<(int)nc;++col){
            const double cx=x+col*px,cy=y+row*py;TopoDS_Shape tool;
            if(kind=="circle")tool=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(cx,cy,-1),gp_Dir(0,0,1)),a*.5,height+2).Shape();
            else if(kind=="rectangle")tool=BRepPrimAPI_MakeBox(gp_Pnt(cx-a*.5,cy-b*.5,-1),a,b,height+2).Shape();
            else{
                const double r=b*.5,s=(a-b)*.5;
                auto middle=BRepPrimAPI_MakeBox(gp_Pnt(cx-s,cy-r,-1),2*s,b,height+2).Shape();
                auto left=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(cx-s,cy,-1),gp_Dir(0,0,1)),r,height+2).Shape();
                auto right=BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(cx+s,cy,-1),gp_Dir(0,0,1)),r,height+2).Shape();
                BRepAlgoAPI_Fuse first(middle,left);if(!first.IsDone())throw std::runtime_error("Slot construction failed");
                BRepAlgoAPI_Fuse second(first.Shape(),right);if(!second.IsDone())throw std::runtime_error("Slot construction failed");tool=second.Shape();
            }tools.push_back(tool);
        }
        receipt.set("opening_count",V::makeNum(nr*nc));receipt.set("pattern_authority",V::makeStr("explicit local XY stock and openings"));
    }else{
        const auto grain=word(request,"grain_axis");if(grain!="x"&&grain!="y"&&grain!="z")throw std::runtime_error("Wood grain_axis must be x, y or z");
        receipt.set("grain_axis",V::makeStr(grain));receipt.set("stock_width_mm",V::makeNum(width));receipt.set("stock_depth_mm",V::makeNum(depth));receipt.set("stock_height_mm",V::makeNum(height));
        receipt.set("structural_checked",V::makeBool(false));
        if(const auto* joints=request.find("joinery")){
            if(joints->t!=V::Arr||joints->arr.size()>64)throw std::runtime_error("Wood joinery needs at most 64 rectangular pockets/grooves/rabbets");
            for(const auto& joint:joints->arr){
                if(joint.t!=V::Obj)throw std::runtime_error("Wood joint must be an object");
                for(const auto& field:joint.obj)if(field.first!="kind"&&field.first!="x"&&field.first!="y"&&field.first!="width"&&field.first!="depth"&&field.first!="cut_depth")throw std::runtime_error("Unknown joinery field: "+field.first);
                const auto kind=word(joint,"kind");if(kind!="pocket"&&kind!="groove"&&kind!="rabbet")throw std::runtime_error("Wood joint kind must be pocket, groove or rabbet");
                const double x=scalar(joint,"x"),y=scalar(joint,"y"),w=positive(joint,"width"),d=positive(joint,"depth"),cut=positive(joint,"cut_depth");
                if(x<0||y<0||x+w>width||y+d>depth||cut>=height)throw std::runtime_error("Wood joint exceeds stock or cuts through its thickness");
                if(kind=="pocket"&&(x==0||y==0||x+w==width||y+d==depth))throw std::runtime_error("Pocket must be inside the stock; use groove or rabbet at an edge");
                if(kind=="rabbet"&&x!=0&&y!=0&&x+w!=width&&y+d!=depth)throw std::runtime_error("Rabbet must meet a stock edge");
                tools.push_back(BRepPrimAPI_MakeBox(gp_Pnt(x,y,height-cut),w,d,cut+1).Shape());
            }receipt.set("joinery",*joints);
        }
    }
    const double before=properties(result);
    if(!tools.empty()){
        BRepAlgoAPI_Cut cut;TopTools_ListOfShape arguments,cutters;arguments.Append(result);for(const auto& tool:tools)cutters.Append(tool);
        cut.SetArguments(arguments);cut.SetTools(cutters);cut.Build();if(!cut.IsDone())throw std::runtime_error("Exact pattern cut failed");
        const auto next=cut.Shape();refs=propagated(refs,next,cut);result=next;
        if(!(properties(result)<before-1e-9))throw std::runtime_error("Pattern did not remove material");
    }
    // finish enforces a valid single solid with positive volume; no solids are discarded.
    V output=finish(result,refs),report=need(output,"report");receipt.set("removed_volume_mm3",V::makeNum(before-properties(result)));report.set(op,receipt);output.set("report",report);return output;
}
