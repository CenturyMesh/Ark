// Area of one trimmed face by piecewise adaptive Gauss-Kronrod quadrature.
// Included privately by common.cpp; properties(shape,'a') uses it only for a
// shape whose BRepGProp estimate missed the fixed area precision.
//
// BRepGProp_Gauss cannot converge on some valid faces, and its estimate is not
// an error bound there (or elsewhere):
// - the outer integral along an edge is split at the knots of the p-curve, not
//   where the p-curve crosses a knot line of the support, and a straight
//   p-curve gets at most 33 subintervals. The area density of an offset of a C1
//   basis jumps at those knot lines, so the subintervals run out;
// - the inner integral uses one fixed Gauss rule per interval, a single 9-point
//   one for offset, extruded and revolved supports, and is never refined;
// - the inner error is cleared before it reaches the reported estimate.
// Here both integrals are adaptive, every loss of smoothness of the support and
// of the p-curves is an interval boundary, and the inner error estimates are
// integrated by the outer rule. |K15-G7| measures the lower-order rule, so it
// overestimates the error of the published Kronrod value on each smooth piece.

namespace facearea {
constexpr double kNodes[8]={.991455371120812639206854697526329,.949107912342758524526189684047851,.864864423359769072789712788640926,.741531185599394439863864773280788,.586087235467691130294144691693013,.405845151377397166906606412076961,.207784955007898467600689403773245,0};
constexpr double kKronrod[8]={.022935322010529224963732008058970,.063092092629978553290700663189204,.104790010322250183839876322541518,.140653259715525918745189590510238,.169004726639267902826583426598550,.190350578064785409913256402421014,.204432940075298892414161999234649,.209482141084727828012999174891714};
constexpr double kGauss[4]={.129484966168869693270611432679082,.279705391489276667901467771423780,.381830050505118944950369775488975,.417959183673469387755102040816327};
constexpr double kRoundoff=50*std::numeric_limits<double>::epsilon();

struct Estimate {double value=0,error=0;};
struct Piece {double a=0,b=0;size_t edge=0;Estimate estimate;};

// Kronrod value, the |K15-G7| difference and the integrated error of f itself.
template<class F>Estimate kronrod15(F&& f,double a,double b){
    const double center=.5*(a+b),half=.5*(b-a);const Estimate middle=f(center);
    double kronrod=kKronrod[7]*middle.value,gauss=kGauss[3]*middle.value,inherited=kKronrod[7]*middle.error;
    for(int i=0;i<7;++i){
        const Estimate low=f(center-half*kNodes[i]),high=f(center+half*kNodes[i]);
        kronrod+=kKronrod[i]*(low.value+high.value);inherited+=kKronrod[i]*(low.error+high.error);
        if(i%2)gauss+=kGauss[i/2]*(low.value+high.value);
    }
    return {kronrod*half,std::abs((kronrod-gauss)*half)+inherited*std::abs(half)};
}
// Deterministic worst-first bisection. It ends at the target, at the rounding
// floor of the summed pieces, or at the evaluation and piece limits; the error
// reached is always reported, so the caller rejects an unresolved integral.
template<class Rule>Estimate refine(std::vector<Piece>& pieces,Rule&& rule,double target,size_t limit,const long long& remaining){
    for(auto& piece:pieces)piece.estimate=rule(piece);
    for(;;){
        Estimate sum;double magnitude=0;size_t worst=0;
        for(size_t i=0;i<pieces.size();++i){
            const auto& estimate=pieces[i].estimate;sum.value+=estimate.value;sum.error+=estimate.error;magnitude+=std::abs(estimate.value);
            if(estimate.error>pieces[worst].estimate.error)worst=i;
        }
        const double floor=kRoundoff*magnitude;
        if(pieces.empty()||!std::isfinite(sum.error)||sum.error<=std::max(target,2*floor)||pieces.size()>=limit||remaining<=0)return {sum.value,sum.error+floor};
        Piece left=pieces[worst],right=left;left.b=right.a=.5*(left.a+left.b);
        if(left.b>left.a&&right.b>right.a){left.estimate=rule(left);right.estimate=rule(right);}
        else return {sum.value,sum.error+floor};
        if(!std::isfinite(left.estimate.error)||!std::isfinite(right.estimate.error))return {sum.value,sum.error+floor}; // Keep the finite bound already found.
        pieces[worst]=left;pieces.push_back(right);
    }
}

class Integrator {
    struct Edge {std::unique_ptr<BRepAdaptor_Curve2d> curve;double sign=1;};
    BRepAdaptor_Surface surface_;
    long long& remaining_;
    double u1_,u2_,v1_,v2_,innerTarget_=0;
    std::vector<double> uBreaks_,vBreaks_,spans_;
    std::vector<Edge> edges_;
    std::vector<Piece> inner_;

    static std::vector<double> interior(const NCollection_Array1<double>& knots,double first,double last){
        std::vector<double> out;for(int i=knots.Lower();i<=knots.Upper();++i)if(knots(i)>first&&knots(i)<last&&(out.empty()||knots(i)>out.back()))out.push_back(knots(i));return out;
    }
    // A lone span is halved so that no integral rests on a single rule.
    static void appendPieces(std::vector<Piece>& pieces,const std::vector<double>& limits,size_t edge){
        for(size_t i=0;i+1<limits.size();++i)if(limits[i+1]>limits[i]){
            if(limits.size()>2)pieces.push_back({limits[i],limits[i+1],edge,{}});
            else {const double middle=.5*(limits[i]+limits[i+1]);pieces.push_back({limits[i],middle,edge,{}});pieces.push_back({middle,limits[i+1],edge,{}});}
        }
    }
    // Integral of |dS/du x dS/dv| from u1 to u at constant v.
    Estimate inner(double u,double v){
        if(remaining_<=0)return {0,std::numeric_limits<double>::infinity()};
        spans_.assign(1,u1_);for(double knot:uBreaks_)if(knot<u)spans_.push_back(knot);spans_.push_back(u);
        inner_.clear();appendPieces(inner_,spans_,0);
        return refine(inner_,[&](const Piece& piece){
            remaining_-=15;
            return kronrod15([&](double s){gp_Pnt point;gp_Vec du,dv;surface_.D1(s,v,point,du,dv);return Estimate{du.Crossed(dv).Magnitude(),0};},piece.a,piece.b);
        },innerTarget_,256,remaining_);
    }
    Estimate boundary(const Edge& edge,double t){
        gp_Pnt2d at(u2_,t);gp_Vec2d tangent(0,1);if(edge.curve)edge.curve->D1(t,at,tangent);
        // The same clamped restriction to the face UV box as BRepGProp.
        const double u=std::clamp(at.X(),u1_,u2_),v=std::clamp(at.Y(),v1_,v2_),rate=tangent.Y()*edge.sign;
        if(!std::isfinite(u)||!std::isfinite(v)||!std::isfinite(rate))return {0,std::numeric_limits<double>::infinity()};
        if(rate==0||u<=u1_)return {};
        const Estimate line=inner(u,v);return {line.value*rate,line.error*std::abs(rate)};
    }
    // P-curve spans split where they cross a knot line of the support. Returns
    // the sampled total variation of v. A tangential crossing that the samples
    // miss is left to the bisection.
    double splitEdge(const BRepAdaptor_Curve2d& curve,std::vector<double>& limits)const{
        NCollection_Array1<double> knots(1,curve.NbIntervals(GeomAbs_CN)+1);curve.Intervals(knots,GeomAbs_CN);
        for(int i=knots.Lower();i<=knots.Upper();++i)limits.push_back(knots(i));
        const size_t count=limits.size();double variation=0;constexpr int samples=16;
        auto crossings=[&](const std::vector<double>& lines,double a,double b,double from,double to,auto coordinate){
            for(auto line=std::lower_bound(lines.begin(),lines.end(),std::min(from,to));line!=lines.end()&&*line<=std::max(from,to);++line){
                if(*line==from||*line==to){limits.push_back(*line==from?a:b);continue;}
                double low=a,high=b;
                for(int i=0;i<64;++i){const double middle=.5*(low+high);if(!(middle>low&&middle<high))break;if((coordinate(curve.Value(middle))<*line)==(from<*line))low=middle;else high=middle;}
                limits.push_back(.5*(low+high));
            }
        };
        for(size_t i=0;i+1<count;++i){
            double a=limits[i];gp_Pnt2d from=curve.Value(a);
            for(int j=1;j<=samples;++j){
                const double b=limits[i]+(limits[i+1]-limits[i])*j/samples;const gp_Pnt2d to=curve.Value(b);
                crossings(uBreaks_,a,b,from.X(),to.X(),[](const gp_Pnt2d& p){return p.X();});
                crossings(vBreaks_,a,b,from.Y(),to.Y(),[](const gp_Pnt2d& p){return p.Y();});
                variation+=std::abs(to.Y()-from.Y());a=b;from=to;
            }
        }
        std::sort(limits.begin(),limits.end());limits.erase(std::unique(limits.begin(),limits.end()),limits.end());return variation;
    }
public:
    Integrator(const TopoDS_Face& face,long long& remaining):surface_(TopoDS::Face(face.Oriented(TopAbs_FORWARD))),remaining_(remaining),
        u1_(surface_.FirstUParameter()),u2_(surface_.LastUParameter()),v1_(surface_.FirstVParameter()),v2_(surface_.LastVParameter()){}
    // Signed like BRepGProp: positive for an outward boundary of the face.
    Estimate area(double target){
        constexpr double unresolved=std::numeric_limits<double>::infinity();
        const TopoDS_Face face=surface_.Face();
        for(double bound:{u1_,u2_,v1_,v2_})if(!std::isfinite(bound)||Precision::IsInfinite(bound))return {0,unresolved};
        {NCollection_Array1<double> knots(1,surface_.NbUIntervals(GeomAbs_CN)+1);surface_.UIntervals(knots,GeomAbs_CN);uBreaks_=interior(knots,u1_,u2_);}
        {NCollection_Array1<double> knots(1,surface_.NbVIntervals(GeomAbs_CN)+1);surface_.VIntervals(knots,GeomAbs_CN);vBreaks_=interior(knots,v1_,v2_);}
        std::vector<Piece> pieces;double variation=0;
        if(face.NbChildren()==0){ // Natural restriction: the whole UV box.
            std::vector<double> limits{v1_};limits.insert(limits.end(),vBreaks_.begin(),vBreaks_.end());limits.push_back(v2_);
            edges_.emplace_back();appendPieces(pieces,limits,0);variation=v2_-v1_;
        }else for(TopExp_Explorer item(face.Oriented(TopAbs_FORWARD),TopAbs_EDGE);item.More();item.Next()){
            const auto& edge=TopoDS::Edge(item.Current());
            if(edge.Orientation()!=TopAbs_FORWARD&&edge.Orientation()!=TopAbs_REVERSED)continue;
            double first=0,last=0;
            if(BRep_Tool::CurveOnSurface(edge,face,first,last).IsNull()||!std::isfinite(first)||!std::isfinite(last)||Precision::IsInfinite(first)||Precision::IsInfinite(last))return {0,unresolved};
            edges_.push_back({std::make_unique<BRepAdaptor_Curve2d>(edge,face),edge.Orientation()==TopAbs_REVERSED?-1.:1.});
            std::vector<double> limits;variation+=splitEdge(*edges_.back().curve,limits);appendPieces(pieces,limits,edges_.size()-1);
        }
        // A quarter of the target for the integrated inner errors.
        innerTarget_=variation>0?.25*target/variation:0;
        return refine(pieces,[&](const Piece& piece){return kronrod15([&](double t){return boundary(edges_[piece.edge],t);},piece.a,piece.b);},target,8192,remaining_);
    }
};
}
