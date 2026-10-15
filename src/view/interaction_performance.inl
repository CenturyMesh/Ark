// A.04: an opt-in benchmark through the native Studio authoring controls.
// Timings end at SwapBuffers API return, never at physical presentation.
static Document nativeProductPerformanceFixture(int count){
    using V=arkjson::Value;Document doc;doc.setParam("driver","0.5");
    for(int i=0;i<count;++i){
        Feature part;part.type=FeatureType::NativeSolid;part.name=i==0?"Enclosure mounting plate":"Mechanical component "+std::to_string(i+1);part.nativeSpec=V::makeObj();
        const bool round=i%3==1;part.nativeSpec.set("op",V::makeStr(round?"cylinder":"box"));
        if(round)part.p={{"radius",std::to_string(3+i%4)},{"height",std::to_string(8+i%5)}};
        else part.p={{"width",std::to_string(40+i%7)},{"depth",std::to_string(20+i%5)},{"height",std::to_string(4+i%3)}};
        auto id=doc.addFeature(part);
        if(i==0){Feature rounded;rounded.type=FeatureType::NativeSolid;rounded.name="Editable mounting-plate fillet";rounded.inputs={id};rounded.nativeSpec=V::makeObj();rounded.nativeSpec.set("op",V::makeStr("fillet"));V edges=V::makeArr();edges.arr.push_back(V::makeStr("edge.x.ymin.zmin"));rounded.nativeSpec.set("edges",edges);rounded.p={{"radius","driver"}};id=doc.addFeature(rounded);}
        AssemblyNode node;node.id=doc.nextAssemblyId_++;node.feature=id;node.name="Mechanical occurrence "+std::to_string(i+1);int columns=(int)std::ceil(std::sqrt((double)count));node.p={{"tx",std::to_string((i%columns)*55)},{"ty",std::to_string((i/columns)*35)},{"tz","0"}};doc.assembly.push_back(node);
    }return doc;
}
static int studioInteractionPerformance(HWND window,HDC dc,const std::string& shotPath,bool nativeProducts=false){
    using V=arkjson::Value;using Clock=std::chrono::steady_clock;namespace fs=std::filesystem;
    auto ms=[](Clock::time_point a,Clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();};
    auto utf8=[](const fs::path& path){auto s=path.u8string();return std::string(reinterpret_cast<const char*>(s.data()),s.size());};
    SYSTEMTIME utc{};GetSystemTime(&utc);char stamp[64]{};std::snprintf(stamp,sizeof stamp,"%04u%02u%02uT%02u%02u%02u%03uZ-%lu",utc.wYear,utc.wMonth,utc.wDay,utc.wHour,utc.wMinute,utc.wSecond,utc.wMilliseconds,GetCurrentProcessId());
    fs::path directory=fs::u8path(shotPath+".interaction-performance")/stamp;
    V report=V::makeObj(),rows=V::makeArr(),failures=V::makeArr();
    auto str=[&](const char* key,const std::string& value){report.set(key,V::makeStr(value));};
    str("schema","ark.studio-interaction-performance.v1");str("version",app_version());str("iteration_code",app_iteration_code());str("run_utc_id",stamp);
    str("workload","synthetic-analytic-box-grid-v1-seed-19020");
    str("interaction_scope","Native tree-row Windows mouse-message dispatch opens the real parameter editor; native Apply BM_CLICK starts timing. Normal expected_revision doc_apply, Studio progress worker/message pump, journal publication and display-scene rebuild finish the authoring stage. Final drawFrame submission and SwapBuffers API return are timed separately.");
    str("timing_limits","Hidden Studio window/OpenGL context, requested swap interval 0. Automated dialog input and its 200ms timer dwell are excluded from Apply latency and recorded separately. No physical input-device latency, OS-cold cache, GPU completion, compositor presentation, monitor scanout or native OCCT workload qualification is claimed. No percentile is a performance acceptance threshold.");
    str("model_scope","Independent legacy analytic box definitions, explicit driver expressions and one translated occurrence per definition; display meshes are generated from those analytic definitions.");
    if(nativeProducts){str("workload","synthetic-native-mechanical-assembly-v1-seed-29004");str("model_scope","Exact OCCT plate/cylindrical component definitions with rigid occurrences, one editable native fillet. Same source fixtures, controls and 21 measured edits per mode. Full rebuild and reuse modes run in the same executable; comparison isolates viewport representation reuse.");str("timing_limits","Hidden OpenGL context; automation dwell excluded. Includes native OCCT fillet evaluation, transaction publication, scene rebuild and SwapBuffers API return. Synthetic representative assemblies; no customer-project, OS-cold, GPU-completion or physical-presentation qualification. Failed attempts are retained.");}
    report.set("warmups_per_fixture",V::makeNum(3));report.set("measured_samples_per_fixture",V::makeNum(21));report.set("complete",V::makeBool(false));report.set("valid",V::makeBool(false));bool createdDirectory=false;
    V machine=V::makeObj();char hostname[256]{};DWORD hostnameSize=sizeof hostname;GetComputerNameA(hostname,&hostnameSize);machine.set("hostname_sha256",V::makeStr(iterationSha256(hostname)));
    if(const char* cpu=std::getenv("PROCESSOR_IDENTIFIER"))machine.set("processor_identifier",V::makeStr(cpu));
    machine.set("logical_processors",V::makeNum(std::thread::hardware_concurrency()));SYSTEM_INFO sys{};GetNativeSystemInfo(&sys);machine.set("processor_architecture",V::makeNum(sys.wProcessorArchitecture));
    MEMORYSTATUSEX mem{};mem.dwLength=sizeof mem;if(GlobalMemoryStatusEx(&mem))machine.set("physical_memory_bytes",V::makeNum((double)mem.ullTotalPhys));
    struct OsVersion {ULONG size,major,minor,build,platform;WCHAR service[128];};OsVersion os{};os.size=sizeof os;
    auto rtl=(LONG(WINAPI*)(OsVersion*))GetProcAddress(GetModuleHandleA("ntdll.dll"),"RtlGetVersion");if(rtl&&rtl(&os)==0){machine.set("windows_major",V::makeNum(os.major));machine.set("windows_minor",V::makeNum(os.minor));machine.set("windows_build",V::makeNum(os.build));}
    report.set("machine",machine);V runtime=V::makeObj();wchar_t executable[32768]{};DWORD executableSize=GetModuleFileNameW(nullptr,executable,32768);
    if(executableSize&&executableSize<32768){std::ifstream file(fs::path(executable),std::ios::binary);std::ostringstream bytes;bytes<<file.rdbuf();runtime.set("executable_sha256",V::makeStr(iterationSha256(bytes.str())));runtime.set("executable_file",V::makeStr(utf8(fs::path(executable).filename())));}
#if defined(__clang__) || defined(__GNUC__)
    runtime.set("compiler",V::makeStr(__VERSION__));
#elif defined(_MSC_FULL_VER)
    runtime.set("compiler",V::makeStr("MSVC "+std::to_string(_MSC_FULL_VER)));
#elif defined(_MSC_VER)
    runtime.set("compiler",V::makeStr("MSVC "+std::to_string(_MSC_VER)));
#else
    runtime.set("compiler",V::makeStr("unavailable"));
#endif
    for(auto field:std::initializer_list<std::pair<const char*,GLenum>>{{"gl_vendor",GL_VENDOR},{"gl_renderer",GL_RENDERER},{"gl_version",GL_VERSION}}){auto value=glGetString(field.second);runtime.set(field.first,V::makeStr(value?reinterpret_cast<const char*>(value):"unavailable"));}
    runtime.set("client_width",V::makeNum(winW));runtime.set("client_height",V::makeNum(winH));runtime.set("window_dpi",V::makeNum(editorDpi()));runtime.set("requested_swap_interval",V::makeNum(0));report.set("runtime",runtime);
    V rendering=V::makeObj();rendering.set("camera_orthographic",V::makeBool(g_cameraOrtho));rendering.set("wireframe",V::makeBool(wireframe));rendering.set("edges",V::makeBool(g_showEdges));rendering.set("light_background",V::makeBool(g_lightBackground));rendering.set("occlusion_mode",V::makeNum(g_occlusionMode));rendering.set("occlusion_strength",V::makeNum(g_occlusionStrength));rendering.set("occlusion_radius",V::makeNum(g_occlusionRadius));report.set("render_settings",rendering);
    const bool previousSwap=g_swap,previousBusyTest=g_busyTestMode,previousReuse=g_sceneReuseEnabled;const unsigned previousDelay=g_busyTestDelayMs;
    struct Reset {bool swap,busy,reuse;unsigned delay;~Reset(){g_swap=swap;g_busyTestMode=busy;g_sceneReuseEnabled=reuse;g_busyTestDelayMs=delay;g_editorSmokeProbe={};g_dialogSmoke=0;g_dialogOverrides.clear();g_dialogOverrideQueue.clear();}} reset{previousSwap,previousBusyTest,previousReuse,previousDelay};
    g_busyTestMode=true;g_busyTestDelayMs=0;g_busyTestShot.clear();g_swap=false;
    auto persist=[&](){report.set("results",rows);report.set("failures",failures);std::ofstream file(directory/"report.json",std::ios::binary|std::ios::trunc);file<<report.dump()<<'\n';file.flush();if(!file)throw std::runtime_error("Cannot retain interaction benchmark report");};
    auto fail=[&](const std::string& text){failures.arr.push_back(V::makeStr(text));};
    auto capture=[&](const fs::path& path){g_swap=false;drawFrame(dc);glFinish();std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(image.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);return glGetError()==GL_NO_ERROR&&writePNG(utf8(path).c_str(),image);};
    try{
        if(fs::exists(directory))throw std::runtime_error("Interaction benchmark refuses to replace a previous run");fs::create_directories(directory);createdDirectory=true;persist();
        for(bool reuse:nativeProducts?std::vector<bool>{false,true}:std::vector<bool>{true})for(int count:nativeProducts?std::vector<int>{12,48,144}:std::vector<int>{16,128,512}){
            g_sceneReuseEnabled=reuse;
            Document fixture;fixture.setParam("driver","10");int columns=(int)std::ceil(std::sqrt((double)count));
            for(int i=0;i<count;++i){Feature feature;feature.type=FeatureType::Box;feature.name="Synthetic box "+std::to_string(i+1);feature.p={{"dx","driver+"+std::to_string(i%7)},{"dy",std::to_string(10+i%5)},{"dz",std::to_string(10+i%3)}};auto id=fixture.addFeature(feature);AssemblyNode node;node.id=fixture.nextAssemblyId_++;node.feature=id;node.name="Grid occurrence "+std::to_string(i+1);node.p={{"tx",std::to_string((i%columns)*28)},{"ty",std::to_string((i/columns)*28)},{"tz","0"}};fixture.assembly.push_back(node);}
            if(nativeProducts)fixture=nativeProductPerformanceFixture(count);
            const std::string label=std::to_string(count)+(nativeProducts?(reuse?"-reuse":"-full"):"");
            std::string text=documentToJson(fixture);auto path=directory/("fixture-"+label+".ark");if(!saveDocument(fixture,utf8(path).c_str()))throw std::runtime_error("Cannot save synthetic fixture");
            V row=V::makeObj();row.set("feature_count",V::makeNum(fixture.features.size()));row.set("occurrence_count",V::makeNum(count));row.set("scene_reuse_enabled",V::makeBool(reuse));row.set("fixture_file",V::makeStr(path.filename().string()));row.set("fixture_authoring_sha256",V::makeStr(iterationSha256(text)));row.set("fixture_logical_bytes",V::makeNum(text.size()));row.set("samples",V::makeArr());rows.arr.push_back(row);auto& result=rows.arr.back();persist();
            V args=V::makeObj();args.set("path",V::makeStr(utf8(path)));args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_load",args);if(g_error)throw std::runtime_error("Synthetic fixture load: "+g_status);
            g_assistantDockOpen=false;g_toolsOpen=false;g_sidebarTab=0;g_parametersOpen=true;g_activeSketch.clear();g_explorerFeature=0;g_picked.clear();selBody=-1;layout();frameScene();target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_treeScroll=100000;g_treeSelectedZone=-1;drawFrame(dc);glFinish();
            V samples=V::makeArr();for(int sample=0;sample<24;++sample){
                V raw=V::makeObj();raw.set("sample_index",V::makeNum(sample<3?sample:sample-3));raw.set("warmup",V::makeBool(sample<3));raw.set("revision_before",V::makeStr(std::to_string(g_session.revision)));
                const u64 before=g_session.revision;const unsigned busyFramesBefore=g_busyFrameCount;std::string expression=nativeProducts?(g_doc.getParam("driver")==.5?"0.75":"0.5"):(g_doc.getParam("driver")==10?"11":"10");raw.set("driver_expression",V::makeStr(expression));bool applyDispatched=false;Clock::time_point applyAt{};
                try{
                    g_swap=false;g_treeScroll=100000;drawFrame(dc);glFinish();while(glGetError()!=GL_NO_ERROR){}
                    auto zone=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& z){return z.id==1000;});if(zone==g_zones.end())throw std::runtime_error("Driver parameter has no visible mouse hit zone");int x=(int)(zone->x+zone->w*.5f),y=(int)(zone->y+zone->h*.5f);
                    g_dialogSmoke=3;g_dialogOverrides={expression};g_editorSmokeProbe=[&](HWND dialog){auto button=GetDlgItem(dialog,1);if(!button)return;applyAt=Clock::now();applyDispatched=true;SendMessage(button,BM_CLICK,0,0);};
                    const auto dialogAt=Clock::now();SendMessage(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(window,WM_LBUTTONUP,0,MAKELPARAM(x,y));const auto sceneAt=Clock::now();g_editorSmokeProbe={};g_dialogSmoke=0;
                    if(!applyDispatched)throw std::runtime_error("Mouse dispatch did not reach native Apply");
                    raw.set("dialog_open_automation_wait_ms",V::makeNum(ms(dialogAt,applyAt)));raw.set("native_apply_to_scene_ready_ms",V::makeNum(ms(applyAt,sceneAt)));raw.set("reported_model_recompute_ms",V::makeNum(g_doc.recomputeMs));
                    if(g_error||g_session.revision!=before+1||g_doc.getParam("driver")!=std::stod(expression)||g_doc.recomputeEvaluated!=(nativeProducts?1u:(size_t)count)||g_bodyIds.size()!=(size_t)count)throw std::runtime_error("Native parameter edit failed revision, model-recompute or display-body verification: "+g_status);
                    raw.set("scene_rebuild_ms",V::makeNum(g_sceneRebuildMs));raw.set("scene_rebuilt_parts",V::makeNum(g_sceneRebuiltParts));raw.set("scene_reused_parts",V::makeNum(g_sceneReusedParts));
                    if(nativeProducts){for(const auto& body:g_doc.evalBodies){const auto* r=body.nativeData.find("report");if(!r||!r->find("brep_valid")||!r->find("brep_valid")->b)throw std::runtime_error("Native product fixture requires valid exact B-reps");}if(g_sceneRebuiltParts!=(reuse?1u:(size_t)count)||g_sceneReusedParts!=(reuse?(size_t)count-1:0u))throw std::runtime_error("Scene reuse/invalidation count differs from edited source closure");}
                    const auto drawAt=Clock::now();drawFrame(dc);const auto drawnAt=Clock::now();const BOOL swapped=SwapBuffers(dc);const auto swapAt=Clock::now();
                    raw.set("draw_frame_submission_ms",V::makeNum(ms(drawAt,drawnAt)));raw.set("swap_buffers_return_ms",V::makeNum(ms(drawnAt,swapAt)));raw.set("native_apply_to_swap_return_ms",V::makeNum(ms(applyAt,swapAt)));raw.set("evaluated_features",V::makeNum(g_doc.recomputeEvaluated));raw.set("display_bodies",V::makeNum(g_bodyIds.size()));
                    if(!swapped)throw std::runtime_error("SwapBuffers returned failure "+std::to_string(GetLastError()));if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("OpenGL error after interaction render");raw.set("status",V::makeStr("ok"));
                }catch(const std::exception& error){g_editorSmokeProbe={};g_dialogSmoke=0;g_dialogOverrides.clear();raw.set("status",V::makeStr("failed"));raw.set("error",V::makeStr(error.what()));fail(std::to_string(count)+" features / sample "+std::to_string(sample)+": "+error.what());}
                raw.set("revision_after",V::makeStr(std::to_string(g_session.revision)));raw.set("studio_progress_frames",V::makeNum(g_busyFrameCount-busyFramesBefore));samples.arr.push_back(raw);result.set("samples",samples);persist();
            }
            V summaries=V::makeObj();size_t successes=0;for(const auto& raw:result.find("samples")->arr)if(!raw.find("warmup")->b&&raw.find("status")->str=="ok")++successes;
            result.set("successful_measured_samples",V::makeNum(successes));
            for(const char* stage:{"native_apply_to_scene_ready_ms","reported_model_recompute_ms","scene_rebuild_ms","draw_frame_submission_ms","swap_buffers_return_ms","native_apply_to_swap_return_ms"}){std::vector<double> values;for(const auto& raw:result.find("samples")->arr)if(!raw.find("warmup")->b&&raw.find("status")->str=="ok"&&raw.find(stage))values.push_back(raw.find(stage)->num);if(values.empty())continue;std::sort(values.begin(),values.end());V summary=V::makeObj();summary.set("median_ms",V::makeNum(values[values.size()/2]));summary.set("p95_nearest_rank_ms",V::makeNum(values[(size_t)std::ceil(.95*values.size())-1]));summary.set("max_ms",V::makeNum(values.back()));summaries.set(stage,summary);}result.set("summary_successful_measured_samples",summaries);
            auto image=directory/("fixture-"+label+".png");g_status="A.04 interaction benchmark / "+std::to_string(count)+" parts / "+std::to_string(successes)+" measured commits";if(!capture(image))fail("Could not capture "+image.filename().string());result.set("capture_file",V::makeStr(image.filename().string()));persist();
        }
        report.set("complete",V::makeBool(true));report.set("valid",V::makeBool(failures.arr.empty()));persist();std::fprintf(stderr,"Studio interaction benchmark report: %s\n",utf8(directory/"report.json").c_str());return failures.arr.empty()?0:940;
    }catch(const std::exception& error){fail(error.what());report.set("valid",V::makeBool(false));if(createdDirectory)try{persist();}catch(...){}std::fprintf(stderr,"Studio interaction benchmark failed: %s / %s\n",error.what(),utf8(directory/"report.json").c_str());return 941;}
}
