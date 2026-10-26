// Include after inertial_panel_056c.inl. Shared editor/vector helpers and the
// existing background exact-read scheduler keep this a native Studio module.
static bool rubberCurrent059(){const auto& p=g_rubber059;return !p.stale&&!p.frames.empty()&&p.revision==g_session.revision&&p.epoch==g_objectDocumentEpoch;}
static void releaseRubberGpu059(){auto& p=g_rubber059;releaseMesh(p.gpu);p.shownFrame=~size_t(0);}
static void stopRubberDisplay059(){auto& p=g_rubber059;p.active=p.playing=false;p.time=0;p.shownFrame=~size_t(0);}
static void cancelRubber059(){auto& p=g_rubber059;if(!p.running)return;*p.cancelled=true;p.cancelling=true;if(p.job)cancelExactJob046B(p.job);p.message="Cancelling the private study; its result will be discarded.";}
static void invalidateRubber059(const std::string& reason){auto& p=g_rubber059;stopRubberDisplay059();cancelRubber059();p.stale=p.running||!p.frames.empty();p.frames.clear();p.mesh.clear();releaseRubberGpu059();p.message=reason;p.error.clear();}
static int rubberOccurrence059(){const auto& p=g_rubber059;for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==p.id&&g_instanceIds[i]==p.node&&g_instanceCopies[i]==p.copy)return (int)i;return -1;}
static bool rubberDisplayAllowed059(std::string& reason){
    if(!inertialDisplayAllowed056(reason))return false;
    if(g_motionActive||g_bakedMotions.active||g_inertial056.active||explodedDisplayActive()){reason="Stop the other motion preview before the local rubber study.";return false;}return true;
}
static size_t rubberFrameIndex059(){const auto& p=g_rubber059;auto it=std::upper_bound(p.frames.begin(),p.frames.end(),p.time,[](double time,const RubberFrame059& f){return time<f.time;});return it==p.frames.begin()?0:(size_t)(it-p.frames.begin()-1);}
static void updateRubberMesh059(){auto& p=g_rubber059;if(!p.active||!rubberCurrent059())return;const size_t frame=rubberFrameIndex059();if(p.shownFrame==frame)return;
    p.mesh.pos=p.frames[frame].positions;p.mesh.computeNormals();auto packed=packDisplayMesh056(p.mesh);
    // Animated frames use one dedicated dynamic buffer; never populate the
    // immutable CAD display cache or change its generation/picking metadata.
    if(!p.gpu.vao){glGenVertexArrays(1,&p.gpu.vao);glGenBuffers(1,&p.gpu.vbo);glGenBuffers(1,&p.gpu.ibo);glBindVertexArray(p.gpu.vao);glBindBuffer(GL_ARRAY_BUFFER,p.gpu.vbo);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(PackedDisplayVertex056),nullptr);glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_SHORT,GL_TRUE,sizeof(PackedDisplayVertex056),(void*)12);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,p.gpu.ibo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,(GLsizeiptr)(p.mesh.idx.size()*sizeof(u32)),p.mesh.idx.data(),GL_STATIC_DRAW);p.gpu.count=(int)p.mesh.triCount();
    }else{glBindVertexArray(p.gpu.vao);glBindBuffer(GL_ARRAY_BUFFER,p.gpu.vbo);}
    glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(packed.size()*sizeof(PackedDisplayVertex056)),packed.data(),GL_DYNAMIC_DRAW);glBindVertexArray(0);p.shownFrame=frame;++p.uploads;
}
static bool startRubberDisplay059(){auto& p=g_rubber059;std::string reason;
    if(!rubberCurrent059()){p.message="Run a study on the current rubber definition first.";return false;}
    if(!rubberDisplayAllowed059(reason)){p.message=reason;return false;}const int occurrence=rubberOccurrence059();
    if(occurrence<0||!displayInstance((size_t)occurrence)){p.message="The studied occurrence is not visible. Show it before playback.";return false;}
    for(int k=0;k<16;++k)if(p.world.m[k]!=g_instanceWorld[(size_t)occurrence].m[k]){p.message="The displayed occurrence pose changed. Run the study again.";return false;}
    p.active=true;g_pathTracing=false;updateRubberMesh059();return true;
}
static void advanceRubber059(double elapsed){auto& p=g_rubber059;
    if((p.running||p.active||!p.frames.empty())&&(p.revision!=g_session.revision||p.epoch!=g_objectDocumentEpoch)){invalidateRubber059("Drawing changed. Previous rubber results are stale; run again.");return;}
    if(p.open&&(g_inertial056.open||g_bakedMotions.open||g_explode.open||g_drawingViewport.active)){stopRubberDisplay059();p.open=false;layout();}
    if(!p.active)return;std::string reason;int occurrence=rubberOccurrence059();
    if(!rubberDisplayAllowed059(reason)||occurrence<0||!displayInstance((size_t)occurrence)){stopRubberDisplay059();p.message=reason.empty()?"Studied occurrence hidden; preview reset.":reason;return;}
    if(p.playing){p.time=std::min(p.frames.back().time,p.time+elapsed);if(p.time>=p.frames.back().time)p.playing=false;}updateRubberMesh059();
}
static void createRubber059(){using V=arkjson::Value;
    std::vector<EditorField> type{{"Body","Belt",false,{"Belt","Track","Tire","Ball"}},{"Name","Rubber body"}};
    if(!editFields("Rubber / Create","Editable exact stock. Relief and procedural appearance can be authored separately.",type))return;
    std::string kind=type[0].value;std::transform(kind.begin(),kind.end(),kind.begin(),[](unsigned char c){return (char)std::tolower(c);});
    std::vector<std::pair<std::string,std::string>> dims;
    if(kind=="belt"||kind=="track")dims={{"center_distance_mm","120"},{"radius_mm","35"},{"thickness_mm","4"},{"width_mm","25"}};
    else if(kind=="tire")dims={{"major_radius_mm","60"},{"radial_radius_mm","18"},{"axial_radius_mm","14"},{"wall_mm","4"}};
    else if(kind=="ball")dims={{"radius_mm","30"},{"wall_mm","3"}};else throw std::runtime_error("Choose Belt, Track, Tire or Ball.");
    const auto name=trimField(type[1].value);if(name.empty()||name.size()>128)throw std::runtime_error("Use a body name of 1..128 bytes.");
    std::vector<EditorField> fields;for(const auto& d:dims)fields.push_back({d.first,d.second});fields.push_back({"Declared density / kg/m3","1100"});fields.push_back({"Declared Young modulus / Pa","3000000"});
    if(!editFields("Rubber / Dimensions and material","mm, or named CAD expressions. Density 1100 and E 3 MPa are illustrative defaults, not measured rubber properties. A separate declared material is created.",fields))return;
    const double density=formNumber(fields[dims.size()].value),young=formNumber(fields[dims.size()+1].value);if(density<=0||density>100000||young<=0||young>1e11)throw std::runtime_error("Declare positive density <=100000 kg/m3 and Young modulus <=1e11 Pa.");
    std::string material="Rubber study declared";for(unsigned suffix=1;std::any_of(g_doc.materials.begin(),g_doc.materials.end(),[&](const Material& m){return m.name==material;});++suffix)material="Rubber study declared "+std::to_string(suffix);
    V commands=V::makeArr(),mat=V::makeObj(),physical=V::makeObj(),body=V::makeObj(),native=V::makeObj(),parameters=V::makeObj(),appearance=V::makeObj();
    mat.set("op",V::makeStr("material.set"));mat.set("name",V::makeStr(material));mat.set("density_kg_m3",V::makeNum(density));physical.set("young_modulus_Pa",V::makeNum(young));mat.set("physical",physical);commands.arr.push_back(mat);
    body.set("op",V::makeStr("feature.add"));body.set("type",V::makeStr("native_solid"));body.set("name",V::makeStr(name));body.set("material",V::makeStr(material));native.set("op",V::makeStr("rubber_body"));native.set("kind",V::makeStr(kind));body.set("native",native);
    for(size_t i=0;i<dims.size();++i)parameters.set(dims[i].first,V::makeStr(fields[i].value));body.set("p",parameters);appearance.set("base_color",inertialVector056({.055,.065,.075}));appearance.set("roughness",V::makeNum(.82));appearance.set("metallic",V::makeNum(0));body.set("appearance",appearance);commands.arr.push_back(body);
    const u64 id=g_doc.nextFeatureId_;if(!g_doc.assembly.empty()){V instance=V::makeObj();instance.set("op",V::makeStr("assembly.add"));instance.set("feature",V::makeNum((double)id));instance.set("name",V::makeStr(name));commands.arr.push_back(instance);}
    V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));args.set("commands",commands);runTool("doc_apply",args);
    if(!g_error){for(size_t i=0;i<g_bodyIds.size();++i)if(g_bodyIds[i]==id){selBody=(int)i;break;}g_picked.clear();refreshSelectionLines();frameScene();g_rubber059.message="Exact rubber stock created with the declared study material. Configure and Run.";}
}
static void rubberStudy059(){auto& p=g_rubber059;std::vector<EditorField> f{{"Solver",p.backend=="gpu"?"GPU":"CPU reference",false,{"GPU","CPU reference"}},{"Duration / s",inertialScalarText056(p.duration)},{"Step / s",inertialScalarText056(p.step)},{"Output frames / s",std::to_string(p.fps)},{"Solver iterations",std::to_string(p.iterations)},{"Around nodes",std::to_string(p.around)},{"Rows",std::to_string(p.rows)}};
    if(!editFields("Rubber / Study","Reduced surface network, not calibrated continuum FEA. The selected occurrence displays a study in the definition's local axes.",f))return;
    double d=formNumber(f[1].value),step=formNumber(f[2].value),fps=formNumber(f[3].value),it=formNumber(f[4].value),around=formNumber(f[5].value),rows=formNumber(f[6].value);
    auto integer=[](double n,double lo,double hi){return std::isfinite(n)&&n>=lo&&n<=hi&&n==std::floor(n);};
    if((f[0].value!="GPU"&&f[0].value!="CPU reference")||d<1e-5||d>10||step<1e-5||step>1./30||d/step>14400||!integer(fps,1,60)||!integer(it,1,32)||!integer(around,8,128)||!integer(rows,3,32)||around*rows>4096||(std::ceil(d*fps)+1)*(around*rows+2)>262144)throw std::runtime_error("Use 0.00001..10 s, step 0.00001..1/30 s (<=14400 steps), fps 1..60, iterations 1..32, around 8..128, rows 3..32, <=4096 nodes and <=262144 node frames.");
    size_t steps=0;double previous=0;for(size_t i=1;i<=(size_t)std::ceil(d*fps);++i){double next=std::min(d,(double)i/fps);steps+=(size_t)std::ceil((next-previous)/step-1e-10);previous=next;}if(steps>14400)throw std::runtime_error("Sample-aligned integration exceeds 14400 steps; reduce duration or output rate.");
    invalidateRubber059("Study settings changed. Run to compute new results.");p.backend=f[0].value=="GPU"?"gpu":"cpu";p.duration=d;p.step=step;p.fps=(unsigned)fps;p.iterations=(unsigned)it;p.around=(unsigned)around;p.rows=(unsigned)rows;
}
static void rubberEnvironment059(){auto& p=g_rubber059;std::vector<EditorField> f{{"Local gravity XYZ / m/s2",inertialVectorText056(p.gravity)},{"Local start translation / mm",inertialVectorText056(p.translation)},{"Initial velocity XYZ / m/s",inertialVectorText056(p.velocity)},{"Local ground plane",p.ground?"1":"0",true},{"Local ground Z / mm",inertialScalarText056(p.groundZ)},{"Friction / 0..2",inertialScalarText056(p.friction)},{"Damping / 1/s",inertialScalarText056(p.damping)}};
    if(!editFields("Rubber / Local environment","All axes belong to the rubber definition. A rotated occurrence rotates this entire study; this is not an assembly contact solver.",f))return;
    auto gravity=inertialVectorField056(f[0].value),translation=inertialVectorField056(f[1].value),velocity=inertialVectorField056(f[2].value);const double z=formNumber(f[4].value),friction=formNumber(f[5].value),damping=formNumber(f[6].value);
    if(!std::isfinite(gravity.len())||!std::isfinite(translation.len())||!std::isfinite(velocity.len())||translation.len()>=1e7||velocity.len()>100||!std::isfinite(z)||friction<0||friction>2||damping<0||damping>100)throw std::runtime_error("Use finite XYZ, start translation <1e7 mm, speed <=100 m/s, friction 0..2 and damping 0..100.");
    invalidateRubber059("Local environment changed. Run to compute new results.");p.gravity=gravity;p.translation=translation;p.velocity=velocity;p.ground=f[3].value=="1";p.groundZ=z;p.friction=friction;p.damping=damping;
}
static void acceptRubber059(const arkjson::Value& result,u64 revision,u64 epoch,u64 generation){using V=arkjson::Value;auto& p=g_rubber059;if(generation!=p.generation)return;p.running=false;p.job=0;
    if(revision!=g_session.revision||epoch!=g_objectDocumentEpoch){p.stale=true;p.message="Drawing changed while solving. Stale result discarded.";return;}
    if(p.cancelling||*p.cancelled){p.cancelling=false;p.message="Rubber study cancelled; CAD unchanged.";g_error=false;return;}
    try{auto require=[](bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);};const auto* data=result.find("structuredContent");require(data&&data->t==V::Obj&&!(result.find("isError")&&result.find("isError")->b),data&&data->find("error")?data->find("error")->asStr().c_str():"Rubber study returned no result.");
        const auto *rev=data->find("revision"),*hash=data->find("source_sha256"),*id=data->find("id"),*nodes=data->find("nodes_count"),*tris=data->find("triangles"),*frames=data->find("frames");
        require(rev&&rev->t==V::Num&&rev->num==(double)revision&&hash&&hash->t==V::Str&&hash->str==p.sourceSha256&&id&&id->t==V::Num&&id->num==(double)p.id,"Rubber result has a different source revision, fingerprint or definition.");
        require(nodes&&nodes->t==V::Num&&nodes->num>=3&&nodes->num<=4096&&std::floor(nodes->num)==nodes->num,"Rubber node count is invalid.");const size_t count=(size_t)nodes->num;
        require(tris&&tris->t==V::Arr&&!tris->arr.empty()&&tris->arr.size()%3==0&&tris->arr.size()<=6*4096,"Rubber triangles exceed the supported network.");Mesh mesh;mesh.idx.reserve(tris->arr.size());for(const auto& index:tris->arr){require(index.t==V::Num&&std::isfinite(index.num)&&index.num>=0&&index.num<count&&std::floor(index.num)==index.num,"Rubber triangle has an invalid node index.");mesh.idx.push_back((u32)index.num);}
        require(frames&&frames->t==V::Arr&&frames->arr.size()>=2&&frames->arr.size()<=601&&frames->arr.size()*count<=262144,"Rubber frame response exceeds supported limits.");std::vector<RubberFrame059> decoded;AABB bounds;
        for(const auto& frame:frames->arr){auto time=frame.find("time_s"),positions=frame.find("positions_mm");require(time&&time->t==V::Num&&std::isfinite(time->num)&&time->num>=0&&time->num<=p.duration+1e-7&&(decoded.empty()||time->num>decoded.back().time),"Rubber frame times are invalid.");require(positions&&positions->t==V::Arr&&positions->arr.size()==count,"Rubber frame node count changed.");RubberFrame059 f;f.time=time->num;f.positions.reserve(count*3);
            for(const auto& point:positions->arr){require(point.t==V::Arr&&point.arr.size()==3,"Rubber position needs XYZ.");for(const auto& v:point.arr){require(v.t==V::Num&&std::isfinite(v.num)&&std::abs(v.num)<=1e9,"Rubber position is nonfinite or outside display bounds.");f.positions.push_back((float)v.num);}bounds.grow(p.world.transformPoint({point.arr[0].num,point.arr[1].num,point.arr[2].num}));}decoded.push_back(std::move(f));}
        require(std::abs(decoded.front().time)<1e-9&&std::abs(decoded.back().time-p.duration)<1e-7,"Rubber result omitted requested time endpoints.");p.frames=std::move(decoded);p.mesh=std::move(mesh);p.bounds=bounds;p.report=V::makeObj();for(const auto& item:data->obj)if(item.first!="frames"&&item.first!="triangles")p.report.set(item.first,item.second);p.stale=false;p.time=0;p.error.clear();p.message="Study complete. Cyan preview is the reduced surface network; dark stock remains exact CAD.";g_error=false;
    }catch(const std::exception& error){p.frames.clear();p.mesh.clear();releaseRubberGpu059();p.error=error.what();p.message="Rubber study rejected.";g_error=true;g_status=p.error;}
}
static std::function<u64(const arkjson::Value&,ExactJobDone046B)> g_rubberSubmitTest059;
static void runRubber059(){using V=arkjson::Value;auto& p=g_rubber059;if(p.running)return;std::string reason;if(!rubberDisplayAllowed059(reason))throw std::runtime_error(reason);
    if(selBody<0||(size_t)selBody>=g_bodyIds.size()||!displayInstance((size_t)selBody))throw std::runtime_error("Select one visible authored rubber body.");const size_t body=(size_t)selBody;const auto* feature=g_doc.find(g_bodyIds[body]);
    if(!feature||feature->type!=FeatureType::NativeSolid||featureOperation(*feature)!="rubber_body"||feature->nativeSpec.find("relief"))throw std::runtime_error("Select smooth authored rubber stock. Modified bodies and exact relief need a separate analysis recipe.");
    stopRubberDisplay059();releaseRubberGpu059();p.frames.clear();p.mesh.clear();p.report={};p.id=feature->id;p.node=g_instanceIds[body];p.copy=g_instanceCopies[body];p.world=g_instanceWorld[body];
    V args=V::makeObj(),study=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));study.set("id",V::makeNum((double)p.id));study.set("backend",V::makeStr(p.backend));study.set("duration_s",V::makeNum(p.duration));study.set("step_s",V::makeNum(p.step));study.set("iterations",V::makeNum(p.iterations));study.set("output_fps",V::makeNum(p.fps));study.set("around",V::makeNum(p.around));study.set("rows",V::makeNum(p.rows));study.set("ground",V::makeBool(p.ground));study.set("ground_z_mm",V::makeNum(p.groundZ));study.set("friction",V::makeNum(p.friction));study.set("damping_per_s",V::makeNum(p.damping));study.set("gravity_m_s2",inertialVector056(p.gravity));study.set("initial_velocity_m_s",inertialVector056(p.velocity));study.set("translation_mm",inertialVector056(p.translation));study.set("fixed_nodes",V::makeArr());args.set("rubber",study);
    study.set("include_frames",V::makeBool(true));args.set("rubber",study);
    const u64 revision=g_session.revision,epoch=g_objectDocumentEpoch,generation=++p.generation;p.revision=revision;p.epoch=epoch;p.sourceSha256=iterationSha256(documentToJson(g_doc,false,false));p.request=args;p.running=true;p.cancelling=p.stale=false;p.error.clear();p.cancelled=std::make_shared<std::atomic_bool>(false);p.message="Solving a private snapshot. CAD and viewport remain available.";
    auto done=[revision,epoch,generation](const V& result,u64 observed){if(generation!=g_rubber059.generation)return;if(observed!=revision){auto& q=g_rubber059;q.running=false;q.job=0;q.error="Background job returned a different revision.";return;}acceptRubber059(result,revision,epoch,generation);};
    try{auto cancelled=p.cancelled;p.job=g_rubberSubmitTest059?g_rubberSubmitTest059(args,done):submitExactJob046B("physics_inspect",args,done,"Rubber simulation",[cancelled]{return cancelled->load();});if(!p.job)throw std::runtime_error("Background exact jobs are unavailable; enable them to run the rubber study.");}catch(...){p.running=false;p.job=0;throw;}
}
static void fitRubber059(){const auto& p=g_rubber059;if(!rubberCurrent059()||!p.bounds.valid())return;AABB b=p.bounds;b.grow(g_sceneBounds);tTarget=b.center();const double aspect=(double)std::max(viewW,1)/std::max(viewH,1);tDist=std::max(2.,b.diagonal()/(2*std::tan(deg2rad(17.5))*std::min(1.,aspect)))*1.22;}
static void rubberEvidence059(){auto& p=g_rubber059;arkjson::Value evidence=arkjson::Value::makeObj();evidence.set("request",p.request);evidence.set("result",p.report);evidence.set("error",arkjson::Value::makeStr(p.error));std::string detail=readableJson(evidence),native;for(char c:detail){if(c=='\n')native+='\r';native+=c;}std::vector<EditorField> fields{{"Study evidence",native}};editFields("Rubber / Evidence","Local reduced surface network. Reported material inputs and limitations are part of this result.",fields,true,"Close",nullptr,true);}
static bool rubberAction059(int id){if(id<49300||id>49319)return false;auto& p=g_rubber059;
    if(id==49300){p.open=!p.open;if(p.open){stopInertialDisplay056();g_inertial056.open=false;stopBakedMotion();g_bakedMotions.open=false;collapseExplodedDisplay();if(g_explode.saved)applyVisualizePresets(false);g_explode.open=false;g_assistantDockOpen=false;advanceRubber059(0);}else stopRubberDisplay059();layout();return true;}
    if(id==49301)createRubber059();else if(id==49302)rubberStudy059();else if(id==49303)rubberEnvironment059();else if(id==49304)runRubber059();else if(id==49305)cancelRubber059();
    else if(id==49306){if(!p.playing&&rubberCurrent059()&&p.time>=p.frames.back().time)p.time=0;if(startRubberDisplay059())p.playing=!p.playing;}
    else if(id==49307){stopRubberDisplay059();p.message="Preview reset. Exact CAD was never changed; results remain available.";}
    else if(id==49308)rubberEvidence059();else if(id==49311)fitRubber059();
    else if(id==49309||id==49310||id==49312||id==49313){if(rubberCurrent059()){long frame=id==49312?0:id==49313?(long)p.frames.size()-1:(long)rubberFrameIndex059()+(id==49309?-1:1);frame=std::clamp(frame,0l,(long)p.frames.size()-1);p.time=p.frames[(size_t)frame].time;p.playing=false;startRubberDisplay059();}}return true;
}
static bool rubberKey059(WPARAM key){if(!g_rubber059.open||GetFocus()!=g_window)return false;if(key==VK_ESCAPE&&g_rubber059.active){rubberAction059(49307);return true;}if(key==VK_SPACE){rubberAction059(49306);return true;}if(key==VK_LEFT||key==VK_RIGHT){rubberAction059(key==VK_LEFT?49309:49310);return true;}if(key==VK_HOME||key==VK_END){rubberAction059(key==VK_HOME?49312:49313);return true;}return false;}
static void drawRubberPreview059(const GLfloat vpT[16],const Vec3& eye){auto& p=g_rubber059;if(!p.active||!rubberCurrent059()||!p.gpu.count)return;
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_TRUE);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glUseProgram(pSolid);GLfloat model[16];for(int k=0;k<16;++k)model[k]=(GLfloat)p.world.m[k];glUniformMatrix4fv(suModel,1,GL_FALSE,model);glUniformMatrix4fv(suVp,1,GL_FALSE,vpT);glUniform3f(suEye,(float)eye.x,(float)eye.y,(float)eye.z);
    glUniform3f(suColor,.16f,.62f,.76f);glUniform1f(suSel,0);glUniform1f(suOutline,0);glUniform1f(suHard,0);setEmission030(Appearance{});clearPatternUniforms044B();
    for(const char* field:{"stampMode","hasTexture","metallic","emissionOnly","selectionOverlay"})glUniform1f(glGetUniformLocation(pSolid,field),0);glUniform1f(glGetUniformLocation(pSolid,"opacity"),1);glUniform1f(glGetUniformLocation(pSolid,"roughness"),.8f);glUniform1f(glGetUniformLocation(pSolid,"satin"),1);glUniform1f(glGetUniformLocation(pSolid,"cameraOrtho"),g_cameraOrtho?1.f:0.f);
    glBindVertexArray(p.gpu.vao);glDrawElements(GL_TRIANGLES,p.gpu.count*3,GL_UNSIGNED_INT,nullptr);
}
template<class Rect,class Button>static void drawRubberPanel059(Rect rect,Button button,POINT pointer,std::string& hint){auto& p=g_rubber059;if(!p.open)return;const float x=(float)(viewX+viewW),y=(float)viewY,w=(float)rubberDockWidth059(),h=(float)(winH-viewY-32),left=x+14,inner=w-28,half=(inner-8)*.5f;
    rect(x,y,w,h,0,.05f,.068f,.084f,1,.20f,.27f,.32f,.9f,1);auto line=[&](const std::string& s,float top,float scale=.64f){textOut(fitText(s,scale,inner),left,top,scale,.70f,.82f,.88f);};
    textOut("RUBBER",left,y+15,.9f,.70f,.91f,.97f);button("Hide",x+w-70,y+7,58,49300);line("Exact stock + local network study",y+44);
    button("Create body",left,y+72,inner,49301);button("Study",left,y+112,half,49302);button("Environment",left+half+8,y+112,half,49303);
    const auto* selected=selBody>=0&&(size_t)selBody<g_bodyIds.size()?g_doc.find(g_bodyIds[(size_t)selBody]):nullptr;line("Selected: "+(selected?selected->name:std::string("none")),y+157);char setting[100];std::snprintf(setting,sizeof setting,"%s / %.3g s / %u x %u nodes",p.backend=="gpu"?"GPU":"CPU reference",p.duration,p.around,p.rows);line(setting,y+179);
    button(p.running?"Running...":"Run study",left,y+207,inner-82,49304,true);button(p.cancelling?"Stopping":"Cancel",left+inner-74,y+207,74,49305);
    std::string message=p.error.empty()?p.message:p.error;for(int row=0;row<3&&!message.empty();++row){auto part=fitText(message,.63f,inner);size_t count=part.size();if(count<message.size()){count=count>3?count-3:count;size_t space=message.rfind(' ',count);if(space!=std::string::npos&&space>0)count=space;part=message.substr(0,count);}line(part,y+252+row*19,.63f);message.erase(0,count);while(!message.empty()&&message.front()==' ')message.erase(0,1);}
    char time[96];std::snprintf(time,sizeof time,"%.3f / %.3f s / %s",p.time,rubberCurrent059()?p.frames.back().time:p.duration,p.active?"preview":"CAD only");line(time,y+319);
    button(p.playing?"Pause":"Play",left,y+345,half,49306);button("Reset preview",left+half+8,y+345,half,49307);
    button("|<",left,y+385,44,49312);button("<",left+50,y+385,44,49309);button(">",left+100,y+385,44,49310);button(">|",left+150,y+385,44,49313);
    button("Fit study",left,y+425,half,49311);button("Evidence",left+half+8,y+425,half,49308);
    if(h>=550){line("Local axes / reduced surface network",y+h-81,.60f);line("No self-contact, pressure or calibrated FEA",y+h-60,.58f);line("Cyan: analysis / dark stock: editable CAD",y+h-39,.60f);}
    if(pointer.x>=x&&pointer.x<x+w&&pointer.y>=y&&pointer.y<y+h)hint="Space play/pause; arrows step; Home/End seek; Esc hides the derived preview. Study axes are local to the definition.";g_zones.push_back({x,y,w,h,49319});
}
