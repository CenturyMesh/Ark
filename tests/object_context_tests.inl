void testObjectContext(){
    AgentSession context;
    CHECK(!failed(context.call("doc_apply",json(R"({"expected_revision":0,"commands":[{"op":"param.set","name":"length","expr":"10"},{"op":"param.set","name":"stroke","expr":"length*2"},{"op":"param.set","name":"unrelated","expr":"7"},{"op":"feature.add","type":"box","name":"Spring","p":{"dx":"stroke","dy":4,"dz":4}},{"op":"feature.add","type":"transform","name":"Suspension","inputs":["Spring"],"p":{"tx":2}},{"op":"feature.add","type":"box","name":"Unrelated","p":{"dx":3,"dy":3,"dz":3}},{"op":"assembly.add","name":"Bike","feature":0},{"op":"assembly.add","name":"Front suspension","parent":"Bike","feature":"Suspension","fixed":true,"pattern":{"type":"linear","count":2,"dx":30,"dy":0,"dz":0}},{"op":"assembly.add","name":"Accessory","parent":"Bike","feature":"Unrelated","p":{"ty":10}}]})"))));
    V args=json(R"({"ids":["Suspension"],"expected_revision":1})");
    const auto frozen=documentToJson(context.document);const auto revision=context.revision;
    auto response=context.call("object_context",args);CHECK(!failed(response));
    V result=*response.find("structuredContent");CHECK(result.find("revision")->num==1&&result.find("objects")->arr.size()==1);
    CHECK(context.revision==revision&&documentToJson(context.document)==frozen);
    const V first=result.find("objects")->arr[0];CHECK(first.find("key")->str=="feature:2"&&first.find("changed")->b);
    CHECK(first.find("authored")->find("p")->find("tx")->str=="2");
    CHECK(first.find("references")->find("dependencies")->arr[0].str=="feature:1");
    CHECK(first.find("measured")->find("manufacturing_qualified")->b==false);
    args.set("known_fingerprints",*result.find("fingerprints"));
    response=context.call("object_context",args);CHECK(!failed(response));
    const V unchanged=response.find("structuredContent")->find("objects")->arr[0];CHECK(!unchanged.find("changed")->b&&!unchanged.find("authored")&&unchanged.find("id")->num==2);
    CHECK(!failed(context.call("doc_apply",json(R"({"expected_revision":1,"commands":[{"op":"param.set","name":"unrelated","expr":"9"},{"op":"feature.edit","id":"Unrelated","p":{"dx":5}}]})"))));
    CHECK(failed(context.call("object_context",args)));args.set("expected_revision",V::makeNum(context.revision));
    response=context.call("object_context",args);CHECK(!failed(response)&&!response.find("structuredContent")->find("objects")->arr[0].find("changed")->b);
    CHECK(!failed(context.call("doc_apply",json(R"({"expected_revision":2,"commands":[{"op":"param.set","name":"length","expr":"15"}]})"))));
    args.set("expected_revision",V::makeNum(context.revision));response=context.call("object_context",args);
    CHECK(!failed(response)&&response.find("structuredContent")->find("objects")->arr[0].find("changed")->b);
    V occurrenceArgs=json(R"({"node":"Front suspension","copy":1})");response=context.call("object_context",occurrenceArgs);CHECK(!failed(response));
    V occurrence=*response.find("structuredContent");CHECK(occurrence.find("objects")->arr.size()==1);
    const V selectedOccurrence=occurrence.find("objects")->arr[0];CHECK(selectedOccurrence.find("key")->str=="occurrence:2:1");
    CHECK(selectedOccurrence.find("occurrences")->arr.size()==1&&selectedOccurrence.find("occurrences")->arr[0].find("copy")->num==1);
    occurrenceArgs.set("known_fingerprints",*occurrence.find("fingerprints"));
    CHECK(!failed(context.call("doc_apply",json(R"({"expected_revision":3,"commands":[{"op":"assembly.edit","id":"Bike","p":{"tz":8}}]})"))));
    response=context.call("object_context",occurrenceArgs);CHECK(!failed(response)&&response.find("structuredContent")->find("objects")->arr[0].find("changed")->b);
    occurrenceArgs.set("copy",V::makeNum(2));CHECK(failed(context.call("object_context",occurrenceArgs)));
    CHECK(failed(context.call("object_context",json(R"({"ids":[999]})"))));
    CHECK(failed(context.call("object_context",json(R"({"node":999})"))));
    CHECK(failed(context.call("object_context",json(R"({"copy":0})"))));
    CHECK(failed(context.call("object_context",json(R"({"ids":[1,1]})"))));
    CHECK(failed(context.call("object_context",json(R"({"depth":3})"))));
    CHECK(failed(context.call("object_context",json(R"({"known_fingerprints":{"feature:1":3}})"))));
    response=context.call("object_context",json(R"({"query":"sUsPeNsIoN","type":"transform"})"));CHECK(!failed(response)&&response.find("structuredContent")->find("objects")->arr.size()==1);
    response=context.call("object_context",json(R"({"limit":1})"));CHECK(!failed(response));const auto* page=response.find("structuredContent");CHECK(page->find("truncated")->b&&page->find("next_after_id")->num==1);
    // A truncated discovery lists every match compactly so an agent selects ids instead of paging details.
    CHECK(page->find("index")&&page->find("index")->arr.size()==page->find("matched_count")->num&&page->find("index")->arr[1].find("id")->num==2&&page->find("index")->arr[1].find("name")&&page->find("index")->arr[1].find("type")&&!page->find("index")->arr[1].find("authored")&&page->find("next_action"));
    response=context.call("object_context",json(R"({"limit":1,"after_id":1})"));CHECK(!failed(response)&&response.find("structuredContent")->find("objects")->arr[0].find("id")->num==2);
    response=context.call("object_context",json(R"({"ids":[2],"depth":1,"limit":1})"));CHECK(!failed(response));CHECK(response.find("structuredContent")->find("objects")->arr[0].find("id")->num==2&&response.find("structuredContent")->find("truncated")->b);
    // Authored JSON key order cannot change a token. Loading the same recipes
    // with a different session revision preserves document-scoped keys/tokens.
    V canonicalRequest=json(R"({"ids":[2]})");const auto current=context.call("object_context",canonicalRequest);
    AgentSession reopened;std::string error;CHECK(documentFromJson(documentToJson(context.document,false,false),reopened.document,&error));reopened.document.recompute();reopened.revision=99;
    const auto loaded=reopened.call("object_context",canonicalRequest);CHECK(!failed(loaded)&&loaded.find("structuredContent")->find("objects")->arr[0].find("fingerprint")->str==current.find("structuredContent")->find("objects")->arr[0].find("fingerprint")->str);
    // Large original source and mesh buffers stay local. Their content remains
    // in the token even when the bounded authoring payload omits the buffers.
    AgentSession mesh;Feature body;body.name="Reference mesh";body.type=FeatureType::MeshBody;body.sourceData=V::makeObj();V vertices=V::makeArr();for(int i=0;i<300;++i)vertices.arr.push_back(V::makeNum(i));body.sourceData.set("positions",vertices);body.sourceData.set("indices",json("[0,1,2]"));mesh.document.addFeature(body);
    auto meshContext=mesh.call("object_context",json(R"({"ids":[1]})"));CHECK(!failed(meshContext));const auto& meshRow=meshContext.find("structuredContent")->find("objects")->arr[0];CHECK(meshRow.find("authored")->find("mesh")->find("omitted")->b);const auto hash=meshRow.find("fingerprint")->str;
    mesh.document.features[0].sourceData.set("indices",json("[1,0,2]"));meshContext=mesh.call("object_context",json(R"({"ids":[1]})"));CHECK(meshContext.find("structuredContent")->find("objects")->arr[0].find("fingerprint")->str!=hash);
    AgentSession sketch;CHECK(!failed(sketch.call("doc_apply",json(R"({"commands":[{"op":"sketch.set","name":"Profile","sketch":{"entities":[{"kind":"line","name":"Edge","dof":[0,0,10,0]}]}},{"op":"feature.add","type":"box","name":"Recipe","p":{"dx":10,"dy":3,"dz":3,"sketch":"Profile"}}]})"))));
    auto linkedSketch=sketch.call("object_context",json(R"({"ids":[1]})"));CHECK(!failed(linkedSketch));const auto sketchHash=linkedSketch.find("structuredContent")->find("objects")->arr[0].find("fingerprint")->str;
    CHECK(linkedSketch.find("structuredContent")->find("objects")->arr[0].find("references")->find("resources")->find("sketches:Profile"));
    CHECK(!failed(sketch.call("doc_apply",json(R"({"commands":[{"op":"sketch.patch","name":"Profile","entity":"Edge","anchor":1,"x":12}]})"))));linkedSketch=sketch.call("object_context",json(R"({"ids":[1]})"));CHECK(linkedSketch.find("structuredContent")->find("objects")->arr[0].find("fingerprint")->str!=sketchHash);
    // Duplicate semantic names reject rather than choosing a convenient part.
    mesh.document.features[0].name="Duplicate";body.name="Duplicate";mesh.document.addFeature(body);CHECK(failed(mesh.call("object_context",json(R"({"ids":["Duplicate"]})"))));
    bool found=false;for(const auto& tool:AgentSession::tools().arr)if(tool.find("name")->str=="object_context"){found=true;CHECK(tool.find("annotations")->find("readOnlyHint")->b);}CHECK(found);
}
void benchmarkObjectContext(){
    const char* path=std::getenv("ARK_CONTEXT_BENCHMARK");if(!path||!*path)return;
    AgentSession session;V args=V::makeObj();args.set("path",V::makeStr(path));const auto loaded=session.call("doc_load",args);CHECK(!failed(loaded));if(failed(loaded))return;
    const auto lowercase=[](std::string text){for(char& c:text)c=(char)std::tolower((unsigned char)c);return text;};
    std::string selected;for(const auto& feature:session.document.features)if(lowercase(feature.name).find("spring")!=std::string::npos||lowercase(feature.name).find("susp")!=std::string::npos||lowercase(feature.name).find("amort")!=std::string::npos){selected=feature.name;break;}
    V request=V::makeObj();if(!selected.empty()){V ids=V::makeArr();ids.arr.push_back(V::makeStr(selected));request.set("ids",ids);}else request.set("limit",V::makeNum(1));
    const auto start=std::chrono::steady_clock::now();auto full=session.call("object_context",request);const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();CHECK(!failed(full));if(failed(full))return;
    const V context=*full.find("structuredContent");request.set("known_fingerprints",*context.find("fingerprints"));const auto deltaStart=std::chrono::steady_clock::now();auto delta=session.call("object_context",request);const double deltaMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-deltaStart).count();CHECK(!failed(delta));
    std::cout<<"object_context_benchmark features="<<session.document.features.size()<<" full_document_json_bytes="<<documentToJson(session.document,false,false).size()<<" selected_context_bytes="<<context.dump().size()<<" unchanged_delta_bytes="<<delta.find("structuredContent")->dump().size()<<" selected_ms="<<elapsed<<" delta_ms="<<deltaMs<<'\n';
}
