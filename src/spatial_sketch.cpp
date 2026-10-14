#include "ark/spatial_sketch.hpp"
#include "ark/datum_authoring.hpp"
#include "ark/sketch.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
constexpr double residualTolerance=1e-7;
const V& field(const V& value,const char* key){const auto* item=value.find(key);if(!item)throw std::runtime_error(std::string("Spatial sketch missing ")+key);return *item;}
std::string text(const V& value,const char* key){const auto& item=field(value,key);if(item.t!=V::Str||item.str.empty())throw std::runtime_error(std::string("Spatial sketch needs text ")+key);return item.str;}
double scalar(const V& value,const ExprEnv& env){bool ok=value.t==V::Num;double result=ok?value.num:value.t==V::Str?evalExpr(value.str,env,&ok):0;if(!ok||!std::isfinite(result)||std::abs(result)>1e7)throw std::runtime_error("Invalid spatial sketch expression or coordinate");return result;}
V xyz(Vec3 point){V result=V::makeArr();for(double v:{point.x,point.y,point.z})result.arr.push_back(V::makeNum(v));return result;}
void erase(V& value,const char* key){value.obj.erase(std::remove_if(value.obj.begin(),value.obj.end(),[&](const auto& item){return item.first==key;}),value.obj.end());}
Vec3 unit(Vec3 v){const double length=v.len();if(length<1e-9)throw std::runtime_error("Spatial constraint has a degenerate direction; supply distinct initial points");return v/length;}
bool linearSolve(std::vector<std::vector<double>> a,std::vector<double> b,std::vector<double>& x){
    const size_t n=b.size();for(size_t column=0;column<n;++column){size_t pivot=column;for(size_t row=column+1;row<n;++row)if(std::abs(a[row][column])>std::abs(a[pivot][column]))pivot=row;if(std::abs(a[pivot][column])<1e-20)return false;std::swap(a[pivot],a[column]);std::swap(b[pivot],b[column]);
        for(size_t row=column+1;row<n;++row){double factor=a[row][column]/a[column][column];for(size_t k=column;k<n;++k)a[row][k]-=factor*a[column][k];b[row]-=factor*b[column];}}
    x.assign(n,0);for(size_t row=n;row-->0;){double value=b[row];for(size_t k=row+1;k<n;++k)value-=a[row][k]*x[k];x[row]=value/a[row][row];if(!std::isfinite(x[row]))return false;}return true;
}
size_t numericalRank(std::vector<std::vector<double>> matrix){
    if(matrix.empty())return 0;const size_t columns=matrix.front().size();size_t row=0;double scale=0;for(const auto& values:matrix)for(double value:values)scale=std::max(scale,std::abs(value));const double threshold=std::max(1e-10,scale*1e-8);
    for(size_t column=0;column<columns&&row<matrix.size();++column){size_t pivot=row;for(size_t r=row+1;r<matrix.size();++r)if(std::abs(matrix[r][column])>std::abs(matrix[pivot][column]))pivot=r;if(std::abs(matrix[pivot][column])<=threshold)continue;std::swap(matrix[pivot],matrix[row]);for(size_t r=row+1;r<matrix.size();++r){double ratio=matrix[r][column]/matrix[row][column];for(size_t c=column;c<columns;++c)matrix[r][c]-=ratio*matrix[row][c];}++row;}return row;
}
struct Point {
    std::string name,key;
    int segment=-1,pole=-1;
    std::array<bool,3> driven{};
};
struct Problem {
    const Document& doc;
    ExprEnv env;
    V native,constraints;
    std::vector<Point> points;
    std::map<std::string,size_t> names;
    std::vector<double> coordinates;
    std::vector<size_t> free;
    size_t authoredConstraints=0;

    explicit Problem(const V& source,const Document& document):doc(document),env(doc.buildEnv()),native(source){
        if(source.t!=V::Obj)throw std::runtime_error("Spatial sketch source must be an object");constraints=source.find("constraints")?*source.find("constraints"):V::makeArr();if(constraints.t!=V::Arr||constraints.arr.size()>128)throw std::runtime_error("Spatial sketch supports at most 128 constraints");erase(native,"constraints");
        authoredConstraints=constraints.arr.size();if(!native.find("start")&&!native.find("start_ref")){V origin=V::makeArr();for(int i=0;i<3;++i)origin.arr.push_back(V::makeStr("0"));native.set("start",origin);}
        addPoint(native,"start","start",-1,-1,"start_ref");V segments=field(native,"segments");if(segments.t!=V::Arr||segments.arr.empty()||segments.arr.size()>64)throw std::runtime_error("Spatial constrained curve needs 1..64 segments");
        std::string cursor="start";
        for(size_t i=0;i<segments.arr.size();++i){auto& segment=segments.arr[i];const auto prefix="segment."+std::to_string(i+1);const auto kind=text(segment,"kind");
            if(kind!="line"&&kind!="arc"&&kind!="nurbs"&&kind!="spline")throw std::runtime_error("Unknown spatial segment kind: "+kind);
            const bool threePoint=kind=="arc"&&segment.find("mode")&&segment.find("mode")->asStr()=="three_point";
            if((segment.find("through")||segment.find("through_ref"))&&!threePoint)throw std::runtime_error("Spatial through handle belongs only to a three-point arc");
            if(segment.find("points")&&kind!="spline")throw std::runtime_error("Spatial interpolation points belong only to a spline");
            if(segment.find("poles")&&kind!="nurbs")throw std::runtime_error("Spatial poles belong only to a NURBS segment");
            if(segment.find("length")&&kind!="line")throw std::runtime_error("Spatial length dimension belongs only to a line");
            addPoint(segment,"to",prefix+".end",(int)i,-1,"to_ref");addPoint(segment,"through",prefix+".through",(int)i,-1,"through_ref");
            std::string actualEnd;
            for(const auto* key:{"poles","points"})if(const auto* value=segment.find(key)){if(value->t!=V::Arr||value->arr.empty())throw std::runtime_error("Spatial controls must be a nonempty array");V controls=*value;const auto controlPrefix=prefix+(std::string(key)=="points"?".point.":".pole.");for(size_t p=0;p<controls.arr.size();++p){V wrapper=V::makeObj();wrapper.set(key,controls.arr[p]);addPoint(wrapper,key,controlPrefix+std::to_string(p+1),(int)i,(int)p,nullptr);controls.arr[p]=field(wrapper,key);}segment.set(key,controls);V continuity=V::makeObj();continuity.set("type",V::makeStr("coincident"));continuity.set("a",V::makeStr(cursor));continuity.set("b",V::makeStr(controlPrefix+"1"));constraints.arr.push_back(continuity);actualEnd=controlPrefix+std::to_string(controls.arr.size());if(names.count(prefix+".end")){continuity.set("a",V::makeStr(prefix+".end"));continuity.set("b",V::makeStr(actualEnd));constraints.arr.push_back(continuity);}else names[prefix+".end"]=names.at(actualEnd);}
            if(const auto* length=segment.find("length")){if(!names.count(prefix+".end"))throw std::runtime_error("Spatial line dimension needs an endpoint");V dimension=V::makeObj();dimension.set("type",V::makeStr("distance"));dimension.set("a",V::makeStr(cursor));dimension.set("b",V::makeStr(prefix+".end"));dimension.set("value",*length);constraints.arr.push_back(dimension);erase(segment,"length");}
            cursor=actualEnd.empty()?prefix+".end":actualEnd;
        }
        native.set("segments",segments);if(points.empty()||points.size()>64)throw std::runtime_error("Spatial sketch supports 1..64 point handles");
        std::set<std::string> ids;for(const auto& constraint:constraints.arr){if(constraint.t!=V::Obj)throw std::runtime_error("Spatial constraint must be an object");auto type=text(constraint,"type");std::set<std::string> allowed{"type","id"};
            if(type=="fixed")allowed.insert({"point","value"});else if(type=="coincident"||type=="distance")allowed.insert({"a","b"});else if(type=="on_plane")allowed.insert({"point","plane"});else if(type=="on_axis")allowed.insert({"point","axis"});else if(type=="parallel"||type=="perpendicular"||type=="angle"||type=="equal_length"||type=="coplanar")allowed.insert({"a","b","c","d"});else throw std::runtime_error("Unknown 3D constraint type: "+type);
            if(type=="distance"||type=="angle")allowed.insert("value");for(const auto& entry:constraint.obj)if(!allowed.count(entry.first))throw std::runtime_error("Unknown 3D constraint field: "+entry.first);
            if(const auto* id=constraint.find("id")){if(id->t!=V::Str||id->str.empty()||id->str.size()>128||!ids.insert(id->str).second)throw std::runtime_error("Spatial constraint IDs must be unique nonempty names");}
        }
        residual(coordinates); // Validate references and initial directions before iteration.
    }
    void addPoint(V& owner,const char* key,const std::string& name,int segment,int pole,const char* ref){
        bool anchored=false;if(ref)if(const auto* anchor=owner.find(ref)){owner.set(key,xyz(datumAnchorPoint(doc,*anchor)));erase(owner,ref);anchored=true;}
        const auto* value=owner.find(key);if(!value)return;if(value->t!=V::Arr||value->arr.size()!=3)throw std::runtime_error("Spatial point must contain three coordinates: "+name);Point point;point.name=name;point.key=key;point.segment=segment;point.pole=pole;V evaluated=V::makeArr();
        for(size_t axis=0;axis<3;++axis){double coordinate=scalar(value->arr[axis],env);point.driven[axis]=anchored||value->arr[axis].t==V::Str;if(!point.driven[axis])free.push_back(coordinates.size());coordinates.push_back(coordinate);evaluated.arr.push_back(V::makeNum(coordinate));}names[name]=points.size();points.push_back(point);owner.set(key,evaluated);
    }
    Vec3 point(const V& constraint,const char* key,const std::vector<double>& x)const {auto name=text(constraint,key);auto found=names.find(name);if(found==names.end()){std::string available;for(const auto& p:points){if(!available.empty())available+=", ";available+=p.name;}throw std::runtime_error("Unknown spatial point "+name+"; available handles: "+available);}size_t offset=found->second*3;return {x[offset],x[offset+1],x[offset+2]};}
    std::vector<double> residual(const std::vector<double>& x,std::vector<std::pair<size_t,size_t>>* spans=nullptr)const {
        std::vector<double> r;auto add=[&](Vec3 value){r.insert(r.end(),{value.x,value.y,value.z});};
        for(const auto& con:constraints.arr){size_t begin=r.size();auto type=text(con,"type");if(type=="fixed"){const auto& target=field(con,"value");if(target.t!=V::Arr||target.arr.size()!=3)throw std::runtime_error("Fixed spatial point needs value [x,y,z]");add(point(con,"point",x)-Vec3{scalar(target.arr[0],env),scalar(target.arr[1],env),scalar(target.arr[2],env)});}
            else if(type=="coincident")add(point(con,"a",x)-point(con,"b",x));
            else if(type=="distance"){double length=scalar(field(con,"value"),env);if(length<0)throw std::runtime_error("Spatial distance must be nonnegative");r.push_back((point(con,"a",x)-point(con,"b",x)).len()-length);}
            else if(type=="on_plane"||type=="on_axis"){auto name=text(con,type=="on_plane"?"plane":"axis");if(type=="on_axis"){auto datum=std::find_if(doc.planes.begin(),doc.planes.end(),[&](const DatumPlane& d){return d.name==name;});if(datum==doc.planes.end()||datum->kind!="axis")throw std::runtime_error("on_axis requires a named construction axis");}else if(name!="XY"&&name!="XZ"&&name!="YZ"){auto datum=std::find_if(doc.planes.begin(),doc.planes.end(),[&](const DatumPlane& d){return d.name==name;});if(datum==doc.planes.end()||datum->kind!="plane")throw std::runtime_error("on_plane requires a named construction plane");}auto frame=resolvePlane(doc,name);auto delta=point(con,"point",x)-frame.transformPoint({});if(type=="on_axis"){r.push_back(delta.dot(frame.transformDir({1,0,0})));r.push_back(delta.dot(frame.transformDir({0,1,0})));}else r.push_back(delta.dot(frame.transformDir({0,0,1})));}
            else{auto a=point(con,"a",x),b=point(con,"b",x),c=point(con,"c",x),d=point(con,"d",x);if(type=="equal_length")r.push_back((b-a).len()-(d-c).len());else if(type=="coplanar")r.push_back((d-a).dot(unit((b-a).cross(c-a))));else{auto u=unit(b-a),v=unit(d-c);if(type=="parallel")add(u.cross(v));else if(type=="perpendicular")r.push_back(u.dot(v));else{double angle=scalar(field(con,"value"),env);if(angle<=0||angle>=180)throw std::runtime_error("Spatial angle requires 0 < degrees < 180; use parallel for endpoint angles");r.push_back(u.dot(v)-std::cos(deg2rad(angle)));}}}
            if(spans)spans->push_back({begin,r.size()});
        }for(double value:r)if(!std::isfinite(value))throw std::runtime_error("Nonfinite spatial constraint residual");return r;
    }
    std::vector<std::vector<double>> jacobian(const std::vector<double>& x)const {
        auto base=residual(x);std::vector<std::vector<double>> result(base.size(),std::vector<double>(free.size()));
        for(size_t column=0;column<free.size();++column){const size_t slot=free[column],point=slot/3;double localScale=1.;
            for(size_t other=0;other<points.size();++other)if(other!=point){const double distance=Vec3{x[point*3]-x[other*3],x[point*3+1]-x[other*3+1],x[point*3+2]-x[other*3+2]}.len();if(distance>1e-9)localScale=std::min(localScale,distance);}
            const double floor=16*std::numeric_limits<double>::epsilon()*std::max(1.,std::abs(x[slot]));double step=std::max(floor,1e-5*localScale);bool differentiated=false;
            for(int attempt=0;attempt<12;++attempt){auto plus=x,minus=x;plus[slot]+=step;minus[slot]-=step;try{const auto upper=residual(plus),lower=residual(minus);const double width=plus[slot]-minus[slot];if(width<=0)throw std::runtime_error("Unrepresentable spatial finite difference");for(size_t row=0;row<base.size();++row)result[row][column]=(upper[row]-lower[row])/width;differentiated=true;break;}catch(const std::exception&){if(step<=floor)break;step=std::max(floor,step*.25);}}
            if(!differentiated)throw std::runtime_error("Spatial direction is numerically singular at this coordinate scale; source document unchanged");
        }return result;
    }
    V solved(const std::vector<double>& x)const {
        V result=native,segments=field(native,"segments");for(size_t i=0;i<points.size();++i){const auto& p=points[i];auto value=xyz({x[i*3],x[i*3+1],x[i*3+2]});if(p.segment<0)result.set(p.key,value);else if(p.pole<0)segments.arr[(size_t)p.segment].set(p.key,value);else{auto controls=field(segments.arr[(size_t)p.segment],p.key.c_str());controls.arr[(size_t)p.pole]=value;segments.arr[(size_t)p.segment].set(p.key,controls);}}result.set("segments",segments);return result;
    }
};
double square(const std::vector<double>& x){double result=0;for(double value:x)result+=value*value;return result;}
double maximum(const std::vector<double>& x){double result=0;for(double value:x)result=std::max(result,std::abs(value));return result;}
}
SpatialSketchSolution solveSpatialCurve(const V& authored,const Document& document){
    Problem problem(authored,document);auto x=problem.coordinates;double damping=1e-3;size_t iterations=0;auto residual=problem.residual(x);
    for(;iterations<100&&maximum(residual)>residualTolerance&&!problem.free.empty();++iterations){auto j=problem.jacobian(x);const size_t n=problem.free.size();std::vector<std::vector<double>> matrix(n,std::vector<double>(n));std::vector<double> gradient(n);
        for(size_t row=0;row<j.size();++row)for(size_t a=0;a<n;++a){gradient[a]-=j[row][a]*residual[row];for(size_t b=0;b<n;++b)matrix[a][b]+=j[row][a]*j[row][b];}
        for(size_t a=0;a<n;++a)matrix[a][a]+=damping*std::max(1.,matrix[a][a]);std::vector<double> delta;if(!linearSolve(matrix,gradient,delta)){damping*=10;continue;}bool accepted=false;
        for(double factor=1.;factor>=1./128;factor*=.5){auto trial=x;bool valid=true;for(size_t i=0;i<n;++i){trial[problem.free[i]]+=delta[i]*factor;valid&=std::isfinite(trial[problem.free[i]])&&std::abs(trial[problem.free[i]])<=1e7;}if(!valid)continue;try{auto next=problem.residual(trial);if(square(next)<square(residual)){x=std::move(trial);residual=std::move(next);accepted=true;break;}}catch(const std::exception&){} }
        damping=accepted?std::max(1e-12,damping*.25):std::min(1e12,damping*10);if(damping>=1e12)break;
    }
    if(maximum(residual)>residualTolerance){std::vector<std::pair<size_t,size_t>> spans;problem.residual(x,&spans);size_t worst=0;double value=0;for(size_t i=0;i<spans.size();++i)for(size_t r=spans[i].first;r<spans[i].second;++r)if(std::abs(residual[r])>value){worst=i;value=std::abs(residual[r]);}const auto& con=problem.constraints.arr[worst];throw std::runtime_error("3D sketch constraints conflict or do not converge at "+(con.find("id")?text(con,"id"):text(con,"type"))+"; maximum residual "+std::to_string(value)+"; source document unchanged");}
    V diagnostics=V::makeObj(),rows=V::makeArr(),handles=V::makeArr();auto j=problem.jacobian(x);size_t rank=numericalRank(j);std::vector<std::pair<size_t,size_t>> spans;problem.residual(x,&spans);for(size_t i=0;i<spans.size();++i){V row=V::makeObj();row.set("index",V::makeNum((double)i));if(const auto* id=problem.constraints.arr[i].find("id"))row.set("id",*id);row.set("type",field(problem.constraints.arr[i],"type"));row.set("implicit",V::makeBool(i>=problem.authoredConstraints));double error=0;for(size_t r=spans[i].first;r<spans[i].second;++r)error=std::max(error,std::abs(residual[r]));row.set("maximum_residual",V::makeNum(error));row.set("satisfied",V::makeBool(error<=residualTolerance));rows.arr.push_back(row);}
    for(size_t i=0;i<problem.points.size();++i){V row=V::makeObj();row.set("name",V::makeStr(problem.points[i].name));row.set("point_mm",xyz({x[i*3],x[i*3+1],x[i*3+2]}));handles.arr.push_back(row);}
    diagnostics.set("constraints",rows);diagnostics.set("handles",handles);diagnostics.set("variables",V::makeNum((double)problem.free.size()));diagnostics.set("rank",V::makeNum((double)rank));diagnostics.set("free_dofs",V::makeNum((double)(problem.free.size()-rank)));diagnostics.set("iterations",V::makeNum((double)iterations));diagnostics.set("maximum_residual",V::makeNum(maximum(residual)));diagnostics.set("converged",V::makeBool(true));diagnostics.set("rank_scope",V::makeStr("Local numerical equality Jacobian at solved point coordinates; no global uniqueness or curve-surface intersection proof"));diagnostics.set("method",V::makeStr("damped_least_squares_central_difference"));diagnostics.set("tolerance",V::makeNum(residualTolerance));return {problem.solved(x),diagnostics};
}
}
