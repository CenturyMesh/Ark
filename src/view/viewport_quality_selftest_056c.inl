// A.04/B.01: real GL buffers, synthetic native source, actual graphics controls
// and framebuffer comparisons. No personal document/settings are loaded.
static int viewportQualitySelfTest056(HDC dc,const std::string& output){
    using V=arkjson::Value;using Clock=std::chrono::steady_clock;V receipt=V::makeObj();
    receipt.set("schema",V::makeStr("ark.viewport-quality.056c.v1"));receipt.set("gpu",V::makeStr((const char*)glGetString(GL_RENDERER)));receipt.set("gl",V::makeStr((const char*)glGetString(GL_VERSION)));
    auto persist=[&]{std::ofstream file(output+".quality.json");file<<receipt.dump()<<'\n';};
    auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    try{
        Mesh mesh;for(int y=0;y<32;++y)for(int x=0;x<32;++x){mesh.pos.insert(mesh.pos.end(),{(float)x,(float)y,(float)(.01*x*y)});if(x<31&&y<31){u32 a=(u32)(y*32+x);mesh.idx.insert(mesh.idx.end(),{a,a+1,a+32,a+1,a+33,a+32});}}mesh.computeNormals();
        const auto original=mesh;auto packed=packDisplayMesh056(mesh);double normalError=0;
        for(size_t i=0;i<packed.size();++i){check(std::memcmp(packed[i].p,mesh.pos.data()+3*i,12)==0,"GPU packing changed a display position");Vec3 n{mesh.nrm[3*i],mesh.nrm[3*i+1],mesh.nrm[3*i+2]};n=n/n.len();normalError=std::max(normalError,(n-Vec3{packed[i].n[0]/32767.,packed[i].n[1]/32767.,packed[i].n[2]/32767.}).len());}
        check(normalError<.000027,"SNORM16 error exceeded the three-component quantization bound");
        GPUMesh first,second;const auto uploads=g_displayMeshUploads056;uploadMesh(first,mesh);uploadMesh(second,mesh);
        check(first.vao==second.vao&&first.ibo==second.ibo&&g_displayMeshUploads056==uploads+1,"Identical content allocated duplicate GPU meshes");
        auto parameters=(void(APIENTRY*)(GLenum,GLenum,GLint*))(void*)wglGetProcAddress("glGetBufferParameteriv");check(parameters!=nullptr,"Buffer-size query unavailable");GLint bufferBytes=0;glBindBuffer(GL_ARRAY_BUFFER,first.vbo);parameters(GL_ARRAY_BUFFER,0x8764,&bufferBytes);
        check(bufferBytes==(GLint)(mesh.vertCount()*20),"Driver vertex allocation did not use the packed format");
        const GLuint shared=second.vao;releaseMesh(first);trimDisplayMeshCache056(0);check(g_displayMeshCache056.count(second.cacheKey)==1,"Eviction released an active shared GPU mesh");
        releaseMesh(second);uploadMesh(first,mesh);check(first.vao==shared&&g_displayMeshUploads056==uploads+1,"Idle cache did not reuse released mesh content");
        auto changed=mesh;changed.pos[0]+=.125f;uploadMesh(second,changed);check(first.vao!=second.vao,"Changed positions aliased a stale GPU mesh");
        releaseMesh(first);releaseMesh(second);trimDisplayMeshCache056(0);check(g_displayMeshIdleBytes056==0,"Idle cache clear retained unused resources");
        check(mesh.pos==original.pos&&mesh.nrm==original.nrm&&mesh.idx==original.idx,"GPU caching mutated CPU geometry");
        receipt.set("vertex_bytes_before",V::makeNum(mesh.vertCount()*24));receipt.set("vertex_bytes_after_driver_query",V::makeNum(bufferBytes));receipt.set("unchanged_index_bytes",V::makeNum(mesh.idx.size()*4));receipt.set("max_normal_component_vector_error",V::makeNum(normalError));receipt.set("cache_sharing_idle_reuse_invalidation_eviction",V::makeBool(true));
        auto parse=[&](const char* text){V v;check(arkjson::parse(text,v),"Invalid synthetic quality fixture");return v;};
        V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",args);check(!g_error,"Synthetic document reset failed");
        args=parse(R"({"commands":[
        {"op":"feature.add","type":"native_solid","name":"Quality base","native":{"op":"box"},"p":{"width":140,"depth":100,"height":10}},
        {"op":"feature.add","type":"native_solid","name":"Quality sphere","native":{"op":"sphere"},"p":{"radius":24},"appearance":{"base_color":[0.18,0.46,0.68],"roughness":0.3}},
        {"op":"feature.add","type":"native_solid","name":"Quality block","native":{"op":"box"},"p":{"width":26,"depth":30,"height":35},"appearance":{"base_color":[0.82,0.37,0.13]}},
        {"op":"assembly.add","name":"Base","feature":1,"p":{"tx":-70,"ty":0,"tz":50,"rx":-90}},
        {"op":"assembly.add","name":"Sphere","feature":2,"p":{"tx":-22,"ty":34,"tz":0}},
        {"op":"assembly.add","name":"Block","feature":3,"p":{"tx":8,"ty":10,"tz":10,"rx":-90}}
        ]})");args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",args);check(!g_error&&g_bodyIds.size()==3,"Synthetic exact quality fixture failed");
        g_assistantDockOpen=false;g_toolsOpen=false;g_showViewCube=false;g_showEdges=true;g_grid=false;g_lightBackground=true;g_showSketches=false;g_showDatumPlanes=false;g_cameraOrtho=false;g_pathTracing=false;wireframe=false;g_picked.clear();selBody=hoverBody=-1;layout();frameScene();target=tTarget;dist=tDist;yaw=tYaw=.67;pitch=tPitch=.39;cameraRoll=0;g_swap=false;
        const auto source=documentToJson(g_doc);const u64 revision=g_session.revision;
        auto capture=[&](const std::string& suffix){drawFrame(dc);glFinish();check(glGetError()==GL_NO_ERROR,"OpenGL error during quality render");Image image;image.w=winW;image.h=winH;image.px.resize((size_t)winW*winH*3);std::vector<u8> raw(image.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,raw.data());for(int y=0;y<winH;++y)std::memcpy(image.px.data()+(size_t)y*winW*3,raw.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);check(writePNG((output+suffix).c_str(),image),"Quality capture could not be written");return image;};
        auto difference=[&](const Image& a,const Image& b){size_t count=0;for(int y=viewY;y<viewY+viewH;++y)for(int x=viewX;x<viewX+viewW;++x){size_t i=((size_t)y*winW+x)*3;int delta=0;for(int c=0;c<3;++c)delta+=std::abs((int)a.px[i+c]-b.px[i+c]);if(delta>3)++count;}return count;};
        g_antialiasing=0;g_occlusionMode=0;g_contactShadowStrength=0;const auto off=capture(".quality-off.png");
        // Matched actual framebuffer comparison against the previous float32
        // vertex format. Keep source, indices, camera, settings and GPU fixed.
        auto packedHandles=std::move(g_meshes);g_meshes.resize(g_cpuMeshes.size());
        for(size_t part=0;part<g_cpuMeshes.size();++part){const auto& cpu=g_cpuMeshes[part];auto& gpu=g_meshes[part];std::vector<float> vertices;for(size_t i=0;i<cpu.vertCount();++i){vertices.insert(vertices.end(),cpu.pos.begin()+3*i,cpu.pos.begin()+3*i+3);vertices.insert(vertices.end(),cpu.nrm.begin()+3*i,cpu.nrm.begin()+3*i+3);}glGenVertexArrays(1,&gpu.vao);glGenBuffers(1,&gpu.vbo);glGenBuffers(1,&gpu.ibo);glBindVertexArray(gpu.vao);glBindBuffer(GL_ARRAY_BUFFER,gpu.vbo);glBufferData(GL_ARRAY_BUFFER,vertices.size()*4,vertices.data(),GL_STATIC_DRAW);glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,24,nullptr);glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(void*)12);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,gpu.ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,cpu.idx.size()*4,cpu.idx.data(),GL_STATIC_DRAW);gpu.count=(int)cpu.triCount();}
        const auto uncompressed=capture(".quality-uncompressed-reference.png");for(auto& gpu:g_meshes)releaseMesh(gpu);g_meshes=std::move(packedHandles);
        int maximumColorError=0;for(int y=viewY;y<viewY+viewH;++y)for(int x=viewX;x<viewX+viewW;++x)for(int c=0;c<3;++c){size_t i=((size_t)y*winW+x)*3+c;maximumColorError=std::max(maximumColorError,std::abs((int)off.px[i]-uncompressed.px[i]));}
        receipt.set("packed_vs_float32_max_rgb8_error",V::makeNum(maximumColorError));check(maximumColorError<=2,"Packed-normal rendering diverged from float32 reference");
        g_antialiasing=1;const auto aa=capture(".quality-fxaa.png");check(g_viewportAaReady056&&difference(off,aa)>100,"FXAA did not change visible edges");
        g_antialiasing=2;const auto hq=capture(".quality-fxaa-hq.png");check(difference(aa,hq)>10,"FXAA high quality did not change its sampling");
        g_antialiasing=0;g_contactShadowStrength=.65f;const auto shadow=capture(".quality-shadows.png");check(difference(off,shadow)>100,"Screen-space contact shadows had no visible effect");
        g_occlusionMode=3;g_contactShadowStrength=.28f;g_antialiasing=2;const auto final=capture(".quality-combined.png");check(difference(off,final)>100,"Combined quality effects had no visible effect");
        receipt.set("aa_changed_pixels",V::makeNum(difference(off,aa)));receipt.set("aa_hq_changed_pixels",V::makeNum(difference(aa,hq)));receipt.set("contact_shadow_changed_pixels",V::makeNum(difference(off,shadow)));
        // Real native controls: dropdown selection, Apply, Cancel and validation.
        g_dialogSmoke=3;g_dialogOverrides={"0","0","1","1","0","2","0.72","1","0","FXAA high quality","0.4"};g_dialogShot=output+".quality-controls.png";graphicsDialog();g_dialogSmoke=0;
        check(g_antialiasing==2&&std::abs(g_contactShadowStrength-.4f)<1e-6,"Graphics controls failed to apply new quality values");
        g_dialogSmoke=2;g_dialogOverrides={"0","0","1","1","0","2","0.72","1","0","Off","0"};graphicsDialog();g_dialogSmoke=0;check(g_antialiasing==2&&std::abs(g_contactShadowStrength-.4f)<1e-6,"Cancel applied graphics values");
        bool rejected=false;g_dialogSmoke=3;g_dialogOverrides={"0","0","1","1","0","2","0.72","1","0","Off","2"};try{graphicsDialog();}catch(...){rejected=true;}g_dialogSmoke=0;check(rejected&&g_antialiasing==2,"Invalid graphics values partially changed settings");
        auto settings=g_settingsOverride;g_settingsOverride=output+".quality-settings.json";saveUiSettings();g_antialiasing=0;g_contactShadowStrength=0;loadUiSettings();g_settingsOverride=settings;check(g_antialiasing==2&&std::abs(g_contactShadowStrength-.4f)<1e-6,"Quality settings did not round-trip");
        V timings=V::makeArr();for(int mode=0;mode<=2;++mode){g_antialiasing=mode;drawFrame(dc);glFinish();std::vector<double> ms;for(int sample=0;sample<21;++sample){auto start=Clock::now();drawFrame(dc);glFinish();ms.push_back(std::chrono::duration<double,std::milli>(Clock::now()-start).count());}std::sort(ms.begin(),ms.end());V row=V::makeObj();row.set("aa_mode",V::makeNum(mode));row.set("median_ms",V::makeNum(ms[10]));row.set("p95_ms",V::makeNum(ms[19]));timings.arr.push_back(row);}receipt.set("completed_frame_timings",timings);
        check(source==documentToJson(g_doc)&&revision==g_session.revision,"Visual settings mutated authored CAD source");
        const auto uploadsBefore=g_displayMeshUploads056;rebuildScene();check(g_sceneReusedParts==3&&g_displayMeshUploads056==uploadsBefore,"Unchanged exact fixture uploaded geometry again");
        receipt.set("source_unchanged",V::makeBool(true));receipt.set("controls_apply_cancel_validation_persistence",V::makeBool(true));receipt.set("timing_scope",V::makeStr("Same synthetic exact assembly, fixed camera/size, 21 frames per AA mode, steady_clock drawFrame plus glFinish; CPU plus completed GPU, no monitor/compositor timing or speedup claim."));receipt.set("passed",V::makeBool(true));persist();return 0;
    }catch(const std::exception& e){g_dialogSmoke=0;g_dialogOverrides.clear();g_dialogShot.clear();receipt.set("passed",V::makeBool(false));receipt.set("error",V::makeStr(e.what()));persist();std::fprintf(stderr,"Viewport quality failed: %s\n",e.what());return 956;}
}
