#include "ark/image_validation_074b.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/assistant.hpp"
#include "ark/io.hpp"
#include "ark/agent.hpp"
#include "ark/serialize.hpp"
#include "ark/iteration_history.hpp"
#include "ark/sha256_074b.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using V=arkjson::Value;
static unsigned checks=0;
static void check(bool okay,const char* message){++checks;if(!okay)throw std::runtime_error(message);}
static V json(const char* text){V value;check(arkjson::parse(text,value),"Fixture JSON");return value;}
static V accepted(const V& value){if(value.find("isError")->b)throw std::runtime_error(value.find("structuredContent")->dump());return *value.find("structuredContent");}
static V edit(ark::AgentSession& session,int value){V args=json(R"({"commands":[{"op":"param.set","name":"Length","expr":"0"}]})");auto commands=*args.find("commands");commands.arr[0].set("expr",V::makeStr(std::to_string(value)));args.set("commands",commands);args.set("expected_revision",V::makeNum(session.revision));return accepted(session.call("doc_apply",args));}
static void equivalent(const ark::Document& source){
    const auto encoded=ark::documentToJson(source);
    check(ark::documentValue(source).dump()==encoded,"Typed persistence differs from canonical JSON");
    check(ark::documentPersistenceSize(source)==encoded.size(),"Typed byte count differs from actual JSON bytes");
    ark::Document text,typed;std::string error;
    check(ark::documentFromJson(ark::documentToJson(source,true,false),text,&error),error.c_str());
    check(ark::cloneDocumentAuthoring(source,typed,&error),error.c_str());
    check(ark::documentToJson(typed)==ark::documentToJson(text),"Typed clone differs from validated text clone");
    check(typed.iterationHistory.isNull()&&typed.evalBodies.empty(),"Authoring clone retained history or evaluated state");
}
#include "image_validation_074b_cases.inl"
int main(){try{
    imageValidationMemoContracts074();
    struct Vector{size_t bytes;const char* expected;};
    const Vector vectors[]={{0,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},{3,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},{255,"857df204175f077a9986709897f00ee0bcc0449585248e4b42498337e9329999"},{256,"5bc31b283cef0072274e97d74916552954c935794536cab632641e5ea071379d"},{4096,"d67c656e01756650d77717b0839985a056ec28ffe174601d690fc407a2ceffca"},{1048576,"631b84027d6b9e52b539c4e8373622d23032dfadc64d60af87339c9037e4f769"}};
    for(const auto& v:vectors){std::string bytes(v.bytes,0);for(size_t i=0;i<bytes.size();++i)bytes[i]=char(i%251);if(v.bytes==3)bytes="abc";check(ark::iterationSha256(bytes)==v.expected,"Portable SHA known vector differs");std::string native="unchanged";if(ark::platformSha256074B(bytes,native))check(native==v.expected,"Platform SHA known vector differs");}
    ark::Document source;source.setParam("Length","10");ark::Feature feature;feature.name="Source";feature.type=ark::FeatureType::NativeSolid;feature.suppressed=true;feature.nativeSpec=json(R"({"op":"box","metadata":{"quoted":"a\n\r\t\u0000\\\"z","number":1.2345678901234567}})");feature.nativeSpec.set("negative_zero",V::makeNum(-0.0));feature.nativeSpec.set("nonfinite",V::makeNum(std::numeric_limits<double>::infinity()));feature.p={{"width","Length"},{"depth","4"},{"height","5"}};source.addFeature(feature);equivalent(source);
    ark::Document destination;destination.setParam("Retained","42");const auto stable=ark::documentToJson(destination);std::string error;
    ark::Document duplicate=source;duplicate.features[0].nativeSpec.obj.push_back({"op",V::makeStr("box")});check(!ark::cloneDocumentAuthoring(duplicate,destination,&error)&&ark::documentToJson(destination)==stable,"Duplicate-key clone did not reject atomically");
    ark::Document deep=source;V nested=V::makeNum(1);for(int i=0;i<66;++i){V layer=V::makeArr();layer.arr.push_back(std::move(nested));nested=std::move(layer);}deep.features[0].nativeSpec.set("nested",std::move(nested));check(!ark::cloneDocumentAuthoring(deep,destination,&error)&&ark::documentToJson(destination)==stable,"Depth rejection changed clone destination");
    ark::AgentSession session;for(int i=0;i<80;++i)edit(session,i);equivalent(session.document);const auto prior=ark::documentToJson(session.document);const auto revision=session.revision;auto invalid=json(R"({"commands":[{"op":"param.set","name":"Length","expr":"999"}]})");invalid.set("expected_revision",V::makeNum(revision));invalid.set("motivation",V::makeStr(std::string(4097,'x')));check(session.call("doc_apply",invalid).find("isError")->b&&session.revision==revision&&ark::documentToJson(session.document)==prior,"Journal failure changed retained authoring");accepted(session.call("doc_undo",V::makeObj()));check(session.document.getParam("Length")==78,"Persistent Undo failed");accepted(session.call("doc_redo",V::makeObj()));check(session.document.getParam("Length")==79,"Persistent Redo failed");
    ark::AgentSession legacy;legacy.suppressIterationHistory();for(int i=0;i<8;++i)edit(legacy,i);accepted(legacy.call("doc_undo",V::makeObj()));check(legacy.document.getParam("Length")==6,"Shared legacy Undo snapshot changed");accepted(legacy.call("doc_redo",V::makeObj()));check(legacy.document.getParam("Length")==7,"Shared legacy Redo snapshot changed");
    std::cout<<"Transaction storage074B: "<<checks<<" typed parity, strict validation, hash and Undo checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
