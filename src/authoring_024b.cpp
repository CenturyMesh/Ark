#include "ark/authoring_024b.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/plane_sections_024b.hpp"
#include "ark/media_authoring.hpp"
#include "ark/media_044b.hpp"
#include <stdexcept>
#include <filesystem>
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
    if(action=="playback")throw std::runtime_error("MEDIA_PLAYBACK_REQUIRES_STUDIO: connect to a live Studio session for display timeline binding");
    if(action=="capabilities"){out=mediaAuthoringCapabilities();out.set("revision",V::makeNum((double)session.revision));return true;}
    if(action=="generate"||action=="compose"){
        const auto* expected=args.find("expected_revision");if(!expected||expected->t!=V::Num||expected->num!=(double)session.revision)throw std::runtime_error("Revision conflict; read doc_info before media generation");
        out=action=="generate"?generateDisplayImage(args):composeDisplayMedia(session.document,args);out.set("source_revision",*expected);out.set("revision",*expected);return true;
    }
    if(action=="inspect"){
        out=V::makeObj();V assets=V::makeArr();for(const auto& asset:session.document.imageAssets)if(text("name").empty()||asset.name==text("name"))assets.arr.push_back(imageAssetToJson(asset));
        out.set("assets",assets);out.set("revision",V::makeNum((double)session.revision));out.set("generation",mediaAuthoringCapabilities());return true;
    }
    if(action!="import")throw std::runtime_error("image_asset action must be capabilities, inspect, generate, compose or import");
    const auto* expected=args.find("expected_revision");if(!expected||expected->t!=V::Num||expected->num!=(double)session.revision)throw std::runtime_error("Revision conflict; read doc_info and provide expected_revision");
    const auto* embed=args.find("embed");if(embed&&embed->t!=V::Bool)throw std::runtime_error("embed must be boolean");
    ImageAsset asset;if(embed&&embed->b&&mediaAssetPathRequest044B(text("path"))){auto absolute=std::filesystem::absolute(std::filesystem::u8path(text("path")));auto file=absolute.u8string(),directory=absolute.parent_path().u8string();asset=mediaAssetFromPath044B(text("name"),std::string(file.begin(),file.end()),true,std::string(directory.begin(),directory.end()));}else asset=imageAssetFromPath(text("name"),text("path"));
    V command=V::makeObj(),commands=V::makeArr(),apply=V::makeObj();command.set("op",V::makeStr("image_asset.set"));command.set("asset",imageAssetToJson(asset,true));commands.arr.push_back(command);apply.set("commands",commands);apply.set("expected_revision",*expected);
    const auto result=session.call("doc_apply",apply);if(result.find("isError")&&result.find("isError")->b)throw std::runtime_error(result.find("structuredContent")->dump());out=*result.find("structuredContent");out.set("asset",imageAssetToJson(asset));return true;
}
void appendAuthoring024Tools(V& tools){
    V tool=V::makeObj(),schema,annotations=V::makeObj();
    arkjson::parse(R"({"type":"object","properties":{"action":{"enum":["capabilities","inspect","import","generate","compose","playback"]},"name":{"type":"string","minLength":1,"maxLength":128},"embed":{"type":"boolean"},"path":{"type":"string","minLength":1},"expected_revision":{"type":"integer","minimum":0},"mode":{"enum":["inspect","bind","clear","play","pause","stop","seek"]},"time_s":{"type":"number","minimum":0,"maximum":86400},"motion":{"type":"string"},"assets":{"type":"array","minItems":1,"maxItems":16,"items":{"type":"string"}},"leds":{"type":"array","maxItems":16,"items":{"type":"object","properties":{"feature":{"type":"integer","minimum":1},"intensity":{"type":"string","maxLength":512}},"required":["feature","intensity"],"additionalProperties":false}},"model":{"type":"string"},"prompt":{"type":"string","maxLength":32000},"size":{"enum":["1024x1024","1536x1024","1024x1536","auto"]},"quality":{"enum":["low","medium","high","auto"]},"format":{"enum":["png","gif","mp4"]},"width":{"type":"integer","minimum":16,"maximum":1024},"height":{"type":"integer","minimum":16,"maximum":1024},"fps":{"type":"integer","minimum":1,"maximum":60},"duration_s":{"type":"number","exclusiveMinimum":0,"maximum":60},"background":{"type":"string"},"layers":{"type":"array","minItems":1,"maxItems":64,"items":{"type":"object","properties":{"type":{"enum":["rect","ellipse","line","text","image"]},"x":{"type":["number","string"]},"y":{"type":["number","string"]},"x2":{"type":["number","string"]},"y2":{"type":["number","string"]},"width":{"type":["number","string"]},"height":{"type":["number","string"]},"opacity":{"type":["number","string"]},"color":{"type":"string"},"from_s":{"type":["number","string"]},"to_s":{"type":["number","string"]},"line_width":{"type":["number","string"]},"outline":{"type":"boolean"},"text":{"type":"string","maxLength":4096},"font_size":{"type":["number","string"]},"asset":{"type":"string"},"path":{"type":"string"}},"required":["type"],"additionalProperties":false}}},"required":["action"],"additionalProperties":false})",schema);
    tool.set("name",V::makeStr("image_asset"));tool.set("description",V::makeStr("Inspect image/media metadata or capabilities. generate writes a real PNG through the OpenAI Images API with explicit model,prompt,path,expected_revision (requires an API key, separate from ChatGPT plan login). compose writes PNG/GIF/MP4 locally from layers: rect,ellipse,line,text,image (PNG/JPEG asset or path); animate numeric fields with expressions over t,u,frame,duration,width,height and document parameters, and schedule from_s/to_s. Colors are #RRGGBB. Local composition needs no image API key. Outputs never overwrite files or change the document; receipts include SHA-256, dimensions, duration and readback. Then import{name,path,expected_revision} embeds a validated asset (use embed:true for a portable MP4 up to 2 MiB) and stamp.set applies it to observed faces. Live Studio only: playback mode bind with motion, GIF assets, optional leds[{feature,intensity expression 0..1}],expected_revision follows the current baked frame clock. inspect reports state; clear removes binding. play,pause,stop and seek{time_s} control the bound preview with expected_revision; pending cable preparation is reported explicitly. Session-local, invalidated by revision/document changes; MP4 synchronization is not supported. This is display synchronization, never hardware triggering. Read agent_onboard stamps."));
    annotations.set("readOnlyHint",V::makeBool(false));tool.set("inputSchema",schema);tool.set("annotations",annotations);tools.arr.push_back(tool);
    tool=V::makeObj();arkjson::parse(R"({"type":"object","properties":{"action":{"enum":["sketch_to_plane","create_plane","offset_plane","section","section_from_line"]},"name":{"type":"string"},"sketch":{"type":"string"},"entity":{"type":"string"},"plane":{"type":"string"},"plane_name":{"type":"string"},"parent":{"type":"string"},"p":{"type":"object","additionalProperties":{"type":["number","string"]}},"offset":{"type":["number","string"]},"id":{"type":["integer","string"]},"mode":{"enum":["contour","positive","negative","both"]},"preview":{"type":"boolean"},"expected_revision":{"type":"integer","minimum":0}},"required":["action","expected_revision"],"additionalProperties":false})",schema);
    tool.set("name",V::makeStr("plane_authoring"));tool.set("description",V::makeStr("Create an editable plane, offset a named plane, move a saved sketch to a plane preserving local sketch coordinates/constraints, or create exact section contour/cut features from a plane or saved sketch line. Line sections retain associative datums. preview returns concrete atomic commands. contour supports one connected exact section wire; positive/negative/both create material cut features and preserve source history. Read agent_onboard topic planes. Revision checked; no automatic drawings."));tool.set("inputSchema",schema);tool.set("annotations",annotations);tools.arr.push_back(tool);
}
}
