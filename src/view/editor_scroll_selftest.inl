if(editorScrollUiTest){
    auto fail=[&](int code,const std::string& message){fprintf(stderr,"Native editor scroll UI: %s\n",message.c_str());return code;};
    const auto revision=g_session.revision;const auto source=documentToJson(g_doc);g_assistantDockOpen=false;layout();
    auto fixture=[](){std::vector<EditorField> fields;for(int i=0;i<24;++i)fields.push_back({"Dimension "+std::to_string(i+1),"original_"+std::to_string(i)});fields[3]={"Retain symmetry","1",true};fields[4]={"Reference plane","XY",false,{"XY","XZ","YZ"}};fields.back().name="Last dimension";return fields;};
    g_editorWorkAreaOverride={0,0,1280,720};
    for(UINT dpi:{96u,144u,192u}){
        auto fields=fixture();bool passed=true;std::string failure;g_editorDpiOverride=dpi;
        auto check=[&](bool ok,const char* message){if(!ok){passed=false;if(failure.empty())failure=message;}};
        g_editorSmokeProbe=[&](HWND dialog){
            const auto panel=GetDlgItem(dialog,9020);auto* state=(EditorScrollState*)GetWindowLongPtr(panel,GWLP_USERDATA);
            check(state&&state->contentHeight>0,"No native scroll viewport");if(!state)return;
            RECT window{},viewport{};GetWindowRect(dialog,&window);GetClientRect(panel,&viewport);
            check(window.left>=0&&window.top>=0&&window.right<=1280&&window.bottom<=720,"Native dialog exceeds 1280x720 work area");
            check(state->contentHeight>viewport.bottom&&GetWindowLong(panel,GWL_STYLE)&WS_VSCROLL,"Large form has no usable overflow scrollbar");
            auto first=editorFieldControl(dialog,3),last=editorFieldControl(dialog,26),choice=editorFieldControl(dialog,7);
            check(first&&last&&choice,"Fields were lost when reparenting to the scroll viewport");if(!first||!last||!choice)return;
            check(editorAccessibleNameEquals(first,"Dimension 1")&&editorAccessibleNameEquals(last,"Last dimension")&&editorAccessibleNameEquals(choice,"Reference plane")&&editorAccessibleNameEquals(editorFieldControl(dialog,6),"Retain symmetry"),"Native accessibility names do not match visible field labels");
            check((SendMessage(dialog,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)==0,"Keyboard focus indicators are hidden");
            SetWindowTextA(first,("first_"+std::to_string(dpi)).c_str());
            SetFocus(last);RECT field{};GetWindowRect(last,&field);MapWindowPoints(nullptr,panel,(POINT*)&field,2);
            check(state->offset>0&&field.top>=0&&field.bottom<=viewport.bottom,"Focusing the last field did not reveal it");
            const auto bottom=state->offset;SendMessage(last,WM_KEYDOWN,VK_PRIOR,0);check(state->offset<bottom,"Page Up did not scroll through native keyboard input");
            SendMessage(last,WM_KEYDOWN,VK_NEXT,0);check(state->offset>0,"Page Down did not scroll through native keyboard input");
            SetFocus(choice);const auto selection=SendMessage(choice,CB_GETCURSEL,0,0);const auto before=state->offset;SendMessage(choice,WM_MOUSEWHEEL,MAKEWPARAM(0,120),0);
            check(state->offset<before&&SendMessage(choice,CB_GETCURSEL,0,0)==selection,"Wheel scrolling changed a combo value or failed to move the form");
            SendMessage(panel,WM_VSCROLL,SB_TOP,0);check(state->offset==0,"Scrollbar Home did not reach the first field");
            SendMessage(panel,WM_VSCROLL,SB_BOTTOM,0);check(state->offset>0,"Scrollbar End did not reach the last field");
            SetFocus(last);SetWindowTextA(last,("last_"+std::to_string(dpi)).c_str());
            SendMessage(dialog,WM_NEXTDLGCTL,0,FALSE);check(GetDlgCtrlID(GetFocus())==2||GetDlgCtrlID(GetFocus())==1,"Tab did not leave the nested field viewport for a commit/cancel control");
            SendMessage(dialog,WM_NEXTDLGCTL,(WPARAM)last,TRUE);check(GetFocus()==last,"Keyboard focus could not return to the last field");
            for(int id:{1,2}){RECT button{},client{};GetWindowRect(GetDlgItem(dialog,id),&button);MapWindowPoints(nullptr,dialog,(POINT*)&button,2);GetClientRect(dialog,&client);check(button.top>=0&&button.bottom<=client.bottom&&button.left>=0&&button.right<=client.right,"Commit/cancel control is clipped");}
            // Inject the same messages Windows sends on DPI/work-area changes.
            // This validates reflow mechanics, not physical monitor hardware.
            for(UINT nextDpi:{192u,144u,dpi}){
                RECT suggested{20,20,1000,680};SendMessage(dialog,WM_DPICHANGED,MAKEWPARAM(nextDpi,nextDpi),(LPARAM)&suggested);
                GetWindowRect(dialog,&window);GetClientRect(panel,&viewport);GetWindowRect(last,&field);MapWindowPoints(nullptr,panel,(POINT*)&field,2);
                check(window.left>=0&&window.top>=0&&window.right<=1280&&window.bottom<=720,"DPI transition exceeds monitor work area");
                check(GetFocus()==last&&field.top>=0&&field.bottom<=viewport.bottom,"DPI transition lost focused field or left it clipped");
                check(editorAccessibleNameEquals(last,"Last dimension"),"DPI transition lost accessible naming");
                for(int id:{1,2}){RECT button{},client{};GetWindowRect(GetDlgItem(dialog,id),&button);MapWindowPoints(nullptr,dialog,(POINT*)&button,2);GetClientRect(dialog,&client);check(button.top>=0&&button.bottom<=client.bottom&&button.left>=0&&button.right<=client.right,"DPI transition clipped commit/cancel controls");}
            }
            g_editorWorkAreaOverride={0,0,1100,660};SendMessage(dialog,WM_SETTINGCHANGE,SPI_SETWORKAREA,0);GetWindowRect(dialog,&window);check(window.right<=1100&&window.bottom<=660,"Work-area change did not reflow native editor");g_editorWorkAreaOverride={0,0,1280,720};SendMessage(dialog,WM_SETTINGCHANGE,SPI_SETWORKAREA,0);
        };
        g_dialogSmoke=3;g_dialogShot=shotPath+".editor-"+std::to_string(dpi)+"dpi.png";const bool accepted=editFields("Scrollable native dimensions","All fields stay editable. Wheel, Page Up/Page Down, Tab and the scrollbar navigate the form; Apply and Cancel remain visible.",fields);g_dialogSmoke=0;g_editorSmokeProbe={};
        if(!accepted||!passed)return fail(920,failure.empty()?"Scrollable form did not accept its result":failure);
        if(fields.front().value!="first_"+std::to_string(dpi)||fields.back().value!="last_"+std::to_string(dpi)||fields[3].value!="1"||fields[4].value!="XY")return fail(921,"Scrolling failed to retain all field types and edits");
    }
    auto cancelled=fixture();g_editorDpiOverride=96;g_editorSmokeProbe=[](HWND dialog){auto last=editorFieldControl(dialog,26);SetFocus(last);SetWindowTextA(last,"cancelled edit");};g_dialogSmoke=2;
    const bool accepted=editFields("Cancel scrolling dimensions","Cancelled fields remain unchanged in the caller.",cancelled);g_dialogSmoke=0;g_editorSmokeProbe={};g_editorDpiOverride=0;g_editorWorkAreaOverride={};
    if(accepted||cancelled.back().value!="original_23"||g_session.revision!=revision||documentToJson(g_doc)!=source)return fail(922,"Cancel or a native form changed the saved document");
    g_status="Native form checks passed / 1280x720 / 96, 144 and 192 DPI layout / wheel / keyboard / focus / last-field edit / Cancel";
}
