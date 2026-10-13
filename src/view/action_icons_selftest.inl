        const u64 revision=g_session.revision,epoch=g_documentEpoch;
        const int originalW=winW,originalH=winH,originalModule=g_toolModule;const bool originalDock=g_assistantDockOpen;
        auto failure=[&](const std::string& detail){std::ofstream(shotPath+".action-icons-test.log")<<"FAIL: "<<detail<<'\n';std::fprintf(stderr,"Action icons UI: %s\n",detail.c_str());return 521;};
        const std::vector<int> actions{58,357,585,586,587,590,549,152,153,154,650,659,710,730,731,732,750,755};
        HMENU menu=CreatePopupMenu();for(int action:actions)AppendMenuA(menu,MF_STRING,(UINT_PTR)action,"Action icon");decorateMenu(menu);
        for(size_t i=0;i<actions.size();++i){MENUITEMINFOW item{sizeof item};item.fMask=MIIM_BITMAP;BITMAP bitmap{};
            if(!GetMenuItemInfoW(menu,(UINT)i,TRUE,&item)||!item.hbmpItem||!GetObject(item.hbmpItem,sizeof bitmap,&bitmap)||bitmap.bmWidth!=22||bitmap.bmHeight!=22){DestroyMenu(menu);return failure("A native menu action has no 22px bitmap.");}
            const auto* pixels=static_cast<unsigned char*>(bitmap.bmBits);unsigned coverage=0;for(int pixel=0;pixel<22*22;++pixel)if(pixels[pixel*4+3]>32)++coverage;
            if(coverage<35||coverage>320){DestroyMenu(menu);return failure("A native menu icon has missing or excessive coverage.");}
        }DestroyMenu(menu);
        auto resize=[&](int width,int height){RECT bounds{0,0,width,height};AdjustWindowRectEx(&bounds,(DWORD)GetWindowLongPtr(hwnd,GWL_STYLE),FALSE,(DWORD)GetWindowLongPtr(hwnd,GWL_EXSTYLE));SetWindowPos(hwnd,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);winW=width;winH=height;layout();};
        auto capture=[&](const std::string& suffix){g_swap=false;drawFrame(hdc);glFinish();std::vector<u8> pixels((size_t)winW*winH*3);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());if(glGetError()!=GL_NO_ERROR)return false;Image image;image.w=winW;image.h=winH;image.px.resize(pixels.size());for(int y=0;y<winH;++y)std::memcpy(image.px.data()+(size_t)y*winW*3,pixels.data()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3);return writePNG((shotPath+suffix).c_str(),image);};
        g_assistantDockOpen=false;layout();
        for(auto size:std::vector<std::pair<int,int>>{{1180,800},{1600,1000}}){resize(size.first,size.second);
            for(int module:{1,3,4,5,6}){g_toolModule=module;if(!capture(".icons-"+std::to_string(module)+"-"+std::to_string(size.first)+".png"))return failure("Could not capture an affected ribbon.");
                if(module==4||module==5){for(int id:module==4?std::vector<int>{58,357}:std::vector<int>{58}){auto zone=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return zone.id==id;});if(zone==g_zones.end()||zone->x<0||zone->x+zone->w>winW||zone->w<24)return failure("Connect / Providers ribbon control is missing or clipped.");}}
            }
            g_assistantDockOpen=true;g_toolModule=4;
            if(!capture(".icons-image-actions-"+std::to_string(size.first)+".png"))return failure("Could not capture the image-reference controls.");
            for(int id:{730,731,732}){auto zone=std::find_if(g_zones.begin(),g_zones.end(),[&](const Zone& item){return item.id==id;});if(zone==g_zones.end()||zone->x<viewX||zone->x+zone->w>viewX+viewW)return failure("An image-reference action is missing or clipped.");}
            g_assistantDockOpen=false;
        }
        g_toolModule=4;startLiveBridge();if(g_liveBridge.endpoint().empty())return failure("Could not start the isolated live-document bridge.");
        for(const char* target:{"codex","claude-code","claude-desktop","opencode"}){drawFrame(hdc);auto zone=std::find_if(g_zones.begin(),g_zones.end(),[](const Zone& zone){return zone.id==58;});if(zone==g_zones.end()){g_liveBridge.stop();return failure("Connect has no hit target.");}
            g_dialogSmoke=3;g_dialogShot=shotPath+".connect-"+target+".png";g_dialogOverrideQueue={{target}};int x=(int)(zone->x+zone->w/2),y=(int)(zone->y+zone->h/2);SendMessage(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessage(hwnd,WM_LBUTTONUP,0,MAKELPARAM(x,y));g_dialogSmoke=0;g_dialogShot.clear();
            if(g_error||g_status.find(std::string("External MCP configuration for ")+target)!=0){g_liveBridge.stop();return failure("Connect click failed to export a client profile.");}
        }g_liveBridge.stop();
        if(g_session.revision!=revision||g_documentEpoch!=epoch)return failure("Icon/connection export changed the drawing revision or epoch.");
        resize(originalW,originalH);g_toolModule=originalModule;g_assistantDockOpen=originalDock;layout();
        std::ofstream(shotPath+".action-icons-test.log")<<"PASS: native menu bitmaps/coverage including authoring atlas, actual affected ribbons and image-reference controls at 1180x800 and 1600x1000, visible Connect/Providers hit targets, real Connect clicks and raw configuration dialogs for Codex/Claude Code/Claude Desktop/OpenCode, unchanged document. No account authentication or provider inference.\n";
        g_status="Action icons verified / menus / ribbon / external client export";
