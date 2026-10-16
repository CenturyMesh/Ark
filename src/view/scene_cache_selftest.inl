if(sceneCacheUiTest){
    using V=arkjson::Value;
    struct ResetSceneFixtureHistory {~ResetSceneFixtureHistory(){g_session.suppressIterationHistory(false);}} resetSceneFixtureHistory;
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    auto parse=[&](const char* source){V value;check(arkjson::parse(source,value),"Invalid scene cache fixture JSON");return value;};
    auto invoke=[&](const char* tool,V args){if(std::string(tool)!="native_inspect")args.set("expected_revision",V::makeNum((double)g_session.revision));auto result=runTool(tool,args);if(g_error)throw std::runtime_error(g_status);return *result.find("structuredContent");};
    auto apply=[&](V command){V args=V::makeObj(),commands=V::makeArr();commands.arr.push_back(command);args.set("commands",commands);return invoke("doc_apply",args);};
    auto partOf=[&](u64 feature){for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==feature)return g_instancePart[i];throw std::runtime_error("Scene cache fixture part disappeared");};
    auto bodyOf=[&](u64 feature){for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==feature)return i;throw std::runtime_error("Scene cache fixture occurrence disappeared");};
    auto digest=[&](){std::string bytes;auto add=[&](const void* source,size_t size){if(size)bytes.append((const char*)source,size);};
        for(size_t i=0;i<g_bodyIds.size();++i){add(&g_bodyIds[i],sizeof(u64));add(&g_instanceIds[i],sizeof(u64));add(&g_instanceCopies[i],sizeof(int));add(g_instanceWorld[i].m,sizeof(g_instanceWorld[i].m));size_t part=g_instancePart[i];const auto& mesh=g_cpuMeshes[part];add(mesh.pos.data(),mesh.pos.size()*sizeof(float));add(mesh.nrm.data(),mesh.nrm.size()*sizeof(float));add(mesh.idx.data(),mesh.idx.size()*sizeof(u32));const auto& pick=g_partPicks[part];
            for(const auto& name:pick.faceNames){bytes+=name;bytes+='\0';}for(const auto& name:pick.edgeNames){bytes+=name;bytes+='\0';}
            for(const auto& range:pick.faces){add(&range.first,sizeof(int));add(&range.second,sizeof(int));}
            for(const auto& edge:pick.edges)for(const auto& p:edge)for(double coordinate:{p.x,p.y,p.z})add(&coordinate,sizeof(double));
        }return iterationSha256(bytes);
    };
    auto equivalentFullRebuild=[&](){const auto reused=digest();g_sceneReuseEnabled=false;rebuildScene();check(digest()==reused,"Cached scene differs from forced rebuild mesh/normals/semantic references/occurrences");g_sceneReuseEnabled=true;};
    try{
        g_sceneReuseEnabled=true;invoke("doc_new",V::makeObj());g_assistantDockOpen=false;layout();
        invoke("doc_apply",parse(R"({"commands":[{"op":"feature.add","name":"Cache box","type":"native_solid","native":{"op":"box"},"p":{"width":20,"depth":10,"height":8}},{"op":"feature.add","name":"Cache cylinder","type":"native_solid","native":{"op":"cylinder"},"p":{"radius":4,"height":12}},{"op":"assembly.add","name":"Box occurrence","feature":1},{"op":"assembly.add","name":"Cylinder occurrence","feature":2,"p":{"tx":60}}]})"));
        check(g_cpuMeshes.size()==2&&g_sceneRebuiltParts==2,"Native fixture must build both independent definitions");
        const auto boxSerial=g_meshes[partOf(1)].serial,cylinderSerial=g_meshes[partOf(2)].serial;rebuildScene();
        check(g_sceneReusedParts==2&&g_sceneRebuiltParts==0&&g_meshes[partOf(1)].serial==boxSerial&&g_meshes[partOf(2)].serial==cylinderSerial,"Unchanged scene did not preserve both GPU representations");
        apply(parse(R"({"op":"feature.edit","id":1,"p":{"width":24}})"));
        check(g_sceneRebuiltParts==1&&g_sceneReusedParts==1&&g_meshes[partOf(1)].serial!=boxSerial&&g_meshes[partOf(2)].serial==cylinderSerial,"Native edit invalidation must follow the changed definition");equivalentFullRebuild();
        invoke("doc_undo",V::makeObj());check(std::abs(g_doc.find(1)->num(g_doc.buildEnv(),"width",0)-20)<1e-9,"Undo did not restore native source");equivalentFullRebuild();
        invoke("doc_redo",V::makeObj());check(std::abs(g_doc.find(1)->num(g_doc.buildEnv(),"width",0)-24)<1e-9,"Redo did not restore native edit");equivalentFullRebuild();
        // Subsequent cache-identity probes deliberately perturb private fixture
        // authoring/derived state. Their mutations are not history operations.
        g_session.suppressIterationHistory(true);g_doc.iterationHistory=V{};
        selBody=(int)bodyOf(1);const auto selectedNode=g_instanceIds[(size_t)selBody];
        const auto beforeOrderBox=g_meshes[partOf(1)].serial,beforeOrderCylinder=g_meshes[partOf(2)].serial;
        // Isolated fixture perturbation exercises representation moves across
        // part indices independently of the history command implementation.
        std::swap(g_doc.assembly[0],g_doc.assembly[1]);rebuildScene();
        check(g_sceneReusedParts==2&&g_meshes[partOf(1)].serial==beforeOrderBox&&g_meshes[partOf(2)].serial==beforeOrderCylinder,"Part-index reorder lost representation custody");
        check(selBody>=0&&g_bodyIds[(size_t)selBody]==1&&g_instanceIds[(size_t)selBody]==selectedNode,"Part-index reorder retargeted the selected occurrence");equivalentFullRebuild();
        apply(parse(R"({"op":"assembly.edit","id":1,"p":{"tx":25}})"));
        check(g_sceneReusedParts==2&&g_sceneRebuiltParts==0&&std::abs(g_instanceWorld[bodyOf(1)].transformPoint({0,0,0}).x-25)<1e-9,"Occurrence edit did not reuse geometry and update placement");equivalentFullRebuild();
        const auto appearanceSerial=g_meshes[partOf(1)].serial;apply(parse(R"({"op":"feature.edit","id":1,"appearance":{"base_color":[0.1,0.8,0.2]}})"));check(g_meshes[partOf(1)].serial==appearanceSerial,"Appearance edit unnecessarily replaced geometry");
        // Actual BVH ray hit and semantic selection after index/placement moves.
        frameScene();target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;drawFrame(hdc);bool picked=false;
        for(int y=viewY+20;y<viewY+viewH-20&&!picked;y+=7)for(int x=viewX+20;x<viewX+viewW-20&&!picked;x+=7){if(overViewCube(x,y))continue;auto hit=pickSurface(x,y);if(!hit.hit||g_bodyIds[(size_t)hit.body]!=1)continue;g_pickKind=1;g_picked.clear();pickReference(x,y);picked=g_picked.size()==1&&g_picked[0].feature==1&&g_picked[0].instance==1&&!g_picked[0].name.empty();}
        check(picked&&!g_selectionFaceFill.empty(),"Reused scene no longer supports named face picking and selection geometry");
        const size_t mutated=partOf(1);auto authoritative=g_cpuMeshes[mutated];auto altered=authoritative;check(!altered.nrm.empty(),"Native fixture has no normals");altered.nrm[0]=-altered.nrm[0]+.25f;g_cpuMeshes[mutated]=altered;uploadMesh(g_meshes[mutated],altered);rebuildScene();
        check(g_sceneRebuiltParts==1&&g_sceneReusedParts==1&&g_cpuMeshes[partOf(1)].nrm==authoritative.nrm,"GPU/display mutation was reused as authoritative geometry");
        // New immutable cache body with identical generation/positions but
        // changed normals must not alias the previous representation.
        auto& entry=g_doc.evaluationCache.at(1);const auto generation=entry.generation;auto replacement=std::make_shared<Body>(*entry.body);check(replacement->baked&&!replacement->baked->nrm.empty(),"Expected baked native test body");replacement->baked.mutate([](Mesh& mesh){mesh.nrm[0]+=.5f;});entry.body=replacement;
        for(auto& body:g_doc.evalBodies)if(body.id==1)body=*replacement;rebuildScene();
        check(entry.generation==generation&&g_sceneRebuiltParts==1&&g_cpuMeshes[partOf(1)].nrm==replacement->baked->nrm,"Same-generation normal-only replacement did not invalidate");equivalentFullRebuild();
        const std::string saved=documentToJson(g_doc);Document loaded;std::string error;check(documentFromJson(saved,loaded,&error),"Synthetic document reload parse failed");loaded.recompute();g_doc=std::move(loaded);rebuildScene();check(g_sceneRebuiltParts==2&&g_sceneReusedParts==0,"Reload with colliding IDs reused a foreign evaluation body");equivalentFullRebuild();
        std::vector<GLuint> oldBuffers;for(const auto& mesh:g_meshes){oldBuffers.push_back(mesh.vbo);oldBuffers.push_back(mesh.ibo);}for(const auto& edge:g_edgeLines)oldBuffers.push_back(edge.vbo);
        g_doc=Document{};rebuildScene();check(g_meshes.empty()&&g_scenePartCache.empty()&&g_partPicks.empty(),"Unload retained stale scene ownership");
        using IsBuffer=GLboolean(APIENTRY*)(GLuint);auto isBuffer=(IsBuffer)wglGetProcAddress("glIsBuffer");check(isBuffer!=nullptr,"OpenGL buffer query unavailable");for(auto id:oldBuffers)check(!isBuffer(id),"Unloading leaked a GPU vertex/index/edge buffer");
        check(documentFromJson(saved,g_doc,&error),"Reload after unload failed");g_doc.recompute();rebuildScene();check(g_sceneRebuiltParts==2&&g_sceneReusedParts==0,"Empty scene reused stale parts after reload");
        // Read-only analytic promotion changes semantic references without
        // changing evaluationCache's immutable body pointer.
        invoke("doc_new",V::makeObj());apply(parse(R"({"op":"feature.add","name":"Analytic promotion","type":"box","p":{"dx":10,"dy":12,"dz":14}})"));
        const auto priorPointer=g_doc.evaluationCache.at(1).body;invoke("native_inspect",parse(R"({"id":1})"));rebuildScene();
        check(g_doc.evaluationCache.at(1).body==priorPointer,"Promotion fixture unexpectedly replaced evaluation identity");
        check(!g_partPicks[partOf(1)].faceNames.empty(),"Read-only native promotion left stale empty semantic picks");equivalentFullRebuild();
        check(glGetError()==GL_NO_ERROR,"Scene reuse test generated an OpenGL error");
        g_status="Scene cache checks passed / native edit / reorder / placement / undo-redo / normals / GPU invalidation / reload / semantic picks";
    }catch(const std::exception& error){fprintf(stderr,"Scene cache UI: %s\n",error.what());return 947;}
}
