#include "ark/manufacturing.hpp"
#include "ark/dfm.hpp"
#include "ark/history_authoring.hpp"
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace ark {
namespace {
using V=arkjson::Value;
enum Kind { Text, Positive, Percent, Draft, Count, Direction, PositiveVector, Position, Choice };
struct Field {const char* name;Kind kind;const char* choices="";};
const std::vector<Field> common{
    {"material_grade",Text},{"tolerance_mm",Positive},{"load_case",Text},{"finish",Text}
};
const std::vector<Field> injection{
    {"nominal_wall_mm",Positive},{"min_draft_deg",Draft},{"pull_direction",Direction},
    {"parting_line",Text},{"undercut_strategy",Choice,"none|side_action|insert|redesign"},
    {"gate_plan",Text},{"ejection_plan",Text},{"shrinkage_percent",Percent}
};
const std::vector<Field> additive{
    {"method",Choice,"fdm"},{"build_direction",Direction},{"nozzle_diameter_mm",Positive},
    {"layer_height_mm",Positive},{"line_width_mm",Positive},{"perimeters",Count},
    {"top_layers",Count},{"bottom_layers",Count},{"infill_percent",Percent},
    {"support_strategy",Choice,"none|build_plate|everywhere|soluble|custom"},
    {"filament_diameter_mm",Positive},{"slicer_profile",Text},{"build_volume_mm",PositiveVector}
};
const std::vector<Field> machining{
    {"stock_size_mm",PositiveVector},{"stock_origin_mm",Position},{"tool_axis",Direction},
    {"cutter_diameter_mm",Positive},{"minimum_internal_radius_mm",Positive},
    {"workholding_plan",Text},{"setup_plan",Text}
};
const std::vector<Field> sheet{
    {"cutting_method",Choice,"laser|other"},{"laser_max_thickness_mm",Positive},
    {"minimum_bend_radius_mm",Positive},{"process_reference",Text}
};
const std::vector<Field> frame{
    {"minimum_weld_throat_mm",Positive},{"welding_procedure",Text},{"process_reference",Text}
};
const std::vector<Field>& fields(const std::string& route){
    if(route=="injection_molding")return injection;
    if(route=="additive")return additive;
    if(route=="machined")return machining;
    if(route=="sheet_metal")return sheet;
    if(route=="frame")return frame;
    static const std::vector<Field> empty;return empty;
}
bool allowed(const std::string& value,const char* choices){
    const std::string haystack="|"+std::string(choices)+"|";return haystack.find("|"+value+"|")!=std::string::npos;
}
bool validField(const V& value,const Field& spec){
    if(spec.kind==Text||spec.kind==Choice)return value.t==V::Str&&!value.str.empty()&&value.str.size()<=1024&&(spec.kind!=Choice||allowed(value.str,spec.choices));
    if(spec.kind==Direction||spec.kind==PositiveVector||spec.kind==Position){
        if(value.t!=V::Arr||value.arr.size()!=3)return false;
        double square=0;for(const auto& item:value.arr){if(item.t!=V::Num||!std::isfinite(item.num)||std::abs(item.num)>1e6||(spec.kind==PositiveVector&&item.num<=0))return false;square+=item.num*item.num;}
        return spec.kind!=Direction||square>1e-18;
    }
    if(value.t!=V::Num||!std::isfinite(value.num))return false;
    if(spec.kind==Percent)return value.num>=0&&value.num<=100;
    if(spec.kind==Draft)return value.num>=0&&value.num<90;
    if(spec.kind==Count)return value.num>=1&&value.num<=1000&&value.num==std::floor(value.num);
    return value.num>0&&value.num<=1e6;
}
void add(V& list,const std::string& text){list.arr.push_back(V::makeStr(text));}
struct Report {
    V row=V::makeObj(),requirements=V::makeArr(),blockers=V::makeArr(),unknowns=V::makeArr(),evidence=V::makeArr(),policy=V::makeArr();
    bool parametersComplete=true;
    void requirement(const char* id,const char* status,const char* guidance,const V* value=nullptr){
        V r=V::makeObj();r.set("id",V::makeStr(id));r.set("status",V::makeStr(status));r.set("guidance",V::makeStr(guidance));
        r.set("evidence_authority",V::makeStr(std::string(status)=="verified"?"evaluated_exact_brep":std::string(status)=="declared"?"caller_declared":"unverified"));
        if(value)r.set("value",*value);requirements.arr.push_back(r);
        if(std::string(status)=="unknown")add(unknowns,id);if(std::string(status)=="blocked")add(blockers,id);
    }
    void declaration(const V& process,const char* field,const char* guidance){
        const auto* value=process.find(field);requirement(field,value?"declared":"unknown",guidance,value);if(!value)parametersComplete=false;
    }
    void pending(const char* id,const char* guidance){requirement(id,"unknown",guidance);}
};
const Body* evaluated(const Document& document,u64 id){
    for(const auto& body:document.evalBodies)if(body.id==id)return &body;return nullptr;
}
bool checkedSolid(const Body* body,V& geometry){
    const auto* brep=body?body->nativeData.find("brep_base64"):nullptr;
    const auto* report=body?body->nativeData.find("report"):nullptr;
    const bool exact=brep&&brep->t==V::Str&&!brep->str.empty();
    geometry=V::makeObj();geometry.set("exact_geometry",V::makeBool(exact));geometry.set("display_mesh_authoritative",V::makeBool(false));
    if(report)for(const char* key:{"kernel","kernel_version","brep_valid","solids","volume_mm3","bounds_mm","flat_pattern_qualified","forming_strain_checked","self_intersections_checked"})if(auto value=report->find(key))geometry.set(key,*value);
    const auto* valid=geometry.find("brep_valid");const auto* solids=geometry.find("solids");const auto* volume=geometry.find("volume_mm3");
    return exact&&valid&&valid->t==V::Bool&&valid->b&&solids&&solids->t==V::Num&&solids->num==1&&volume&&volume->t==V::Num&&std::isfinite(volume->num)&&volume->num>0;
}
V inspectComponent(const Document& document,const Feature& feature){
    Report r;V linked=V::makeArr();for(const auto& plan:document.manufacturingPlans.arr)if(const auto* nodes=plan.find("nodes"))for(const auto& node:nodes->arr){const auto* kind=node.find("kind"),*data=node.find("data");if(kind&&kind->str=="part"&&data&&data->find("feature")&&data->find("feature")->num==(double)feature.id){V link=V::makeObj();link.set("name",*plan.find("name"));link.set("process",*plan.find("process"));link.set("source_sha256",*data->find("source_sha256"));link.set("next_action",V::makeStr("manufacturing_plan inspect at the current revision checks source freshness; manufacturing_simulate supplies bounded evidence."));linked.arr.push_back(link);}}if(!linked.arr.empty())r.row.set("linked_manufacturing_plans",linked);
    const auto& route=feature.manufacturingRoute;const auto& process=feature.manufacturingProcess;
    r.row.set("id",V::makeNum((double)feature.id));r.row.set("name",V::makeStr(feature.name));r.row.set("declared_route",V::makeStr(route.empty()?"undeclared":route));
    const auto lineage=featureManufacturingLineage(document,feature);r.row.set("geometry_route",V::makeStr(lineage.geometryRoute));r.row.set("process",process.t==V::Obj?process:V::makeObj());
    if(lineage.origin)r.row.set("origin",V::makeNum((double)lineage.origin));
    V geometry;const Body* body=evaluated(document,feature.id);const bool solid=checkedSolid(body,geometry);r.row.set("geometry",geometry);
    std::string error;const bool validProcess=validateManufacturingProcess(feature,error);if(!validProcess)r.requirement("process_schema","blocked",error.c_str());
    if(feature.suppressed)r.requirement("active_geometry","blocked","Suppressed features have no active fabrication body.");
    if(route.empty()||route=="unspecified"){
        r.requirement("fabrication_route","unknown","Declare the intended route from function, material and production method; geometry or appearance cannot choose it.");r.parametersComplete=false;
    }else{
        const V declared=V::makeStr(route);r.requirement("fabrication_route","declared","An explicit route is design intent, not a process qualification.",&declared);
        if(route=="frame"){
            auto exact=geometry.find("exact_geometry"),valid=geometry.find("brep_valid"),solids=geometry.find("solids");
            const bool frameGeometry=exact&&exact->b&&valid&&valid->t==V::Bool&&valid->b&&solids&&solids->t==V::Num&&solids->num>=1;
            r.requirement("exact_frame_geometry",frameGeometry?"verified":"blocked","Frame members require current valid exact B-reps; multiple members may remain multiple solids.");
        }else if(route!="purchased")r.requirement("exact_single_solid",solid?"verified":"blocked","Require the current evaluated exact B-rep, valid topology and one positive-volume solid. Display meshes and authoring type alone cannot establish this.");
        if((route=="machined"||route=="injection_molding"||route=="additive")&&lineage.geometryRoute!="solid")r.requirement("solid_authoring_lineage","blocked","This fabrication route requires exact solid authoring, not legacy or sampled display geometry.");
        if(!feature.material.empty()){const V value=V::makeStr(feature.material);V e=V::makeObj();e.set("id",V::makeStr("document_material"));e.set("value",value);e.set("authority",V::makeStr("caller_declared"));r.evidence.arr.push_back(e);}
        if(route=="injection_molding"||route=="additive"||route=="machined"){
            r.declaration(process,"material_grade","Select a process-compatible supplier grade, including additives and operating environment.");
            r.declaration(process,"tolerance_mm","Declare required dimensional tolerance; CAD precision is not the process capability.");
            r.declaration(process,"load_case","Specify loads, constraints, temperatures and functional interfaces before reducing material.");
        }
        if(route=="injection_molding"){
            r.declaration(process,"nominal_wall_mm","Design consistent nominal walls and gradual transitions; core out bulky sections and size ribs/bosses for the selected resin.");
            r.declaration(process,"pull_direction","Declare the mold opening direction in feature-local coordinates.");
            r.declaration(process,"min_draft_deg","Set resin/texture/depth-specific draft for surfaces in the pull direction; a declared target is not a measured draft analysis.");
            r.declaration(process,"parting_line","Define an actual mold split and surfaces to review.");
            r.declaration(process,"undercut_strategy","Review all trapped geometry; use an explicit side action/insert or redesign where required.");
            r.declaration(process,"gate_plan","Plan gate positions, flow/weld lines, cosmetic vestige and filling balance with the mold supplier.");
            r.declaration(process,"ejection_plan","Plan ejection support, pin locations and removal without damaging functional surfaces.");
            r.declaration(process,"shrinkage_percent","Use the chosen resin and tooling process data; do not scale the finished CAD part by an invented shrinkage factor.");
            r.pending("wall_distribution_checked","Exact local wall distribution, rib/boss transitions and sink/warpage risk have not been evaluated.");
            r.pending("draft_and_undercuts_checked","An authored draft operation or target does not prove all faces can leave the declared mold.");
            r.pending("mold_flow_and_tooling_checked","Supplier flow/cooling, gate/ejection, tooling feasibility and shrinkage validation are required.");
            add(r.policy,"Keep the finished part as the dimensional authority. Resin-dependent mold compensation belongs to an explicit tooling design.");
            add(r.policy,"Use uniform walls, cored masses, rounded transitions and deliberate ribs/bosses; review every face against pull direction and mold split.");
            add(r.policy,"FDM infill, nozzle settings and filament estimates cannot establish injection moldability.");
        }else if(route=="additive"){
            for(const auto& field:additive)r.declaration(process,field.name,
                field.kind==Direction?"Declare orientation in feature-local coordinates against load paths, layer adhesion and support removal.":
                field.kind==Count?"Declare slicer shell/layer count from required strength and skin thickness, without assuming infill replaces walls.":
                "Declare the actual FDM printer, material and slicer setting; review compatibility and machine limits.");
            if(validProcess)if(auto layer=process.find("layer_height_mm"))if(auto nozzle=process.find("nozzle_diameter_mm"))if(layer->num>=nozzle->num)r.requirement("fdm_layer_nozzle_compatibility","blocked","The supported FDM planning domain requires layer height smaller than nozzle diameter. Use the printer/profile capability for the actual limit.");
            V configured=V::makeObj();if(validProcess)if(auto width=process.find("line_width_mm"))if(auto count=process.find("perimeters"))configured.set("nominal_perimeter_band_mm",V::makeNum(width->num*count->num));
            if(validProcess)if(auto layer=process.find("layer_height_mm"))for(const char* key:{"top_layers","bottom_layers"})if(auto count=process.find(key))configured.set(std::string(key)=="top_layers"?"nominal_top_skin_mm":"nominal_bottom_skin_mm",V::makeNum(layer->num*count->num));
            configured.set("authority",V::makeStr("caller_settings_arithmetic_not_measured_wall"));r.row.set("configured_shell",configured);
            V filament=V::makeObj();filament.set("actual_consumption_checked",V::makeBool(false));filament.set("actual_filament_length_mm",V{});
            if(solid&&validProcess)if(auto diameter=process.find("filament_diameter_mm")){filament.set("solid_equivalent_length_mm",V::makeNum(geometry.find("volume_mm3")->num/(K_PI*diameter->num*diameter->num/4)));filament.set("basis",V::makeStr("exact_CAD_volume_divided_by_declared_filament_area; fully_solid_equivalent_only"));}
            filament.set("excludes",V::makeStr("slicer infill/shell toolpaths, support, brim/raft, purge, extrusion calibration and waste"));r.row.set("filament_budget",filament);
            r.pending("layer_strength_checked","Evaluate the declared load case in the chosen layer orientation with the material/process data; isotropic CAD material is insufficient.");
            r.pending("overhang_support_and_build_fit_checked","Orientation-specific overhangs, bridging, support removability and machine/build-volume fit have not been checked.");
            r.pending("slicer_consumption_checked","Slice the exact design with the declared profile and compare shell/infill/support/purge consumption. No actual filament saving is certified from CAD volume or infill percent.");
            add(r.policy,"Orient against load direction and layer adhesion, reduce inaccessible supports, and choose nozzle/layer/line widths for the smallest functional details.");
            add(r.policy,"Preserve critical walls, mounting interfaces and load paths; compare slicer consumption while changing infill and support rather than hollowing without strength evidence.");
            add(r.policy,"A printable cavity or undercut does not need the mold's pull direction; injection draft/gating/shrinkage checks belong to the injection route.");
        }else if(route=="machined"){
            for(const auto& field:machining)r.declaration(process,field.name,"Declare stock and local frame, cutter/access direction, workholding and setup order against actual machine capability.");
            if(validProcess)if(auto radius=process.find("minimum_internal_radius_mm"))if(auto cutter=process.find("cutter_diameter_mm"))if(radius->num<cutter->num/2)r.requirement("declared_cutter_corner_fit","blocked","The declared internal radius is smaller than the declared cutter radius. Change the radius, cutter or process; actual edge geometry is not measured by this comparison.");
            if(solid&&validProcess)if(auto size=process.find("stock_size_mm"))if(auto origin=process.find("stock_origin_mm"))if(auto bounds=geometry.find("bounds_mm"))if(bounds->t==V::Arr&&bounds->arr.size()==6){
                bool fit=true;for(size_t i=0;i<3;++i)fit&=bounds->arr[i].num>=origin->arr[i].num-1e-7&&bounds->arr[i+3].num<=origin->arr[i].num+size->arr[i].num+1e-7;
                V e=V::makeObj();e.set("id",V::makeStr("axis_aligned_stock_enclosure"));e.set("fits",V::makeBool(fit));e.set("authority",V::makeStr("exact_brep_bounds_and_caller_declared_local_stock"));r.evidence.arr.push_back(e);
                if(!fit)r.requirement("stock_enclosure","blocked","The exact part bounds are outside the declared feature-local axis-aligned stock; correct the stock size/origin. This does not assess clamp or machining allowances.");
            }
            r.pending("tool_access_and_workholding_checked","Verify tool/holder access to every feature, collision-free setups and stock clamping; bounds enclosure is not accessibility.");
            r.pending("machined_tolerance_checked","Validate cutter deflection, process capability, datum scheme, finish and inspection against the declared tolerance.");
            add(r.policy,"Use accessible cutters and attainable internal radii, declare stock allowance and workholding, and minimize setups without weakening functional geometry.");
            add(r.policy,"A machined plastic follows cutter/access/stock policies; neither injection moldability nor FDM infill is implied by the material name.");
        }else if(route=="sheet_metal"){
            if(lineage.geometryRoute!="sheet_metal")r.requirement("sheet_lineage","blocked","Sheet-metal intent requires an originating authored or supported imported sheet.");
            if(auto qualified=geometry.find("flat_pattern_qualified"))if(qualified->t==V::Bool&&!qualified->b)r.requirement("qualified_flat","blocked","Stamped nominal flats are not qualified developed blanks and cannot be fabrication exports.");
            r.pending("sheet_tooling_and_material_checked","Check calibrated bend deductions, stock, bend-tool access, forming strain and the actual manufacturing sequence.");
            add(r.policy,"Keep one authored sheet thickness and panel-owned holes; inspect the originating blank and formed clearances separately.");
        }else if(route=="frame"){
            if(lineage.geometryRoute!="frame")r.requirement("frame_lineage","blocked","Frame intent requires construction_frame authoring lineage.");
            r.pending("frame_joints_and_welds_checked","Review cutlist, cope/access, welding method, strength, distortion and assembly fixtures.");
            add(r.policy,"Use actual profiles and lengths from frame_inspect; specify joints and welds independently of display geometry.");
        }else if(route=="purchased"){
            r.pending("supplier_product_verified","An envelope is not a reconstructed product. Verify supplier identity, interfaces, tolerances and specification separately.");
            add(r.policy,"Model supplier installation interfaces and clearance envelope; preserve the supplier part identity in documentation.");
        }else r.requirement("fabrication_route","blocked","Unsupported manufacturing route.");
    }
    if(solid){V e=V::makeObj();e.set("id",V::makeStr("exact_single_solid"));e.set("authority",V::makeStr("evaluated_exact_brep"));e.set("volume_mm3",*geometry.find("volume_mm3"));r.evidence.arr.push_back(e);}
    const auto dfm=inspectDfm(document,feature);r.row.set("dfm",dfm);
    for(const auto& rule:dfm.find("rules")->arr){if(rule.find("status")->str=="incompatible")add(r.blockers,rule.find("id")->str);if(rule.find("status")->str=="unknown")add(r.unknowns,rule.find("id")->str);}
    r.row.set("requirements",r.requirements);r.row.set("blockers",r.blockers);r.row.set("unknowns",r.unknowns);r.row.set("evidence",r.evidence);r.row.set("design_policy",r.policy);
    r.row.set("process_parameters_complete",V::makeBool(r.parametersComplete&&r.blockers.arr.empty()));
    r.row.set("ready_for_process_review",V::makeBool(r.blockers.arr.empty()&&r.unknowns.arr.empty()));
    r.row.set("manufacturing_qualified",V::makeBool(false));r.row.set("geometry_mutated",V::makeBool(false));return r.row;
}
}
bool isManufacturingRoute(const std::string& route){return route=="sheet_metal"||route=="machined"||route=="frame"||route=="purchased"||route=="unspecified"||route=="injection_molding"||route=="additive";}
bool validateManufacturingProcess(const Feature& feature,std::string& error){
    const auto& process=feature.manufacturingProcess;if(process.t==V::Null)return true;
    if(process.t!=V::Obj||process.obj.size()>32){error="manufacturing.process must be an object with at most 32 explicit settings";return false;}
    if(process.obj.empty())return true;
    if(fields(feature.manufacturingRoute).empty()){error="manufacturing.process is supported only for sheet_metal, frame, machined, injection_molding and additive routes";return false;}
    std::map<std::string,Field> specs;for(const auto& field:common)specs.emplace(field.name,field);for(const auto& field:fields(feature.manufacturingRoute))specs.emplace(field.name,field);
    for(const auto& item:process.obj){auto spec=specs.find(item.first);if(spec==specs.end()){error="Unsupported manufacturing process setting for "+feature.manufacturingRoute+": "+item.first;return false;}if(!validField(item.second,spec->second)){error="Invalid manufacturing process setting: "+item.first;return false;}}
    return true;
}
bool applyManufacturingIntent(Feature& feature,const V& intent,std::string& error){
    if(intent.t==V::Null){feature.manufacturingRoute.clear();feature.manufacturingProcess=V{};return true;}
    if(intent.t!=V::Obj||intent.obj.size()>2){error="manufacturing must contain route and optional process, or be null";return false;}
    for(const auto& item:intent.obj)if(item.first!="route"&&item.first!="process"){error="Unknown manufacturing intent field: "+item.first;return false;}
    const auto* route=intent.find("route");if(!route||route->t!=V::Str||!isManufacturingRoute(route->str)){error="manufacturing.route must be sheet_metal, machined, frame, purchased, unspecified, injection_molding or additive";return false;}
    Feature candidate=feature;candidate.manufacturingRoute=route->str;candidate.manufacturingProcess=intent.find("process")?*intent.find("process"):V{};
    if(!validateManufacturingProcess(candidate,error))return false;feature.manufacturingRoute=std::move(candidate.manufacturingRoute);feature.manufacturingProcess=std::move(candidate.manufacturingProcess);return true;
}
V inspectManufacturing(const Document& document,const V& args,u64 revision){
    if(args.t!=V::Obj)throw std::runtime_error("manufacturing_inspect arguments must be an object");for(const auto& item:args.obj)if(item.first!="id")throw std::runtime_error("Unknown manufacturing_inspect argument: "+item.first);
    V report=V::makeObj(),components=V::makeArr();const auto consumed=consumedFeatureInputs(document),active=activeFeatureIds(document);std::set<u64> placed;for(const auto& node:document.assembly)if(node.feature)placed.insert(node.feature);
    if(const auto* selected=args.find("id")){
        const Feature* match=nullptr;for(const auto& feature:document.features)if((selected->t==V::Str&&selected->str==feature.name)||(selected->t==V::Num&&selected->num==(double)feature.id)){if(match)throw std::runtime_error("Ambiguous manufacturing feature");match=&feature;}
        if(!match)throw std::runtime_error("Manufacturing feature must resolve uniquely by id or name");components.arr.push_back(inspectComponent(document,*match));
    }else for(const auto& feature:document.features)if(active.count(feature.id)&&(!feature.manufacturingRoute.empty()||!consumed.count(feature.id)||placed.count(feature.id)))components.arr.push_back(inspectComponent(document,feature));
    bool ready=!components.arr.empty();for(const auto& component:components.arr)ready&=component.find("ready_for_process_review")->b;
    report.set("revision",V::makeNum((double)revision));report.set("scope",V::makeStr(args.find("id")?"feature_local":"declared_and_output_feature_definitions"));report.set("components",components);report.set("ready_for_process_review",V::makeBool(ready));report.set("manufacturing_qualified",V::makeBool(false));report.set("geometry_mutated",V::makeBool(false));report.set("dimension_authority",V::makeStr("authored_CAD_and_evaluated_exact_BRep; process_settings_are_caller_declarations"));return report;
}
}
