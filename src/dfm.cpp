#include "ark/dfm.hpp"
#include "ark/manufacturing.hpp"
#include <cmath>

namespace ark {
namespace {
using V=arkjson::Value;
const Body* bodyFor(const Document& document,u64 id){for(const auto& body:document.evalBodies)if(body.id==id)return &body;return nullptr;}
const V* number(const V& object,const char* key){const auto* value=object.find(key);return value&&value->t==V::Num&&std::isfinite(value->num)?value:nullptr;}
bool exact(const Body* body){
    if(!body)return false;const auto* brep=body->nativeData.find("brep_base64"),*report=body->nativeData.find("report");
    if(!brep||brep->t!=V::Str||brep->str.empty()||!report)return false;
    const auto* valid=report->find("brep_valid"),*solids=number(*report,"solids"),*volume=number(*report,"volume_mm3");
    return valid&&valid->t==V::Bool&&valid->b&&solids&&solids->num>=1&&volume&&volume->num>0;
}
bool bounds(const V* value){if(!value||value->t!=V::Arr||value->arr.size()!=6)return false;for(const auto& item:value->arr)if(item.t!=V::Num||!std::isfinite(item.num))return false;for(size_t i=0;i<3;++i)if(value->arr[i+3].num<value->arr[i].num)return false;return true;}
V strings(std::initializer_list<const char*> values){V array=V::makeArr();for(const auto* value:values)array.arr.push_back(V::makeStr(value));return array;}
struct Screen {
    V rules=V::makeArr(),recommendations=V::makeArr();
    void rule(const char* id,const char* process,const char* status,const char* authority,const V& evidence,const V& assumptions,const char* action,const char* guidance){
        V row=V::makeObj();row.set("id",V::makeStr(id));row.set("version",V::makeNum(1));row.set("process",V::makeStr(process));row.set("status",V::makeStr(status));row.set("evidence_authority",V::makeStr(authority));row.set("evidence",evidence);row.set("assumptions",assumptions);row.set("guidance",V::makeStr(guidance));rules.arr.push_back(row);
        if(std::string(status)=="incompatible"||std::string(status)=="unknown"){
            V recommendation=V::makeObj();recommendation.set("rule_id",V::makeStr(id));recommendation.set("rule_version",V::makeNum(1));recommendation.set("kind",V::makeStr(action));recommendation.set("severity",V::makeStr(std::string(status)=="incompatible"?"warning":"needs_evidence"));recommendation.set("guidance",V::makeStr(guidance));recommendation.set("automatic_edit",V::makeBool(false));recommendations.arr.push_back(recommendation);
        }
    }
    void pending(const char* id,const char* process,const char* guidance){rule(id,process,"unknown","unverified",V::makeObj(),strings({"No external process qualification evidence is available to this inspector."}),"obtain_process_evidence",guidance);}
};
}
V inspectDfm(const Document& document,const Feature& feature){
    Screen screen;const auto& route=feature.manufacturingRoute;const auto& p=feature.manufacturingProcess;
    const auto lineage=featureManufacturingLineage(document,feature);const Body* body=bodyFor(document,feature.id);const bool geometry=exact(body)&&!feature.suppressed;
    const V* report=geometry?body->nativeData.find("report"):nullptr;
    const bool single=report&&number(*report,"solids")&&number(*report,"solids")->num==1;
    std::string error;const bool valid=validateManufacturingProcess(feature,error);
    if(!valid)screen.rule("DFM.INPUT.01","input","unknown","caller_declared",V::makeObj(),strings({"Invalid process settings are not evaluated."}),"request_input","Provide a valid route-specific process plan.");
    else if(route=="additive"){
        const auto* layer=number(p,"layer_height_mm"),*nozzle=number(p,"nozzle_diameter_mm");V settings=V::makeObj();if(layer)settings.set("layer_height_mm",*layer);if(nozzle)settings.set("nozzle_diameter_mm",*nozzle);
        screen.rule("DFM.FDM.01","fdm",layer&&nozzle?(layer->num<nozzle->num?"compatible":"incompatible"):"unknown","caller_declared",settings,strings({"FDM only; layer height must be less than nozzle diameter in this planning domain.","This comparison does not establish printer/profile capability."}),layer&&nozzle?"change_process_or_design":"request_input","Choose a layer height below the nozzle diameter and validate the actual printer/material profile.");
        const auto* direction=p.find("build_direction"),*volume=p.find("build_volume_mm"),*box=report?report->find("bounds_mm"):nullptr;
        const bool axisAligned=direction&&direction->t==V::Arr&&direction->arr.size()==3&&direction->arr[0].num==0&&direction->arr[1].num==0&&direction->arr[2].num!=0;
        V fitEvidence=V::makeObj();if(direction)fitEvidence.set("build_direction",*direction);if(volume)fitEvidence.set("build_volume_mm",*volume);
        bool evaluated=single&&bounds(box)&&volume&&axisAligned;bool fits=true;
        if(evaluated){V extents=V::makeArr();for(size_t i=0;i<3;++i){const double extent=box->arr[i+3].num-box->arr[i].num;extents.arr.push_back(V::makeNum(extent));fits&=extent<=volume->arr[i].num+1e-7;}fitEvidence.set("part_bounds_mm",*box);fitEvidence.set("part_extents_mm",extents);fitEvidence.set("numerical_comparison_epsilon_mm",V::makeNum(1e-7));}
        screen.rule("DFM.FDM.02","fdm",evaluated?(fits?"compatible":"incompatible"):"unknown",evaluated?"evaluated_exact_brep_and_caller_declared":"unverified",fitEvidence,strings({"Feature-local +Z or -Z build direction only; local X/Y align with machine X/Y without in-plane rotation.","Translation is free; the part envelope excludes supports, brim/raft, purge, bed margins and fixture clearance.","Other orientations require an explicit build frame and new exact directional bounds."}),evaluated?"change_process_or_design":"request_input","Review build volume and orientation. Supply supported axis-aligned orientation for envelope screening; obtain slicer fit including supports and margins.");
        screen.pending("DFM.FDM.03","fdm","Obtain actual slicer overhang/bridge/support evidence and material layer-strength evidence for the declared load case.");
    }else if(route=="machined"){
        const auto* size=p.find("stock_size_mm"),*origin=p.find("stock_origin_mm"),*box=report?report->find("bounds_mm"):nullptr;bool evaluated=single&&bounds(box)&&size&&origin;bool fits=true;V stock=V::makeObj();if(size)stock.set("stock_size_mm",*size);if(origin)stock.set("stock_origin_mm",*origin);
        if(evaluated){stock.set("part_bounds_mm",*box);stock.set("numerical_comparison_epsilon_mm",V::makeNum(1e-7));for(size_t i=0;i<3;++i)fits&=box->arr[i].num>=origin->arr[i].num-1e-7&&box->arr[i+3].num<=origin->arr[i].num+size->arr[i].num+1e-7;}
        screen.rule("DFM.CNC.01","cnc",evaluated?(fits?"compatible":"incompatible"):"unknown",evaluated?"evaluated_exact_brep_and_caller_declared":"unverified",stock,strings({"Stock is axis aligned in feature-local coordinates.","Enclosure excludes clamp space, allowances, holder access and machining sequence."}),evaluated?"change_process_or_design":"request_input","Declare stock size/origin that encloses the exact part; review machining allowances and workholding separately.");
        const auto* cutter=number(p,"cutter_diameter_mm"),*radius=number(p,"minimum_internal_radius_mm");V corner=V::makeObj();if(cutter)corner.set("cutter_diameter_mm",*cutter);if(radius)corner.set("declared_minimum_internal_radius_mm",*radius);
        screen.rule("DFM.CNC.02","cnc",cutter&&radius?(radius->num>=cutter->num/2?"compatible":"incompatible"):"unknown","caller_declared",corner,strings({"The internal radius is a caller target, not a measured edge radius.","Circular cutter radius comparison only; no toolpath or holder-access proof."}),cutter&&radius?"change_process_or_design":"request_input","Use a cutter radius no greater than the declared internal radius; confirm actual feature geometry and access in CAM.");
        screen.pending("DFM.CNC.03","cnc","Obtain tool/holder access, setup collision, clamping, deflection and achievable tolerance evidence.");
    }else if(route=="sheet_metal"){
        // Sheet evidence is deliberately taken from the retained authoring origin,
        // rather than the installed pose's bounds or unevaluated expressions.
        const Body* source=bodyFor(document,lineage.origin?lineage.origin:feature.id);const V* sourceReport=geometry&&lineage.geometryRoute=="sheet_metal"&&exact(source)?source->nativeData.find("report"):nullptr;
        const auto* qualified=sourceReport?sourceReport->find("flat_pattern_qualified"):nullptr;const bool flat=qualified&&qualified->t==V::Bool&&qualified->b;
        const auto* method=p.find("cutting_method");
        if(method&&method->str=="laser"){
            const auto* thickness=sourceReport?number(*sourceReport,"thickness_mm"):nullptr,*limit=number(p,"laser_max_thickness_mm");V evidence=V::makeObj();evidence.set("geometry_feature_id",V::makeNum((double)(lineage.origin?lineage.origin:feature.id)));if(thickness)evidence.set("thickness_mm",*thickness);if(limit)evidence.set("declared_laser_max_thickness_mm",*limit);
            screen.rule("DFM.LASER.01","laser",thickness&&limit?(thickness->num<=limit->num?"compatible":"incompatible"):"unknown",thickness?"evaluated_sheet_model_and_caller_declared":"unverified",evidence,strings({"Constant-thickness authored/imported sheet domain; capability is caller declared for the actual material/machine.","No kerf, minimum web/hole, heat, lead-in or nesting analysis."}),thickness&&limit?"change_process_or_design":"request_input","Compare exact sheet thickness with an actual material/machine cutting limit; obtain cut-quality and small-feature evidence.");
            V flatEvidence=V::makeObj();if(qualified)flatEvidence.set("flat_pattern_qualified",*qualified);
            screen.rule("DFM.LASER.02","laser",qualified?(flat?"compatible":"incompatible"):"unknown",qualified?"evaluated_exact_sheet_model":"unverified",flatEvidence,strings({"Qualified means the producer's geometric development scope only, with declared K-factor.","Stamped nominal reference flats do not establish a cutting blank."}),qualified?"change_process_or_design":"request_input","Use a producer-supported developed blank; resolve unsupported forming before laser flat export.");
        }else screen.rule("DFM.LASER.01","laser","not_applicable","caller_declared",V::makeObj(),strings({"Laser rules require cutting_method:laser; no process is inferred from sheet geometry."}),"request_input","Declare laser cutting only when it is the intended production method.");
        const auto* limit=number(p,"minimum_bend_radius_mm"),*count=sourceReport?number(*sourceReport,"bend_count"):nullptr;const auto* animation=source?source->nativeData.find("animation"):nullptr;const auto* bends=animation?animation->find("bends"):nullptr;V bendEvidence=V::makeObj();bendEvidence.set("geometry_feature_id",V::makeNum((double)(lineage.origin?lineage.origin:feature.id)));if(limit)bendEvidence.set("declared_minimum_bend_radius_mm",*limit);
        bool evaluated=sourceReport&&count&&bends&&bends->t==V::Arr&&count->num==(double)bends->arr.size();bool meets=true;V radii=V::makeArr();if(evaluated)for(const auto& bend:bends->arr){const auto* radius=number(bend,"inner_radius_mm");if(!radius){evaluated=false;break;}V row=V::makeObj();if(auto id=bend.find("id"))row.set("id",*id);row.set("inner_radius_mm",*radius);radii.arr.push_back(row);if(limit)meets&=radius->num>=limit->num;}bendEvidence.set("bends",radii);
        screen.rule("DFM.BEND.01","bend",evaluated&&count->num==0?"not_applicable":evaluated&&limit?(meets?"compatible":"incompatible"):"unknown",evaluated?"evaluated_sheet_model_and_caller_declared":"unverified",bendEvidence,strings({"Every producer-reported bend, including hems/jogs, is compared with the same explicit material/tooling radius limit.","No generic material ratio is invented; springback, deduction calibration, bend sequence and tooling access remain unverified."}),evaluated&&limit?"change_process_or_design":"request_input","Declare the material/tooling minimum radius and review every evaluated bend radius against it.");
        screen.pending("DFM.BEND.02","bend","Obtain material-specific bend deduction/springback, tool access and forming sequence evidence; geometric refold is not process qualification.");
    }else if(route=="frame"){
        const Body* source=bodyFor(document,lineage.origin?lineage.origin:feature.id);const V* sourceReport=geometry&&lineage.geometryRoute=="frame"&&exact(source)?source->nativeData.find("frame_report"):nullptr;const auto* welds=sourceReport?sourceReport->find("welds"):nullptr,*limit=number(p,"minimum_weld_throat_mm");V evidence=V::makeObj();evidence.set("geometry_feature_id",V::makeNum((double)(lineage.origin?lineage.origin:feature.id)));if(limit)evidence.set("declared_minimum_weld_throat_mm",*limit);
        bool evaluated=welds&&welds->t==V::Arr;bool meets=true;V throats=V::makeArr();if(evaluated)for(const auto& weld:welds->arr){const auto* throat=number(weld,"theoretical_throat_mm"),*checked=weld.find("attachment_checked");if(!throat||!checked||checked->t!=V::Bool||!checked->b){evaluated=false;break;}V row=V::makeObj();if(auto id=weld.find("id"))row.set("id",*id);row.set("theoretical_throat_mm",*throat);row.set("attachment_checked",*checked);throats.arr.push_back(row);if(limit)meets&=throat->num>=limit->num;}evidence.set("welds",throats);
        screen.rule("DFM.WELD.01","weld",evaluated&&welds->arr.empty()?"unknown":evaluated&&limit?(meets?"compatible":"incompatible"):"unknown",evaluated?"evaluated_exact_declared_beads_and_caller_declared":"unverified",evidence,strings({"Declared straight triangular fillet beads only; theoretical throat derives from the evaluated bead section.","Touching both members does not prove continuous fusion, penetration, weld quality, strength or accessibility.","No beads means absent evidence; it does not prove no welding is needed."}),evaluated&&limit&&!welds->arr.empty()?"change_process_or_design":"request_input","Declare the required throat from a justified joint design and compare all evaluated beads; obtain WPS, joint strength, distortion and inspection evidence.");
        screen.pending("DFM.WELD.02","weld","Obtain welding procedure/material compatibility, continuous joint attachment, strength, fixture/distortion and inspection evidence.");
    }else if(route=="injection_molding")screen.pending("DFM.MOLD.01","injection_molding","Obtain wall distribution, draft/undercut, mold split/flow/cooling and tooling evidence for the chosen resin and pull direction.");
    else screen.rule("DFM.ROUTE.01","unspecified","not_applicable","caller_declared",V::makeObj(),strings({"Process-specific screening requires an explicit supported fabrication route; purchased supplier validation is separate."}),"request_input","Declare the intended fabrication route when production review is required.");
    V result=V::makeObj();result.set("schema_version",V::makeNum(1));result.set("ruleset_id",V::makeStr("ark.dfm"));result.set("ruleset_version",V::makeNum(1));result.set("milestone",V::makeStr("C.03"));result.set("rules",screen.rules);result.set("recommendations",screen.recommendations);result.set("manufacturing_qualified",V::makeBool(false));result.set("geometry_mutated",V::makeBool(false));return result;
}
}
