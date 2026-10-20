// Included in the Studio test driver after a real GL context. Synthetic GIF,
// H.264 clip and boxes only; no user documents or artwork.
if(media044bUiTest){
    using V=arkjson::Value;int stage=4400;const std::string clipName="ark-media-044b-selftest.mp4";
    auto verify=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);};
    auto json=[](const std::string& text){V out;if(!arkjson::parse(text,out))throw std::runtime_error("Invalid synthetic media UI source");return out;};
    auto draw=[&]{target=tTarget;dist=tDist;yaw=tYaw;pitch=tPitch;g_swap=false;drawFrame(hdc);glFinish();};
    auto capture=[&](const std::string& suffix){draw();Image shot;shot.w=winW;shot.h=winH;shot.px.resize((size_t)winW*winH*3);std::vector<u8> pixels(shot.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());for(int y=0;y<winH;++y)std::copy_n(pixels.begin()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3,shot.px.begin()+(size_t)y*winW*3);if(!suffix.empty())verify(writePNG((shotPath+suffix).c_str(),shot),"Could not capture the media viewport");return shot;};
    auto fail=[&](const std::string& message){g_mediaClockOverride044B=-1;std::error_code ignored;std::filesystem::remove(clipName,ignored);try{capture(".media-044b-failure.png");}catch(...){}fprintf(stderr,"Media 044B UI %d: %s / %s\n",stage,message.c_str(),g_status.c_str());return stage;};
    auto apply=[&](V commands){V args=V::makeObj();args.set("commands",std::move(commands));args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",args);verify(!g_error,g_status);};
    auto pixel=[&](const Image& shot,Vec3 p){float x=0,y=0;verify(projectScreen(p,x,y),"Media evidence point is not visible");const int sx=(int)std::lround(x),sy=(int)std::lround(y);verify(sx>viewX+4&&sx<viewX+viewW-4&&sy>viewY+4&&sy<viewY+viewH-4,"Media evidence point is outside the viewport");Vec3 rgb{};for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){const auto offset=((size_t)(sy+dy)*shot.w+sx+dx)*3;rgb+=Vec3{(double)shot.px[offset],(double)shot.px[offset+1],(double)shot.px[offset+2]};}return rgb/25.;};
    auto dominant=[](Vec3 c){return c.x>c.y+25&&c.x>c.z+25?0:c.y>c.x+25&&c.y>c.z+25?1:c.z>c.x+25&&c.z>c.y+25?2:-1;};
    auto spread=[&](const Image& shot,Vec3 a,Vec3 b){float ax=0,ay=0,bx=0,by=0;verify(projectScreen(a,ax,ay)&&projectScreen(b,bx,by),"Pattern evidence region is not visible");double sum=0,sum2=0,n=0;for(int y=(int)std::min(ay,by);y<(int)std::max(ay,by);++y)for(int x=(int)std::min(ax,bx);x<(int)std::max(ax,bx);++x){const double v=shot.px[((size_t)y*shot.w+x)*3+1];sum+=v;sum2+=v*v;++n;}verify(n>400,"Pattern evidence region is too small");return sum2/n-(sum/n)*(sum/n);};
    auto base64=[](const std::string& bytes){static const char* a="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string out;for(size_t i=0;i<bytes.size();i+=3){unsigned n=(unsigned)(unsigned char)bytes[i]<<16;if(i+1<bytes.size())n|=(unsigned)(unsigned char)bytes[i+1]<<8;if(i+2<bytes.size())n|=(unsigned char)bytes[i+2];out+=a[(n>>18)&63];out+=a[(n>>12)&63];out+=i+1<bytes.size()?a[(n>>6)&63]:'=';out+=i+2<bytes.size()?a[n&63]:'=';}return out;};
    try{
        cancelDirectAuthoring();cancelSketchEditing();cancelSketchDrawing();g_picked.clear();g_assistantDockOpen=false;g_showViewCube=false;g_showEdges=false;g_grid=false;g_occlusionMode=0;g_pathTracing=false;g_showSketches=false;g_showDatumPlanes=false;wireframe=false;g_toolsOpen=false;g_error=false;
        V fresh=V::makeObj();fresh.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_new",fresh);verify(!g_error,"Synthetic media document could not start");layout();++stage;
        std::vector<std::vector<unsigned char>> frames;for(unsigned char index:{0,1,2})frames.emplace_back((size_t)32*24,index);const auto gif=encodeGif044B(32,24,{{{235,25,25}},{{25,215,40}},{{30,40,240}}},frames,{20,20,20});
        V asset=V::makeObj();asset.set("name",V::makeStr("Screen loop"));asset.set("mime_type",V::makeStr("image/gif"));asset.set("data_base64",V::makeStr(base64(gif)));V setAsset=V::makeObj();setAsset.set("op",V::makeStr("image_asset.set"));setAsset.set("asset",asset);
        V batch=json(R"([{"op":"feature.add","type":"native_solid","name":"Screen","native":{"op":"box"},"p":{"width":120,"depth":80,"height":8}},{"op":"feature.add","type":"native_solid","name":"Panel blank","native":{"op":"box"},"p":{"width":120,"depth":80,"height":8}},{"op":"feature.add","type":"native_solid","name":"Panel","inputs":["Panel blank"],"native":{"op":"transform"},"p":{"tx":0,"ty":-110,"tz":0}}])");batch.arr.push_back(setAsset);apply(batch);++stage;
        const Body* screen=nullptr;for(const auto& body:g_doc.evalBodies)if(const auto* f=g_doc.find(body.id);f&&f->name=="Screen")screen=&body;verify(screen&&screen->nativeData.find("topology"),"Screen body has no exact topology");const std::string topology=screen->nativeData.find("topology")->find("brep_sha256")->str;
        V stamp=json(R"({"op":"stamp.set","name":"Display","asset":"Screen loop","feature":"Screen","faces":["z_max"],"origin":[60,40,8],"u_axis":[1,0,0],"v_axis":[0,1,0],"width":100,"height":60})");stamp.set("source_topology",V::makeStr(topology));V stampBatch=V::makeArr();stampBatch.arr.push_back(stamp);apply(stampBatch);++stage;
        tTarget={60,-40,4};tDist=430;tYaw=0;tPitch=-.12;cameraRoll=0;g_picked.clear();selBody=-1;hoverBody=-1;g_cameraOrtho=false;
        g_mediaClockOverride044B=0;const auto first=capture(".media-gif-t0.png");const Vec3 centre{60,40,8};const auto red=pixel(first,centre);
        g_mediaClockOverride044B=.3;const auto second=capture(".media-gif-t030.png");const auto green=pixel(second,centre);
        g_mediaClockOverride044B=.5;const auto blue=pixel(capture(".media-gif-t050.png"),centre);
        verify(dominant(red)==0&&dominant(green)==1&&dominant(blue)==2,"Animated GIF stamp did not show the red/green/blue frames at 0/0.3/0.5 s");++stage;
        // Procedural patterns on the second body: each one is visible without an image.
        const Vec3 cornerA{20,-100,8},cornerB{100,-40,8};selBody=-1;hoverBody=-1;const auto plain=spread(capture(".media-pattern-plain.png"),cornerA,cornerB);std::ostringstream patternLog;
        for(const char* name:{"twill","weave","carbon","leather","brushed","knurl","perforated"}){V edit=json(std::string(R"([{"op":"feature.edit","id":"Panel","appearance":{"base_color":[0.42,0.45,0.5],"roughness":0.6,"pattern":")")+name+R"(","pattern_scale_mm":6,"pattern_contrast":0.9}}])");apply(edit);const auto shot=capture(std::string(".media-pattern-")+name+".png");const double value=spread(shot,cornerA,cornerB);patternLog<<name<<"="<<value<<" ";verify(value>plain+20,std::string("Procedural pattern is not visible in the viewport: ")+name);}
        ++stage;
        // Video: a synthetic H.264 clip beside the working folder, decoded at fixed times.
        std::string videoResult="skipped: ";std::vector<std::vector<unsigned char>> clip;for(int k=0;k<12;++k){std::vector<unsigned char> rgba((size_t)160*96*4,255);static const unsigned char colors[3][3]={{220,30,30},{30,200,40},{30,40,220}};for(size_t i=0;i<(size_t)160*96;++i)for(int c=0;c<3;++c)rgba[i*4+c]=colors[(k/4)%3][c];clip.push_back(std::move(rgba));}
        std::string encodeError;if(writeVideoClip044B(clipName,160,96,12,clip,encodeError)){
            V videoBatch=json(R"([{"op":"image_asset.set","name":"Clip","path":")"+clipName+R"("},{"op":"stamp.set","name":"Clip screen","asset":"Clip","feature":"Screen","faces":["z_max"],"origin":[60,40,8],"u_axis":[1,0,0],"v_axis":[0,1,0],"width":40,"height":24}])");videoBatch.arr[1].set("source_topology",V::makeStr(topology));apply(videoBatch);
            g_mediaClockOverride044B=.1;const auto early=pixel(capture(".media-video-t010.png"),centre);g_mediaClockOverride044B=.6;const auto later=pixel(capture(".media-video-t060.png"),centre);
            verify(dominant(early)==0&&dominant(later)==1,"Video stamp did not show the decoded red then green frames");videoResult="H.264 frames red@0.1 s, green@0.6 s";
        }else videoResult+=encodeError;
        ++stage;
        // Live clock: background decoding and frame uploads advance while visible.
        g_mediaClockOverride044B=-1;std::set<int> seen;const auto started=std::chrono::steady_clock::now();while(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(1600)){seen.insert(dominant(pixel(capture(""),centre)));Sleep(40);}
        seen.erase(-1);verify(seen.size()>=2,"Live media playback did not change the displayed frame");
        const bool visible=IsWindowVisible(g_window)!=0;ShowWindow(g_window,SW_SHOWMINNOACTIVE);std::string pauseResult="window manager did not minimize";if(IsIconic(g_window)){tickMedia044B();const double paused=g_mediaClock044B;Sleep(250);tickMedia044B();verify(std::abs(g_mediaClock044B-paused)<1e-9,"Media clock advanced while minimized");pauseResult="clock held while minimized";}
        ShowWindow(g_window,visible?SW_SHOWNOACTIVATE:SW_HIDE);
        std::error_code ignored;std::filesystem::remove(clipName,ignored);
        std::ofstream receipt(shotPath+".media-044b.log");receipt<<"PASS: GIF stamp frames red/green/blue at media time 0/0.3/0.5 s from framebuffer samples; procedural patterns visible ("<<patternLog.str()<<"plain="<<plain<<"); video "<<videoResult<<"; live playback frames "<<seen.size()<<"; minimized pause: "<<pauseResult<<". Synthetic pixels and boxes only.\n";
        g_error=false;g_status="Media 044B UI checks passed / animated GIF stamp / patterns / video";g_mediaClockOverride044B=-1;return 0;
    }catch(const std::exception& error){return fail(error.what());}
}
