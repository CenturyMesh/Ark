        // B.01/A.04: attach the public startup fixture to a real occurrence so
        // Appearance, Opacity and Move controls all participate in layout checks.
        if(g_bodyIds.empty()){std::fprintf(stderr,"Navigation fixture has no body\n");return 402;}
        if(!g_instanceIds.front()){using V=arkjson::Value;V command=V::makeObj(),commands=V::makeArr(),args=V::makeObj();command.set("op",V::makeStr("assembly.add"));command.set("name",V::makeStr("Navigation fixture occurrence"));command.set("feature",V::makeNum((double)g_bodyIds.front()));commands.arr.push_back(command);args.set("commands",commands);args.set("expected_revision",V::makeNum((double)g_session.revision));runTool("doc_apply",args);if(g_error)return 402;}
        uiAction(2000);
        // All following navigation/display interactions must preserve this source.
        u64 revision=g_session.revision,epoch=g_documentEpoch;int savedW=winW,savedH=winH,savedSelection=selBody;auto savedPicked=g_picked;
        bool savedDock=g_assistantDockOpen,savedMode=g_teamMode,savedCompact=g_compactNavigation;unsigned savedCount=g_teamRoleCount;double savedRoll=cameraRoll;
        g_assistantDockOpen=true;g_teamMode=true;g_teamRoleCount=3;g_compactNavigation=false;g_pickKind=1;g_verifyTeamCardBounds=true;g_swap=false;
        auto failure=[&](const std::string& detail){std::ofstream log(shotPath+".navigation-test.log");log<<detail<<"\n";std::fprintf(stderr,"Navigation UI selftest: %s\n",detail.c_str());return 402;};
        auto resize=[&](int width,int height){RECT bounds{0,0,width,height};AdjustWindowRectEx(&bounds,(DWORD)GetWindowLongPtr(g_window,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtr(g_window,GWL_EXSTYLE));SetWindowPos(g_window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=width;winH=height;layout();};
        auto draw=[&]{HDC dc=GetDC(g_window);drawFrame(dc);glFinish();ReleaseDC(g_window,dc);};
        auto readPixels=[&](int left,int top,int width,int height){std::vector<u8> pixels((size_t)width*height*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(left,winH-top-height,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());Image image;image.w=width;image.h=height;image.px.resize(pixels.size());for(int y=0;y<height;++y)std::memcpy(image.px.data()+(size_t)y*width*3,pixels.data()+(size_t)(height-1-y)*width*3,(size_t)width*3);return image;};
        auto widget=[&](CubeHit hover=CubeHit{}){glEnable(GL_SCISSOR_TEST);glScissor(g_cubeX,winH-g_cubeY-g_cubeS,g_cubeS,g_cubeS);glClearColor(.044f,.052f,.058f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);g_cubeHover=hover;paintViewCube();glFinish();return readPixels(g_cubeX,g_cubeY,g_cubeS,g_cubeS);};
        auto write=[&](const std::string& suffix,const Image& image){return writePNG((shotPath+suffix+".png").c_str(),image);};
        auto click=[&](int x,int y){SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(x,y));};
        auto snap=[&](const NavigationFacet& f){static const int faces[]={182,183,23,184,180,181};if(f.face>=0)uiAction(faces[f.face]);else setOrbitView(std::atan2(f.normal.x,f.normal.z),std::asin(f.normal.y),true,"Diagonal");yaw=tYaw;pitch=tPitch;};
        if(g_navigationFacets.size()!=26||g_cubeTriangles.size()!=44||g_navigationGeometryBuilds!=1)return failure("Retained compass must contain six cardinal, twelve edge and eight corner facets.");
        for(auto size:std::vector<std::pair<int,int>>{{1180,800},{1600,1000}}){resize(size.first,size.second);frameScene();target=tTarget;dist=tDist;uiAction(24);yaw=tYaw;pitch=tPitch;cameraRoll=.35;draw();std::string detail;
            if(!navigationUiFits(detail)||!g_teamCardContentFits||viewH<160)return failure(detail+" / "+g_teamCardBoundsFailure);
            auto shot=readPixels(0,0,winW,winH);std::array<unsigned,3> colors{};
            for(int y=g_compassBounds.top;y<g_compassBounds.bottom;++y)for(int x=g_compassBounds.left;x<g_compassBounds.right;++x){const u8* p=shot.px.data()+((size_t)y*winW+x)*3;for(int axis=0;axis<3;++axis)if(p[axis]>80&&p[axis]>p[(axis+1)%3]*1.2&&p[axis]>p[(axis+2)%3]*1.2)++colors[axis];}
            for(unsigned count:colors)if(count<20)return failure("Cartesian triad lost an RGB axis in actual rendered pixels.");
            if(!write(".navigation-"+std::to_string(size.first)+"x"+std::to_string(size.second),shot))return failure("Could not save navigation window capture.");
            // Keep all display modes reachable with Assistant + navigation bar
            // in the actual supported compact window, not a hidden-dock fixture.
            g_compactNavigation=true;draw();
            if(g_cubeS<96)return failure("Assistant + navigation controls made the cube unreadable.");
            for(int id:{462,963,464,455,176,177,178,186}){auto zone=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id;});if(zone==g_zones.end())return failure("Assistant + navigation controls hid a display/navigation action.");
                if(zone->x<g_cubeX+g_cubeS&&zone->x+zone->w>g_cubeX&&zone->y<g_cubeY+g_cubeS&&zone->y+zone->h>g_cubeY)return failure("Display/navigation control overlaps the cube.");
                if(id==462||id==963||id==464){click((int)(zone->x+zone->w*.5f),(int)(zone->y+zone->h*.5f));if(wireframe!=(id==464)||(id==462&&g_showEdges)||(id==963&&!g_showEdges))return failure("Display style click failed with Assistant + navigation controls.");draw();}
                if(id>=176&&id<=178){click((int)(zone->x+zone->w*.5f),(int)(zone->y+zone->h*.5f));if(g_navMode!=id-176)return failure("Navigation mode click failed in unified toolbar.");draw();}
                if(id==186){const int previous=g_occlusionMode;click((int)(zone->x+zone->w*.5f),(int)(zone->y+zone->h*.5f));if(g_occlusionMode!=(previous+1)%4)return failure("Occlusion click failed in unified toolbar.");while(g_occlusionMode!=previous)uiAction(186);draw();}}
            const auto bar=viewportBarLayout();for(int id:{560,561,562}){auto zone=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& z){return z.id==id;});if(zone==g_zones.end())return failure("Selected occurrence lost an Appearance/Opacity/Move control.");if(zone->x<bar.x+bar.width&&zone->x+zone->w>bar.x&&zone->y<bar.y+bar.height&&zone->y+zone->h>bar.y)return failure("Unified toolbar overlaps selected-occurrence controls.");}
            if(!navigationUiFits(detail))return failure(detail);
            if(!write(".navigation-"+std::to_string(size.first)+"x"+std::to_string(size.second)+"-compact-controls",readPixels(0,0,winW,winH)))return failure("Could not save compact controls capture.");
            g_compactNavigation=false;uiAction(176);uiAction(462);draw();
        }
        unsigned directionChecks=0,labelChecks=0,edgePixels=0;std::ostringstream timings;
        for(int size:{96,136,204,272}){g_navigationTestSize=size;draw();
            // These physical-pixel sizes cover compact, 100%, 150% and 200%
            // widget rendering; they do not claim a monitor-DPI transition test.
            for(size_t index=0;index<g_navigationFacets.size();++index)for(double roll:{0.,.63}){
                const auto& f=g_navigationFacets[index];snap(f);cameraRoll=roll;auto image=widget();int x=g_cubeX+g_cubeS/2,y=g_cubeY+g_cubeS/2;auto hit=cubeHitAt(x,y);
                if(!hit.hit||hit.facet!=(int)index||hit.face!=f.face||hit.normal.dot(f.normal)<.999999)return failure("Visible facet and picking differ at size "+std::to_string(size)+", facet "+std::to_string(index)+", roll "+std::to_string(roll));
                double expectedYaw=tYaw,expectedPitch=tPitch;click(x,y);
                if(std::abs(tYaw-expectedYaw)>1e-8||std::abs(tPitch-expectedPitch)>1e-8||!g_cameraOrtho||std::abs(cameraRoll)>1e-12)return failure("Actual facet mouse click changed the world-axis snap mapping.");
                if(cameraFrame(tYaw,tPitch,0).outward.dot(f.normal)<.998)return failure("Named view does not face the clicked world-axis facet (Top is +Y, Front is +Z).");
                if(selBody!=savedSelection||g_picked.size()!=savedPicked.size()||g_session.revision!=revision||g_documentEpoch!=epoch)return failure("Navigation selected or edited the CAD model.");
                ++directionChecks;
                if(f.face>=0){const auto& bounds=g_navigationLabelBounds[(size_t)f.face];if(bounds.right<=bounds.left){write(".navigation-failed-label",image);return failure("Cardinal word disappeared at physical size "+std::to_string(size)+", face "+std::to_string(f.face)+", roll "+std::to_string(roll));}unsigned ink=0,soft=0;
                    for(int py=std::max(0,(int)bounds.top-g_cubeY);py<std::min(g_cubeS,(int)bounds.bottom-g_cubeY);++py)for(int px=std::max(0,(int)bounds.left-g_cubeX);px<std::min(g_cubeS,(int)bounds.right-g_cubeX);++px){const u8* p=image.px.data()+((size_t)py*g_cubeS+px)*3;if(p[0]<80&&p[1]<100&&p[2]<130)++ink;else if(p[0]>35&&p[0]<145&&p[2]>55)++soft;}
                    if(ink<4||soft<3)return failure("Cardinal font lacks ink or antialias coverage in GL pixels.");++labelChecks;
                }
                if(roll==0&&(index==4||index==12||index==25)&&!write(".navigation-widget-"+std::to_string(size)+"-facet-"+std::to_string(index),image))return failure("Could not save widget capture.");
            }
            // A subpixel silhouette must contain blended pixels rather than a
            // stair-stepped opaque outline. The clear colour is (11,13,15).
            yaw=.85;pitch=.65;cameraRoll=.35;auto base=widget();for(size_t p=0;p<base.px.size();p+=3)if(base.px[p]>15&&base.px[p]<55&&base.px[p+1]>18&&base.px[p+2]>22)++edgePixels;
            auto hover=cubeHitAt(g_cubeX+g_cubeS/2,g_cubeY+g_cubeS/2);auto hovered=widget(hover);unsigned changed=0;for(size_t p=0;p<base.px.size();p+=3)if(std::abs((int)base.px[p]-(int)hovered.px[p])>12)++changed;
            if(changed<20)return failure("Individual facet hover produces no distinct rendered highlight.");
            if(!write(".navigation-widget-"+std::to_string(size)+"-overview",base)||!write(".navigation-widget-"+std::to_string(size)+"-hover",hovered))return failure("Could not save hover overview.");
            unsigned allocations=g_navigationTargetAllocations,geometry=g_navigationGeometryBuilds,fonts=g_navigationFontBuilds;
            std::vector<double> frameMs;
            for(int frame=0;frame<12;++frame){yaw+=.035;pitch-=.014;auto started=std::chrono::steady_clock::now();paintViewCube();glFinish();frameMs.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());}
            std::sort(frameMs.begin(),frameMs.end());timings<<"Physical extent "<<size<<" px: retained widget paint + GPU finish, 12 orbit frames, median "<<frameMs[frameMs.size()/2]<<" ms, max "<<frameMs.back()<<" ms (no screenshot readback).\n";
            if(g_navigationTargetAllocations!=allocations||g_navigationGeometryBuilds!=geometry||g_navigationFontBuilds!=fonts)return failure("Orbiting rebuilt retained geometry, font or render target.");
        }
        if(edgePixels<30||!g_navigationTargetReady||g_navigationFontBuilds!=1)return failure("Supersampled outline/font target did not produce verified coverage.");
        g_navigationTestSize=0;draw();
        // Blank widget padding consumes the pointer, including reference mode.
        int blankX=g_cubeX+2,blankY=g_cubeY+2;if(cubeHitAt(blankX,blankY).hit)return failure("Compass padding unexpectedly hits geometry.");click(blankX,blankY);
        if(selBody!=savedSelection||g_picked.size()!=savedPicked.size()||g_session.revision!=revision)return failure("Blank compass padding selected the model behind it.");
        auto home=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& z){return z.id==191;});if(home==g_zones.end())return failure("Home control disappeared.");click((int)(home->x+home->w*.5),(int)(home->y+home->h*.5));if(g_cameraOrtho||std::abs(tYaw-.85)>1e-8||std::abs(tPitch-.65)>1e-8)return failure("Home control lost its perspective mapping.");
        draw();int cx=g_cubeX+g_cubeS/2,cy=g_cubeY+g_cubeS/2;double beforeYaw=tYaw,beforePitch=tPitch;
        SendMessage(g_window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(cx,cy));SendMessage(g_window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(cx+18,cy+10));SendMessage(g_window,WM_LBUTTONUP,0,MAKELPARAM(cx+18,cy+10));
        if(std::abs(tYaw-beforeYaw)+std::abs(tPitch-beforePitch)<.001)return failure("Dragging compass no longer orbits the camera.");
        if(g_session.revision!=revision||g_documentEpoch!=epoch)return failure("Navigation changed document authority.");
        std::ofstream receipt(shotPath+".navigation-test.log");receipt<<"PASS: 26 facets; "<<directionChecks<<" mouse snap/roll/physical-size checks; "<<labelChecks<<" cardinal pixel checks; "<<edgePixels<<" blended outline pixels; geometry/font built once; targets reused while orbiting; padding/Home/orbit; unchanged document.\n"<<timings.str();
        resize(savedW,savedH);g_assistantDockOpen=savedDock;g_teamMode=savedMode;g_teamRoleCount=savedCount;g_compactNavigation=savedCompact;g_pickKind=0;g_verifyTeamCardBounds=false;cameraRoll=savedRoll;uiAction(24);yaw=tYaw;pitch=tPitch;frameScene();target=tTarget;dist=tDist;
        g_status="Navigation checks passed / 26 facets / antialiased words / retained GPU data / unchanged document";
