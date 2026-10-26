        // A manual edit must reject stale work, then permit a fresh inspection
        // and a newly reasoned write in the same Single conversation.
        for(bool opacityEdit:{true,false}){
            const auto before=g_session.revision;
            auto gate=std::make_shared<std::atomic_bool>(false);
            auto calls=std::make_shared<std::atomic_int>(0);
            auto observed=std::make_shared<std::atomic_bool>(true);
            AssistantTransport recovery=[gate,calls,observed,before](const auto&,const V& request,const auto* cancelled){
                while(!*gate){if(cancelled&&cancelled->load())throw std::runtime_error("Cancelled revision fixture");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
                const int step=(*calls)++;const auto history=request.dump();
                if(step==1&&history.find("STUDIO_REVISION_CHANGED")==std::string::npos)*observed=false;
                if(step==2&&history.find("doc_info detail must")==std::string::npos)*observed=false;
                V result=V::makeObj(),output=V::makeArr(),item=V::makeObj();result.set("status",V::makeStr("completed"));
                if(step<6){
                    V args=V::makeObj();std::string tool="doc_apply";
                    if(step==1||step==3){tool="doc_info";if(step==1)args.set("detail",V::makeStr("invalid-detail"));}
                    else {V commands=V::makeArr(),command=V::makeObj();command.set("op",V::makeStr("param.set"));command.set("name",V::makeStr(step==5?"recovered_ui_probe":"stale_ui_probe"));command.set("expr",V::makeStr(std::to_string(before+99)));commands.arr.push_back(command);args.set("commands",commands);args.set("expected_revision",V::makeNum(step==5||step==2?before+1:before));}
                    item.set("type",V::makeStr("function_call"));item.set("call_id",V::makeStr("revision_recovery_"+std::to_string(before)+"_"+std::to_string(step)));item.set("name",V::makeStr(tool));item.set("arguments",V::makeStr(args.dump()));
                }else {item.set("type",V::makeStr("message"));item.set("role",V::makeStr("assistant"));V content=V::makeArr(),text=V::makeObj();text.set("type",V::makeStr("output_text"));text.set("text",V::makeStr("Retained task continued after inspecting the manual edit."));content.arr.push_back(text);item.set("content",content);}
                output.arr.push_back(item);result.set("output",output);return result;
            };
            sendAssistant("Offline manual-edit continuity test.",recovery);
            const double previousYaw=yaw,previousPitch=pitch;const int orbitX=viewX+viewW/2,orbitY=viewY+viewH/2;
            SendMessage(g_window,WM_MBUTTONDOWN,MK_MBUTTON,MAKELPARAM(orbitX,orbitY));SendMessage(g_window,WM_MOUSEMOVE,MK_MBUTTON,MAKELPARAM(orbitX+32,orbitY+20));SendMessage(g_window,WM_MBUTTONUP,0,MAKELPARAM(orbitX+32,orbitY+20));
            const bool viewOnly=(yaw!=previousYaw||pitch!=previousPitch)&&g_session.revision==before&&g_chatBusy&&!*g_assistantCancel;
            if(opacityEdit){selBody=0;uiAction(560);}else applyCommand(parse(R"({"op":"param.set","name":"human_ui_probe","expr":"1"})"));
            const bool edited=!g_error&&g_session.revision==before+1;*gate=true;
            if(!pump()||!viewOnly||!edited||!*observed||*calls!=7||*g_assistantCancel||g_assistantResponseError||g_conversation.arr.empty()||g_session.revision!=before+2||g_doc.getParam("recovered_ui_probe")!=before+99||std::any_of(g_doc.params.begin(),g_doc.params.end(),[](auto& p){return p.name=="stale_ui_probe";}))return assistantFailure(121);
            if(opacityEdit&&resolvedAppearance(g_doc,*g_doc.find(g_bodyIds[0])).opacity>=.999)return assistantFailure(121);
            if(!opacityEdit&&g_doc.getParam("human_ui_probe")!=1)return assistantFailure(121);
            try{assistantRequest(g_assistantConfig,g_conversation,AgentSession::tools());}catch(...){return assistantFailure(121);}
        }
        // Replacing the document/connector still cancels old-context requests.
        auto oldContext=std::make_unique<UiToolRequest>();oldContext->tool="doc_info";oldContext->args=V::makeObj();oldContext->epoch=g_documentEpoch-1;oldContext->revision=std::make_shared<std::atomic<u64>>(g_session.revision);auto oldCancel=std::make_shared<std::atomic_bool>(false);oldContext->cancel=oldCancel;bool rejected=false;oldContext->complete=[&](V response){rejected=response.find("isError")->b;};g_toolQueue.push_back(std::move(oldContext));drainUiTools();if(!rejected||!*oldCancel||*g_assistantCancel)return assistantFailure(121);
        if(assistantRevisionUiTest){
            g_status="Assistant continuity passed / opacity and manual edit / failed inspection / stale write / fresh inspection / retained task / replaced context";
            g_swap=false;drawFrame(hdc);glFinish();Image image;image.w=winW;image.h=winH;image.px.resize((size_t)winW*winH*3);std::vector<u8> pixels(image.px.size());glPixelStorei(GL_PACK_ALIGNMENT,1);glReadBuffer(GL_BACK);glReadPixels(0,0,winW,winH,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());for(int y=0;y<winH;++y)std::copy_n(pixels.begin()+(size_t)(winH-1-y)*winW*3,(size_t)winW*3,image.px.begin()+(size_t)y*winW*3);if(!writePNG(shotPath.c_str(),image)||std::all_of(pixels.begin(),pixels.end(),[](u8 value){return value==0;}))return assistantFailure(121);
            return 0;
        }
