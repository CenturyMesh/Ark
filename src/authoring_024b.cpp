#include "ark/authoring_024b.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/plane_sections_024b.hpp"
#include <stdexcept>
namespace ark {
using V=arkjson::Value;
bool callAuthoring024Tool(AgentSession& session,const std::string& tool,const V& args,V& out){
    if(tool=="plane_authoring"){
        const auto* expected=args.find("expected_revision");if(!expected||expected->t!=V::Num||expected->num!=(double)session.revision)throw std::runtime_error("Revision conflict; inspect doc_info before plane or section authoring");
        if(const auto* preview=args.find("preview");preview&&preview->t!=V::Bool)throw std::runtime_error("preview must be boolean");
        out=planeSectionRecipe(session.document,args);out.set("revision",*expected);
        if(args.find("preview")&&args.find("preview")->b){out.set("applied",V::makeBool(false));return true;}
        V apply=V::makeObj();apply.set("expected_revision",*expected);apply.set("commands",*out.find("commands"));const auto result=session.call("doc_apply",apply);
        if(result.find("isError")&&result.find("isError")->b)throw std::runtime_error(result.find("structuredContent")->dump());out=*result.find("structuredContent");out.set("applied",V::makeBool(true));return true;
    }
    if(tool!="image_asset")return false;
    auto text=[&](const char* key,const std::string& fallback={}){const auto* v=args.find(key);if(!v)return fallback;if(v->t!=V::Str||v->str.empty())throw std::runtime_error(std::string(key)+" must be nonempty text");return v->str;};
    const auto action=text("action","inspect");
    if(action=="inspect"){
        out=V::makeObj();V assets=V::makeArr();for(const auto& asset:session.document.imageAssets)if(text("name").empty()||asset.name==text("name"))assets.arr.push_back(imageAssetToJson(asset));
        out.set("assets",assets);out.set("revision",V::makeNum((double)session.revision));out.set("generation",V::makeStr("Import actual PNG/JPEG output from your available image-generation skill or connector; ARK does not fabricate a generated image."));return true;
    }
    if(action!="import")throw std::runtime_error("image_asset action must be inspect or import");
    const auto* expected=args.find("expected_revision");if(!expected||expected->t!=V::Num||expected->num!=(double)session.revision)throw std::runtime_error("Revision conflict; read doc_info and provide expected_revision");
    const auto asset=imageAssetFromPath(text("name"),text("path"));
    V command=V::makeObj(),commands=V::makeArr(),apply=V::makeObj();command.set("op",V::makeStr("image_asset.set"));command.set("asset",imageAssetToJson(asset,true));commands.arr.push_back(command);apply.set("commands",commands);apply.set("expected_revision",*expected);
    const auto result=session.call("doc_apply",apply);if(result.find("isError")&&result.find("isError")->b)throw std::runtime_error(result.find("structuredContent")->dump());out=*result.find("structuredContent");out.set("asset",imageAssetToJson(asset));return true;
}
void appendAuthoring024Tools(V& tools){
    V tool=V::makeObj(),schema,annotations=V::makeObj();
    arkjson::parse(R"({"type":"object","properties":{"action":{"enum":["inspect","import"]},"name":{"type":"string","minLength":1,"maxLength":128},"path":{"type":"string","minLength":1},"expected_revision":{"type":"integer","minimum":0}},"required":["action"],"additionalProperties":false})",schema);
    tool.set("name",V::makeStr("image_asset"));tool.set("description",V::makeStr("Inspect embedded image metadata without pixel payload, or import a real local PNG/JPEG from user input or an available image-generation skill. Import requires name,path,expected_revision and embeds validated pixels in the portable document. Then stamp.set projects it onto exact face regions or planes; canvas.set asset places it on a sketch plane. Read agent_onboard topic stamps. Raster appearance never determines production geometry."));
    annotations.set("readOnlyHint",V::makeBool(false));tool.set("inputSchema",schema);tool.set("annotations",annotations);tools.arr.push_back(tool);
    tool=V::makeObj();arkjson::parse(R"({"type":"object","properties":{"action":{"enum":["sketch_to_plane","create_plane","offset_plane","section","section_from_line"]},"name":{"type":"string"},"sketch":{"type":"string"},"entity":{"type":"string"},"plane":{"type":"string"},"plane_name":{"type":"string"},"parent":{"type":"string"},"p":{"type":"object","additionalProperties":{"type":["number","string"]}},"offset":{"type":["number","string"]},"id":{"type":["integer","string"]},"mode":{"enum":["contour","positive","negative","both"]},"preview":{"type":"boolean"},"expected_revision":{"type":"integer","minimum":0}},"required":["action","expected_revision"],"additionalProperties":false})",schema);
    tool.set("name",V::makeStr("plane_authoring"));tool.set("description",V::makeStr("Create an editable plane, offset a named plane, move a saved sketch to a plane preserving local sketch coordinates/constraints, or create exact section contour/cut features from a plane or saved sketch line. Line sections retain associative datums. preview returns concrete atomic commands. contour supports one connected exact section wire; positive/negative/both create material cut features and preserve source history. Read agent_onboard topic planes. Revision checked; no automatic drawings."));tool.set("inputSchema",schema);tool.set("annotations",annotations);tools.arr.push_back(tool);
}
}
