static arkjson::Value mediaPlaybackTool(const arkjson::Value& args){
    using V=arkjson::Value;auto need=[](bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason);};
    auto text=[&](const V& v,const char* key,const std::string& fallback={}){const auto* p=v.find(key);if(!p)return fallback;need(p->t==V::Str&&!p->str.empty(),std::string(key)+" must be nonempty text");return p->str;};
    const std::set<std::string> allowed{"action","mode","motion","assets","leds","time_s","expected_revision","actor"};for(const auto& item:args.obj)need(allowed.count(item.first)!=0,"Unknown playback binding field: "+item.first);
    auto mode=text(args,"mode","inspect");
    if(mode!="inspect"){
        const auto* expected=args.find("expected_revision");need(expected&&expected->t==V::Num&&expected->num==(double)g_session.revision,"Revision conflict; inspect doc_info before binding media playback");
        if(mode=="clear")g_mediaPlaybackBinding={};
        else if(mode=="play"||mode=="pause"||mode=="stop"||mode=="seek"){
            need(!g_mediaPlaybackBinding.motion.empty(),"Bind media to a named baked motion first");
            if(mode=="stop"){need(g_bakedMotions.selected==g_mediaPlaybackBinding.motion,"The selected motion is not the bound motion");stopBakedMotion();}
            else{
                need(mediaBindingCurrent(),"MEDIA_BINDING_STALE: rebind after the document revision changed");
                if(mode=="pause"){need(g_bakedMotions.selected==g_mediaPlaybackBinding.motion,"The selected motion is not the bound motion");g_bakedMotions.playing=false;motion_cables_view_054b::state.autoPlay=false;}
                else{
                    double seek=0;if(mode=="seek"){const auto* value=args.find("time_s");need(value&&value->t==V::Num&&std::isfinite(value->num)&&value->num>=0&&value->num<=g_mediaPlaybackBinding.duration,"Seek time_s must be inside the bound motion duration");seek=value->num;}
                    if(g_bakedMotions.selected!=g_mediaPlaybackBinding.motion){stopBakedMotion();g_bakedMotions.selected=g_mediaPlaybackBinding.motion;g_bakedMotions.revision=~u64(0);}
                    g_bakedMotions.open=true;layout();std::string reason;
                    if(bakedMotionPlaybackAvailable(reason)){if(mode=="seek")g_bakedMotions.time=seek;startBakedMotionDisplay();g_bakedMotions.playing=mode=="play";}
                    else{need(motion_cables_view_054b::state.worker.valid()&&motion_cables_view_054b::current(),reason);motion_cables_view_054b::state.autoPlay=mode=="play";}
                }
            }
        }
        else{
            need(mode=="bind","Playback mode must be inspect, bind, clear, play, pause, stop or seek");MediaPlaybackBinding candidate;candidate.motion=text(args,"motion");candidate.revision=g_session.revision;candidate.epoch=g_documentEpoch;
            V query=V::makeObj();query.set("name",V::makeStr(candidate.motion));V info;need(callMotionTool(g_session,"motion_info",query,info),"Motion inspection unavailable");
            const auto* status=info.find("status");const auto* stale=status?status->find("stale"):nullptr,*baked=status?status->find("baked"):nullptr;need(baked&&baked->t==V::Bool&&baked->b&&stale&&stale->t==V::Bool&&!stale->b,"MEDIA_BINDING_REQUIRES_CURRENT_BAKE: bake the named motion before binding");
            const auto* motion=findMotion(g_doc,candidate.motion);need(motion&&motion->find("bake"),"Named baked motion was not found");const auto& bake=*motion->find("bake");candidate.fps=bake.find("fps")->num;candidate.duration=bake.find("duration_s")->num;size_t frameCount=(size_t)bake.find("frame_count")->num;need(frameCount>0&&frameCount<=7201,"Unsupported baked frame count");
            const auto* assets=args.find("assets");need(assets&&assets->t==V::Arr&&!assets->arr.empty()&&assets->arr.size()<=16,"Binding requires 1..16 GIF assets");
            for(const auto& name:assets->arr){need(name.t==V::Str&&!name.str.empty(),"Binding assets must be names");const auto* asset=findImageAsset(g_doc,name.str);need(asset&&asset->mimeType=="image/gif"&&asset->frames>1,"MEDIA_SYNC_REQUIRES_GIF: only frame-indexed GIF synchronization is verified; MP4 remains independent playback");candidate.assets.emplace(asset->name,asset->sha256);}
            std::map<std::string,double> starts;if(const auto* channels=bake.find("channels"))for(const auto& c:channels->arr)if(const auto* start=c.find("graph_start"))starts[c.find("name")->str]=start->num;
            CompiledMotion graph;std::string error;need(compileMotionGraph(*motion->find("graph"),g_doc.buildEnv(),starts,graph,error),"Media motion graph invalid: "+error);
            if(const auto* leds=args.find("leds")){need(leds->t==V::Arr&&leds->arr.size()<=16,"leds must contain at most 16 bindings");for(const auto& led:leds->arr){need(led.t==V::Obj,"LED binding must be an object");for(const auto& entry:led.obj)need(entry.first=="feature"||entry.first=="intensity","Unknown LED binding field");const auto* id=led.find("feature");need(id&&id->t==V::Num&&id->num>=1&&std::floor(id->num)==id->num,"LED binding requires a numeric feature id");u64 feature=(u64)id->num;const auto* definition=g_doc.find(feature);need(definition!=nullptr,"LED feature not found");need(resolvedAppearance(g_doc,*definition).emissionIntensity>0,"LED feature must have an authored emissive appearance first (light_up)");need(!candidate.ledFrames.count(feature),"Duplicate LED feature binding");std::string expression=text(led,"intensity");need(expression.size()<=512,"LED intensity expression exceeds 512 bytes");auto& values=candidate.ledFrames[feature];ExprEnv env=g_doc.buildEnv();env.vars["duration"]=candidate.duration;
                    for(size_t frame=0;frame<frameCount;++frame){double t=std::min(candidate.duration,(double)frame/candidate.fps);auto channels=evaluateMotion(graph,t);for(size_t c=0;c<channels.size();++c)env.vars[graph.channels[c].name]=channels[c];env.vars["t"]=t;env.vars["u"]=t/candidate.duration;env.vars["frame"]=(double)frame;bool ok=false;double value=evalExpr(expression,env,&ok);need(ok&&std::isfinite(value)&&value>=0&&value<=1,"LED intensity expression must evaluate to 0..1 at every baked frame");values.push_back(value);}
                }}
            g_mediaPlaybackBinding=std::move(candidate); // all validation precedes publication
        }
    }
    const auto& preparation=motion_cables_view_054b::state;bool pending=mediaBindingCurrent()&&g_bakedMotions.selected==g_mediaPlaybackBinding.motion&&preparation.worker.valid()&&motion_cables_view_054b::current();
    V r=V::makeObj();r.set("scope",V::makeStr("live_studio_session"));r.set("persisted",V::makeBool(false));r.set("display_only",V::makeBool(true));r.set("hardware_triggered",V::makeBool(false));r.set("document_unchanged",V::makeBool(true));r.set("revision",V::makeNum((double)g_session.revision));r.set("motion",V::makeStr(g_mediaPlaybackBinding.motion));r.set("bound",V::makeBool(!g_mediaPlaybackBinding.motion.empty()));r.set("current",V::makeBool(mediaBindingCurrent()));r.set("time_s",V::makeNum(mediaBindingTime()));r.set("pending",V::makeBool(pending));r.set("playing",V::makeBool(mediaBindingCurrent()&&g_bakedMotions.selected==g_mediaPlaybackBinding.motion&&g_bakedMotions.playing));if(pending){r.set("prepared_frames",V::makeNum((double)preparation.done->load()));r.set("total_frames",V::makeNum((double)preparation.total->load()));}
    r.set("status",V::makeStr(g_mediaPlaybackBinding.motion.empty()?"independent":!mediaBindingCurrent()?"stale / rebind at current revision":pending?"preparing native flexible cable frames":g_bakedMotions.active&&g_bakedMotions.selected==g_mediaPlaybackBinding.motion?"following baked frame clock":"waiting for named baked motion / frame zero"));V assets=V::makeArr();for(const auto& a:g_mediaPlaybackBinding.assets)assets.arr.push_back(V::makeStr(a.first));r.set("assets",assets);r.set("led_bindings",V::makeNum((double)g_mediaPlaybackBinding.ledFrames.size()));return r;
}
