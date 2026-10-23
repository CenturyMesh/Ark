// Isolated native Studio verification. Only synthetic author-owned solids are
// generated; no local drawing or persisted human settings are read or changed.
static int studioRenderSelfTest(HDC dc,const std::string& output){
 using V=arkjson::Value;auto fail=[&](int code,const char* message){fprintf(stderr,"ARK studio renderer check %d: %s / %s\n",code,message,g_trace.reason.c_str());return code;};
 auto parse=[](const char* json){V value;if(!arkjson::parse(json,value))throw std::runtime_error("Invalid renderer test fixture");return value;};
 auto call=[&](const char* tool,V args){return runTool(tool,args);};
 V args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));call("doc_new",args);if(g_error)return fail(780,"Synthetic document could not be created");
 args=V::makeObj();args.set("expected_revision",V::makeNum((double)g_session.revision));args.set("commands",parse(R"([
 {"op":"feature.add","type":"native_solid","name":"Studio base","native":{"op":"box"},"p":{"width":140,"depth":100,"height":12},"appearance":{"base_color":[0.46,0.51,0.58],"metallic":0.45,"roughness":0.38}},
 {"op":"feature.add","type":"native_solid","name":"Polished sphere","native":{"op":"sphere"},"p":{"radius":24},"appearance":{"base_color":[0.82,0.85,0.90],"metallic":0.94,"roughness":0.16}},
 {"op":"feature.add","type":"native_solid","name":"Satin cylinder","native":{"op":"cylinder"},"p":{"radius":17,"height":50},"appearance":{"base_color":[0.18,0.48,0.66],"metallic":0.56,"roughness":0.32}},
 {"op":"feature.add","type":"native_solid","name":"Matte block","native":{"op":"box"},"p":{"width":26,"depth":27,"height":32},"appearance":{"base_color":[0.83,0.39,0.13],"metallic":0.02,"roughness":0.76}},
 {"op":"assembly.add","name":"Base","feature":"Studio base","p":{"tx":-70,"ty":0,"tz":50,"rx":-90}},
 {"op":"assembly.add","name":"Sphere","feature":"Polished sphere","p":{"tx":-31,"ty":36,"tz":-2}},
 {"op":"assembly.add","name":"Cylinder","feature":"Satin cylinder","p":{"tx":28,"ty":12,"tz":-18,"rx":-90}},
 {"op":"assembly.add","name":"Block","feature":"Matte block","p":{"tx":10,"ty":12,"tz":29,"rx":-90}}
 ])"));call("doc_apply",args);if(g_error||g_doc.evalBodies.size()!=4)return fail(781,"Exact synthetic primitives rejected");
 g_path.clear();selBody=-1;hoverBody=-1;g_grid=true;g_showSketches=false;g_showDatumPlanes=false;g_lightBackground=true;g_showEdges=false;g_pathTracing=false;wireframe=false;g_cameraOrtho=false;g_occlusionMode=2;g_foldPlaying=false;g_suspSpinPlaying=false;g_motionPlaying=false;
 // Keep the measured crop inside the model viewport even when the current
 // assistant dock defaults to an expanded conversation area.
 g_assistantDockOpen=false;g_showViewCube=false;layout();
 tYaw=yaw=.67;tPitch=pitch=.39;cameraRoll=0;frameScene();target=tTarget;dist=tDist;g_swap=false;
 const u64 baselineRevision=g_session.revision;std::string breps;for(auto& body:g_doc.evalBodies)breps+=body.nativeData.dump();
 auto capture=[&](const char* suffix){
  drawFrame(dc);glFinish();if(glGetError()!=GL_NO_ERROR)return std::uint64_t{0};
  std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
  Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(image.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);
  if(!writePNG((output+suffix).c_str(),image))return std::uint64_t{0};std::uint64_t hash=1469598103934665603ull;
  for(int y=viewY+170;y<viewY+viewH-70;++y)for(int x=viewX+160;x<viewX+viewW-160;++x)for(int c=0;c<3;++c){hash^=image.px[((size_t)y*winW+x)*3+c];hash*=1099511628211ull;}return hash;
 };
 const auto raster=capture(".studio-raster.png");g_showEdges=true;const auto edges=capture(".studio-edges.png");g_lightBackground=false;const auto dark=capture(".studio-dark.png");
 if(!raster||!edges||!dark||raster==edges||edges==dark)return fail(782,"Raster, black edges and both backgrounds must differ");
 g_lightBackground=true;g_cameraOrtho=true;const auto ortho=capture(".studio-ortho.png");if(!ortho||ortho==edges)return fail(783,"Orthographic material/edge display failed");g_cameraOrtho=false;

 // A known occluder isolates hidden-line depth correctness from source topology.
 // No CAD selection/triangle provenance is inferred from this GL-only fixture.
 {
  Mesh plane;plane.pos={-1,-1,0,1,-1,0,1,1,0,-1,1,0};plane.idx={0,1,2,0,2,3};plane.computeNormals();GPUMesh gpu;uploadMesh(gpu,plane);
  GLuint vao=0,buffer=0;glGenVertexArrays(1,&vao);glGenBuffers(1,&buffer);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);
  glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,32,nullptr);glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,32,(void*)12);glEnableVertexAttribArray(2);glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,32,(void*)24);
  static const GLfloat identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  auto renderLine=[&](float depth){
   glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,128,128);glDisable(GL_BLEND);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LEQUAL);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
   glUseProgram(pSolid);glUniformMatrix4fv(suVp,1,GL_FALSE,identity);glUniformMatrix4fv(suModel,1,GL_FALSE,identity);glUniform3f(suEye,0,0,10);glUniform3f(suColor,.82f,.82f,.82f);glUniform1f(suSel,0);glUniform1f(suOutline,0);glUniform1f(suHard,1);glUniform1f(glGetUniformLocation(pSolid,"hasTexture"),0);glUniform1f(glGetUniformLocation(pSolid,"patternKind"),0.f);glUniform1f(glGetUniformLocation(pSolid,"metallic"),0);glUniform1f(glGetUniformLocation(pSolid,"roughness"),.7f);glUniform1f(glGetUniformLocation(pSolid,"opacity"),1);studioBasis(pSolid,CameraFrame{});glBindVertexArray(gpu.vao);glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr);
   std::vector<float> line;const float corners[6][2]={{0,-1},{1,-1},{1,1},{0,-1},{1,1},{0,1}};for(auto& c:corners)line.insert(line.end(),{-.8f,-.25f,depth,.8f,.25f,depth,c[0],c[1]});
   glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(line.size()*sizeof(float)),line.data(),GL_DYNAMIC_DRAW);glUseProgram(pFeatureEdge);glUniformMatrix4fv(glGetUniformLocation(pFeatureEdge,"vp"),1,GL_FALSE,identity);glUniformMatrix4fv(glGetUniformLocation(pFeatureEdge,"model"),1,GL_FALSE,identity);glUniform2f(glGetUniformLocation(pFeatureEdge,"viewport"),128,128);glUniform1f(glGetUniformLocation(pFeatureEdge,"width"),1.22f);glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDepthMask(GL_FALSE);glDrawArrays(GL_TRIANGLES,0,6);glDepthMask(GL_TRUE);glFinish();
   std::vector<u8> pixels(128*128*3);glReadBuffer(GL_BACK);glReadPixels(0,0,128,128,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());int black=0,partial=0;
   for(int y=35;y<93;++y)for(int x=20;x<108;++x){size_t at=((size_t)y*128+x)*3;int luminance=(pixels[at]+pixels[at+1]+pixels[at+2])/3;if(luminance<80)++black;else if(luminance<150)++partial;}return std::pair<int,int>{black,partial};
  };
  auto hidden=renderLine(.2f),visible=renderLine(-.2f);glDeleteBuffers(1,&buffer);glDeleteVertexArrays(1,&vao);releaseMesh(gpu);
  if(hidden.first!=0||hidden.second!=0||visible.first<20||visible.second<20)return fail(784,"Hidden edges leaked, visible edges absent, or AA coverage absent");
 }

 // Exercise the actual System options form; its test override prevents writes
 // to a human's saved settings while using the same checkbox dispatch.
 drawFrame(dc);glFinish();g_dialogShot=output+".system-options.png";g_dialogOverrides={"1","0","1","1","0","2","0.72","1","1"};g_dialogSmoke=3;graphicsDialog();g_dialogSmoke=0;g_dialogShot.clear();if(!g_pathTracing||!std::filesystem::is_regular_file(output+".system-options.png"))return fail(785,"System options path-tracing checkbox did not activate or capture");
 auto tracedFirst=capture(".path-1spp.png");if(!tracedFirst||!g_trace.active||g_trace.samples!=1)return fail(786,"Actual GPU path tracer failed to render one sample");
 std::vector<double> timing;for(int sample=1;sample<32;++sample){auto start=std::chrono::steady_clock::now();drawFrame(dc);glFinish();timing.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
 if(g_trace.samples!=32||glGetError()!=GL_NO_ERROR)return fail(787,"Accumulation failed to advance consistently");
 auto traced=capture(".path-33spp.png");if(!traced||traced==tracedFirst||traced==edges)return fail(788,"Progressive ray samples do not change the image");
 int accumulated=g_trace.samples;wireframe=true;capture(".path-wireframe-fallback.png");if(g_trace.active||g_trace.reason.find("Wireframe")==std::string::npos)return fail(797,"Wireframe did not select raster display");wireframe=false;capture(".path-wireframe-restored.png");if(!g_trace.active)return fail(798,"Returning from wireframe left a sticky path fallback");
 tYaw=yaw+=.12;capture(".path-camera-reset.png");if(!g_trace.active||g_trace.samples!=1)return fail(789,"Camera edit did not reset accumulation");
 g_cameraOrtho=true;capture(".path-ortho-reset.png");if(!g_trace.active||g_trace.samples!=1)return fail(790,"Projection edit did not reset accumulation");g_cameraOrtho=false;
 int savedWidth=winW,savedHeight=winH,savedTraceWidth=g_trace.width;RECT frame{0,0,winW+96,winH};AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(g_window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);capture(".path-resize-reset.png");if(!g_trace.active||g_trace.samples!=1||g_trace.width==savedTraceWidth)return fail(799,"Window resize did not reset and resize accumulation");frame={0,0,savedWidth,savedHeight};AdjustWindowRect(&frame,WS_OVERLAPPEDWINDOW,FALSE);SetWindowPos(g_window,nullptr,0,0,frame.right-frame.left,frame.bottom-frame.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
 g_hiddenBodies[0]=true;capture(".path-visibility-reset.png");if(!g_trace.active||g_trace.samples!=1)return fail(791,"Visibility edit did not reset accumulation");g_hiddenBodies[0]=false;
 auto savedTransform=g_instanceWorld[1];g_instanceWorld[1]=Mat4::translation({8,0,0})*savedTransform;capture(".path-transform-reset.png");if(!g_trace.active||g_trace.samples!=1)return fail(792,"Instance transform did not reset accumulation");g_instanceWorld[1]=savedTransform;
 const auto savedAppearance=g_doc.features[1].appearance;g_doc.features[1].appearance=parse(R"({"opacity":0.4})");g_trace.sceneKey=0;capture(".path-transparency-fallback.png");if(g_trace.active||g_trace.reason.find("Transparent")==std::string::npos)return fail(793,"Unsupported transparency was silently traced");g_doc.features[1].appearance=savedAppearance;g_trace.sceneKey=0;
 g_dialogOverrides={"1","0","1","1","0","2","0.72","1","0"};g_dialogSmoke=3;graphicsDialog();g_dialogSmoke=0;capture(".path-disabled.png");if(g_pathTracing||g_trace.active)return fail(794,"Runtime path-tracing checkbox did not disable");
 std::string afterBreps;for(auto& body:g_doc.evalBodies)afterBreps+=body.nativeData.dump();if(g_session.revision!=baselineRevision||afterBreps!=breps)return fail(795,"Display actions changed the authoritative native document");
 std::sort(timing.begin(),timing.end());V receipt=V::makeObj();receipt.set("backend",V::makeStr("OpenGL 3.3 GPU fragment path tracing, software BVH, no RT-core acceleration"));receipt.set("gpu",V::makeStr((const char*)glGetString(GL_RENDERER)));receipt.set("driver_gl",V::makeStr((const char*)glGetString(GL_VERSION)));receipt.set("synthetic_exact_bodies",V::makeNum(4));receipt.set("triangles",V::makeNum(g_trace.triangleCount));receipt.set("bvh_nodes",V::makeNum(g_trace.nodeCount));receipt.set("trace_width",V::makeNum(g_trace.width));receipt.set("trace_height",V::makeNum(g_trace.height));receipt.set("max_bounces",V::makeNum(4));receipt.set("accumulated_samples_before_reset",V::makeNum(accumulated));receipt.set("completed_frame_p50_ms",V::makeNum(timing[timing.size()/2]));receipt.set("completed_frame_p95_ms",V::makeNum(timing[(timing.size()*95)/100]));receipt.set("bvh_build_ms",V::makeNum(g_trace.buildMs));receipt.set("gpu_timing_method",V::makeStr("steady_clock full drawFrame followed by glFinish, CPU plus completed GPU, 31 stationary frames"));receipt.set("native_document_unchanged",V::makeBool(true));receipt.set("hidden_edge_occlusion_and_aa",V::makeBool(true));receipt.set("camera_projection_visibility_transform_resets",V::makeBool(true));receipt.set("runtime_system_options_toggle",V::makeBool(true));receipt.set("unsupported_transparency_fallback",V::makeBool(true));
 std::ofstream file(output+".studio-render.json");file<<receipt.dump();if(!file)return fail(796,"Render verification receipt could not be written");fprintf(stderr,"ARK studio renderer passed: %d triangles, %dx%d, %.3f ms p50 / %.3f ms p95 completed frame\n",g_trace.triangleCount,g_trace.width,g_trace.height,timing[timing.size()/2],timing[(timing.size()*95)/100]);return 0;
}
