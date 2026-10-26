// Runs inside studio-selection-picking-ui, against an isolated public fixture.
// Mouse and Tab input use the production window procedure, not cycle helpers.
{
    auto tab=[&](bool reverse=false,HWND receiver=nullptr){
        BYTE saved[256]{},keys[256]{};if(!GetKeyboardState(saved))return false;std::copy_n(saved,256,keys);
        keys[VK_CONTROL]=keys[VK_MENU]=keys[VK_LCONTROL]=keys[VK_RCONTROL]=keys[VK_LMENU]=keys[VK_RMENU]=0;
        keys[VK_SHIFT]=keys[VK_LSHIFT]=reverse?0x80:0;keys[VK_RSHIFT]=0;
        if(!SetKeyboardState(keys))return false;
        SendMessage(receiver?receiver:hwnd,WM_KEYDOWN,VK_TAB,1);SendMessage(receiver?receiver:hwnd,WM_KEYUP,VK_TAB,1);
        return SetKeyboardState(saved)!=FALSE;
    };
    // The preceding sampled patch must never acquire native references via Tab.
    if(!click(170))return fail(1040,"Body selector missing before mesh Tab rejection");draw();pointClick({viewX+viewW/2,viewY+viewH/2});
    if(selBody<0||!tab()||!g_picked.empty()||g_status.find("sampled mesh")==std::string::npos)return fail(1041,"Tab promoted sampled mesh triangles to exact faces");
    fresh();apply(R"({"commands":[{"op":"feature.add","type":"native_solid","name":"Face cycle definition","native":{"op":"box"},"p":{"width":40,"depth":35,"height":30}},{"op":"assembly.add","name":"Face cycle patterned copies","feature":1,"pattern":{"type":"linear","count":3,"dx":"70"}},{"op":"assembly.add","name":"Face cycle other occurrence","feature":1,"p":{"ty":"70","rz":"20"}}]})");
    if(g_error)return fail(1042,"Face cycle exact occurrence fixture rejected");
    g_cameraOrtho=true;yaw=tYaw=0;pitch=tPitch=0;frameScene();draw();
    auto bodyPoint=[&](int body,POINT& at){
        draw();if(body<0||(size_t)body>=g_bodyIds.size())return false;const auto& mesh=g_cpuMeshes[g_instancePart[(size_t)body]];
        for(size_t triangle=0;triangle<mesh.triCount();++triangle){Vec3 point{};for(int k=0;k<3;++k)point+=directMeshPoint(mesh,mesh.idx[triangle*3+k]);float x,y;
            if(!projectScreen(g_instanceWorld[(size_t)body].transformPoint(point/3.),x,y)||x<viewX||x>=viewX+viewW||y<viewY+155||y>=viewY+viewH)continue;
            const bool covered=std::any_of(g_zones.begin(),g_zones.end(),[&](const Zone& zone){return x>=zone.x&&x<=zone.x+zone.w&&y>=zone.y&&y<=zone.y+zone.h;});
            if(!covered&&pickBody((int)x,(int)y)==body){at={(LONG)x,(LONG)y};return true;}
        }return false;
    };
    int patterned=-1,other=-1;for(size_t i=0;i<g_bodyIds.size();++i){if(g_instanceCopies[i]==2)patterned=(int)i;if(g_instanceIds[i]==2)other=(int)i;}
    POINT patternPoint{},otherPoint{};if(!bodyPoint(patterned,patternPoint)||!bodyPoint(other,otherPoint))return fail(1043,"Patterned and separately placed occurrences cannot be clicked");
    const u64 cycleRevision=g_session.revision;const auto cycleSource=documentToJson(g_doc,false,false);
    pointClick(patternPoint);if(selBody!=patterned||!g_picked.empty()||g_pickKind!=0||GetFocus()!=hwnd)return fail(1044,"Body click changed selection mode or failed to focus the viewport");
    const u64 patternNode=g_instanceIds[(size_t)patterned];std::vector<std::string> order;
    for(int face=0;face<6;++face){if(!tab()||g_picked.size()!=1)return fail(1045,"Tab did not select a single exact face");const auto& item=g_picked.front();
        if(selBody!=patterned||g_pickKind!=0||item.kind!="face"||item.feature!=1||item.instance!=patternNode||item.copy!=2||item.name.empty()||!item.meshTriangles.empty()||!item.meshVertices.empty()||g_selectionFaceFill.empty())return fail(1046,"Face cycle lost exact reference, placed copy or selected face geometry");
        if(!g_partPicks[g_instancePart[(size_t)patterned]].faceReferences.count(item.name))return fail(1047,"Face cycle invented a semantic reference");order.push_back(item.name);
        if(item.name=="z_max"&&!capture(".tab-pattern-face.png"))return fail(1048,"Placed Tab face capture failed");
    }
    if(std::set<std::string>(order.begin(),order.end()).size()!=6||g_faceCycle059.brepSha256.size()!=64)return fail(1049,"Box cycle repeated aliases or lost its B-rep fingerprint");
    if(!tab()||g_picked.front().name!=order.front()||!tab(true)||g_picked.front().name!=order.back()||!tab(true)||g_picked.front().name!=order[4])return fail(1050,"Tab wrap or Shift+Tab reverse order failed");
    pointClick(patternPoint);if(!tab(true)||g_picked.front().name!=order.back())return fail(1051,"Shift+Tab did not start at the last exact face after a body click");
    pointClick(patternPoint);for(const auto& name:order)if(!tab()||g_picked.front().name!=name)return fail(1052,"Re-clicking the same occurrence changed deterministic face order");
    pointClick(otherPoint);if(selBody!=other||!g_picked.empty()||!tab()||g_picked.front().instance!=2||g_picked.front().copy!=0||g_picked.front().name!=order.front())return fail(1053,"Next body click retained the previous occurrence's cycle");
    // Native text controls own Tab, including a defensively forwarded message.
    HWND field=CreateWindowExA(0,"EDIT","fixture field",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,12,12,150,22,hwnd,nullptr,GetModuleHandle(nullptr),nullptr);
    if(!field)return fail(1054,"Native Edit focus fixture failed");const auto selected=g_picked.front();SetFocus(field);
    bool textFocus=GetFocus()==field&&tab(false,field)&&tab()&&g_picked.size()==1&&sameReference(selected,g_picked.front())&&GetFocus()==field;
    DestroyWindow(field);SetFocus(hwnd);if(!textFocus)return fail(1055,"Face cycling intercepted native text focus Tab");
    if(g_session.revision!=cycleRevision||documentToJson(g_doc,false,false)!=cycleSource)return fail(1056,"Mouse/Tab face selection changed the authored document");
    pointClick(patternPoint);if(!tab())return fail(1057,"Stale selection setup failed");
    apply(R"({"commands":[{"op":"feature.edit","id":1,"p":{"width":42}}]})");if(g_error)return fail(1058,"Native revision-change fixture failed");
    const u64 editedRevision=g_session.revision;const auto editedSource=documentToJson(g_doc,false,false);
    if(!tab()||!g_picked.empty()||g_status.find("expired")==std::string::npos)return fail(1059,"Tab silently rebound a stale face cycle after an exact source edit");
    frameScene();if(!bodyPoint(patterned,patternPoint))return fail(1060,"Edited occurrence cannot be clicked");pointClick(patternPoint);if(!tab())return fail(1061,"Hidden occurrence setup failed");
    uiAction(22000+patterned);if(!tab()||!g_picked.empty())return fail(1062,"Tab retained an exact face of a hidden occurrence");uiAction(22000+patterned);
    if(!bodyPoint(patterned,patternPoint))return fail(1063,"Restored occurrence cannot be clicked");pointClick(patternPoint);
    g_stockMode=true;const bool rejectedPreview=tab()&&g_picked.empty()&&g_status.find("Formed")!=std::string::npos;g_stockMode=false;
    if(!rejectedPreview)return fail(1064,"Tab accepted unsupported stock preview as exact authored geometry");
    if(g_session.revision!=editedRevision||documentToJson(g_doc,false,false)!=editedSource)return fail(1065,"Rejected Tab changed source geometry or revision");
    pointClick(patternPoint);for(int face=0;face<6;++face){if(!tab())return fail(1066,"Final cycle failed");if(g_picked.front().name=="z_max")break;}
    if(!capture(".tab-exact-face-final.png"))return fail(1067,"Final exact face cycle capture failed");
    fprintf(stderr,"Face cycle UI: exact 6-face traversal, wrap/reverse, pattern copy 2, second occurrence, native Edit focus, source immutability, stale/hidden/mesh/preview rejection passed.\n");
}
