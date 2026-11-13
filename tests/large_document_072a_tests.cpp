#include "ark/agent.hpp"
#include "ark/appearance_024b.hpp"
#include "ark/assistant.hpp"
#include "ark/document_container.hpp"
#include "ark/io.hpp"
#include "ark/iteration_history.hpp"
#include "ark/serialize.hpp"
#include "native_process.hpp"
#include "sqlite3.h"
#include <iostream>
#include <stdexcept>
using V=arkjson::Value;
static unsigned checks=0;
static void check(bool value,const std::string& message){++checks;if(!value)throw std::runtime_error(message);}
static V json(const char* text){V value;check(arkjson::parse(text,value),"Invalid synthetic fixture JSON");return value;}
static V accepted(V value){if(const auto* bad=value.find("isError");bad&&bad->b)throw std::runtime_error(value.find("structuredContent")?value.find("structuredContent")->dump():value.dump());check(value.find("structuredContent"),"Missing tool receipt");return *value.find("structuredContent");}
static void apply(ark::AgentSession& session,V commands){V request=V::makeObj();request.set("expected_revision",V::makeNum((double)session.revision));request.set("commands",std::move(commands));accepted(session.call("doc_apply",request));}
static V asset(unsigned seed,const std::string& name){
    ark::Image image;image.w=700;image.h=700;image.px.resize(700u*700u*3u);for(auto& channel:image.px){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;channel=(unsigned char)seed;}
    auto png=ark::encodePNG(image);check(png.size()<4u*1024u*1024u,"Individual media source exceeded its unchanged bound");auto attachment=ark::assistantImageAttachment(png,"synthetic-large-source.png",ark::AssistantImageDecoder::Portable);
    V value=V::makeObj();value.set("name",V::makeStr(name));value.set("mime_type",V::makeStr(attachment.mimeType));value.set("data_base64",V::makeStr(attachment.base64));return value;
}
int main(){try{
    native_test::TempDirectory workspace("ark-large-document-072a-");const auto path=workspace.path()/"source.ark";const auto filename=native_test::pathUtf8(path);
    ark::AgentSession session;apply(session,json(R"([{"op":"param.set","name":"width","expr":"10"},{"op":"feature.add","type":"box","name":"Box","p":{"dx":"width","dy":"3","dz":"4"}}])"));
    for(unsigned index=0;index<3;++index){V commands=V::makeArr(),command=V::makeObj();command.set("op",V::makeStr("image_asset.set"));command.set("asset",asset(713+index*11,"Synthetic "+std::to_string(index)));commands.arr.push_back(std::move(command));check(commands.dump().size()<4u*1024u*1024u,"Each public command remains below unchanged transport bound");apply(session,std::move(commands));}
    const auto authored=ark::documentToJson(session.document);check(authored.size()>4u*1024u*1024u,"Fixture did not cross legacy document size");const auto semantic=json(authored.c_str()).find("version")->num;
    const auto images=session.document.imageAssets;const auto previousHead=session.document.iterationHistory.find("head_id")->str;const auto previousNodes=session.document.iterationHistory.find("nodes")->arr.size();
    apply(session,json(R"([{"op":"param.set","name":"width","expr":"12"}])"));check(session.document.getParam("width")==12&&session.document.iterationHistory.find("nodes")->arr.size()==previousNodes+1,"Large-source edit did not retain and extend all prior nodes");
    bool retainedHead=false;for(const auto& node:session.document.iterationHistory.find("nodes")->arr)if(node.find("id")->str==previousHead)retainedHead=true;check(retainedHead,"Large-source edit pruned an earlier history node");
    const auto beforeSave=ark::documentToJson(session.document);const auto revision=session.revision;V request=V::makeObj();request.set("path",V::makeStr(filename));request.set("expected_revision",V::makeNum((double)revision));accepted(session.call("doc_save",request));check(session.revision==revision,"Save unexpectedly edited model revision");
    sqlite3* database=nullptr;check(sqlite3_open_v2(filename.c_str(),&database,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,"Cannot read synthetic storage identity");sqlite3_stmt* query=nullptr;check(sqlite3_prepare_v2(database,"PRAGMA user_version",-1,&query,nullptr)==SQLITE_OK&&sqlite3_step(query)==SQLITE_ROW&&sqlite3_column_int(query,0)==3,"Large source did not use capacity storage schema3");sqlite3_finalize(query);sqlite3_close(database);
    ark::AgentSession reopened;request.set("expected_revision",V::makeNum(0));accepted(reopened.call("doc_load",request));check(ark::documentToJson(reopened.document)==beforeSave,"Public save/load changed authored source or any retained history");check(json(ark::documentToJson(reopened.document).c_str()).find("version")->num==semantic,"Capacity upgrade altered semantic authoring version");
    check(reopened.document.imageAssets.size()==images.size(),"Embedded source asset was lost");for(size_t i=0;i<images.size();++i)check(reopened.document.imageAssets[i].base64==images[i].base64&&reopened.document.imageAssets[i].sha256==images[i].sha256,"Embedded pixels or hashes changed");
    const auto originalMesh=session.document.evalBodies.front().tessellate(session.document.tessOpt),loadedMesh=reopened.document.evalBodies.front().tessellate(reopened.document.tessOpt);check(originalMesh.pos==loadedMesh.pos&&originalMesh.idx==loadedMesh.idx,"Geometry differs after large document persistence");
    apply(reopened,json(R"([{"op":"param.set","name":"width","expr":"14"}])"));check(reopened.document.getParam("width")==14&&reopened.document.iterationHistory.find("nodes")->arr.size()==previousNodes+2,"Editing after reopen still encountered a legacy quota or lost history");
    const auto stable=ark::documentToJson(reopened.document);V rejected=json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"width","expr":"999"}]})");check(reopened.call("doc_apply",rejected).find("isError")->b&&ark::documentToJson(reopened.document)==stable,"Stale revision did not retain the complete large document");
    V destination=V::makeStr("existing document retained");std::string error;check(!ark::loadDocumentContainer(filename.c_str(),destination,&error,nullptr,nullptr,nullptr,64*1024)&&destination.str=="existing document retained"&&error.find("DOCUMENT_READ_RESOURCE_PRESSURE")!=std::string::npos,"Explicit small read budget did not reject atomically");
    std::cout<<"Large document 072A: "<<checks<<" checks passed; logical_bytes="<<beforeSave.size()<<"; semantic_version="<<semantic<<"; retained_nodes="<<reopened.document.iterationHistory.find("nodes")->arr.size()<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<"Large document 072A: "<<error.what()<<'\n';return 1;}}
