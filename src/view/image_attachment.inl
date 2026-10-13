// Native Studio user-image composer. Selecting/previewing never contacts a provider.
static std::optional<AssistantImageAttachment> g_pendingAssistantImage;
static AssistantImageMode g_assistantImageMode=AssistantImageMode::InspireDesign;
static std::array<HWND,3> g_assistantImageDimensionEdits{};
static std::array<std::string,3> g_assistantImageDimensionText{};
static u64 g_pendingAssistantImageEpoch=0,g_pendingAssistantImageObjectEpoch=0;
static std::string g_pendingAssistantImageTarget;
static int assistantImageComposerHeight(){return g_pendingAssistantImage?112:40;}
static void drawAssistantAttachmentThumbnail(const AssistantImageAttachment&,float,float,float,float);
static std::string assistantImageTargetKey(){return g_pendingObjectTarget?std::to_string(g_pendingObjectTarget->feature)+"/"+std::to_string(g_pendingObjectTarget->node)+"/"+std::to_string(g_pendingObjectTarget->copy):std::string{};}
static bool isAssistantImageDimensionEdit(HWND control){return control&&std::find(g_assistantImageDimensionEdits.begin(),g_assistantImageDimensionEdits.end(),control)!=g_assistantImageDimensionEdits.end();}
static void syncAssistantImageDimensions(){
    for(size_t i=0;i<3;++i)if(g_assistantImageDimensionEdits[i]){int length=GetWindowTextLengthW(g_assistantImageDimensionEdits[i]);std::vector<wchar_t> text((size_t)length+1);GetWindowTextW(g_assistantImageDimensionEdits[i],text.data(),length+1);int size=WideCharToMultiByte(CP_UTF8,0,text.data(),length,nullptr,0,nullptr,nullptr);g_assistantImageDimensionText[i].resize((size_t)size);if(size)WideCharToMultiByte(CP_UTF8,0,text.data(),length,g_assistantImageDimensionText[i].data(),size,nullptr,nullptr);}
}
static void clearPendingAssistantImage(){
    g_pendingAssistantImage.reset();g_pendingAssistantImageTarget.clear();g_assistantImageDimensionText={};for(auto edit:g_assistantImageDimensionEdits)if(edit){SetWindowTextW(edit,L"");ShowWindow(edit,SW_HIDE);}
    layout();
}
static void checkPendingAssistantImageContext(){
    if(g_pendingAssistantImage&&(g_pendingAssistantImageEpoch!=g_documentEpoch||g_pendingAssistantImageObjectEpoch!=g_objectDocumentEpoch||g_pendingAssistantImageTarget!=assistantImageTargetKey())){clearPendingAssistantImage();throw std::runtime_error("Drawing, connector or object target changed. Attach the image again before sending.");}
}
static void setPendingAssistantImage(const std::string& path){
    const auto epoch=g_documentEpoch,objectEpoch=g_objectDocumentEpoch;const auto target=assistantImageTargetKey();auto image=loadAssistantImageAttachment(path);
    if(epoch!=g_documentEpoch||objectEpoch!=g_objectDocumentEpoch||target!=assistantImageTargetKey())throw std::runtime_error("The image target changed while opening the file; attach it again.");
    clearPendingAssistantImage();g_pendingAssistantImage=std::move(image);g_pendingAssistantImageEpoch=epoch;g_pendingAssistantImageObjectEpoch=objectEpoch;g_pendingAssistantImageTarget=target;g_assistantDockOpen=true;g_toolsOpen=false;g_error=false;g_status="Image attached locally. Choose an intent and Send or Queue to share it with the selected agent.";layout();
}
static arkjson::Value pendingAssistantImageMessage(const std::string& prompt){
    checkPendingAssistantImageContext();syncAssistantImageDimensions();const auto dimensions=g_pendingAssistantImage&&g_assistantImageMode==AssistantImageMode::ImageToCAD?parseAssistantImageDimensions(g_assistantImageDimensionText):AssistantImageDimensions{};return assistantUserMessage(prompt,g_pendingAssistantImage,g_assistantImageMode,dimensions);
}
static std::string assistantImageVisiblePrompt(const std::string& prompt){
    if(!g_pendingAssistantImage)return prompt;std::string label="["+std::string(assistantImageModeName(g_assistantImageMode))+": "+g_pendingAssistantImage->name+"]";
    if(g_assistantImageMode==AssistantImageMode::ImageToCAD){syncAssistantImageDimensions();const auto sizes=parseAssistantImageDimensions(g_assistantImageDimensionText);for(const auto& item:std::initializer_list<std::pair<const char*,std::optional<double>>>{{"width",sizes.width},{"height",sizes.height},{"depth",sizes.depth}})if(item.second){std::ostringstream number;number.imbue(std::locale::classic());number<<*item.second;label+=" "+std::string(item.first)+" ~"+number.str()+" mm";}}
    return label+(prompt.empty()?"":"\n"+prompt);
}
static bool handleAssistantImageAction(int action){
    if(action<730||action>733)return false;
    if(action==730){wchar_t selected[32768]{};OPENFILENAMEW picker{};picker.lStructSize=sizeof picker;picker.hwndOwner=g_window;picker.lpstrFile=selected;picker.nMaxFile=32768;picker.lpstrFilter=L"Image reference (PNG, JPEG)\0*.png;*.jpg;*.jpeg\0";picker.lpstrTitle=L"Attach an image reference to the next message";picker.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;if(GetOpenFileNameW(&picker)){int size=WideCharToMultiByte(CP_UTF8,0,selected,-1,nullptr,0,nullptr,nullptr);std::string path((size_t)size,'\0');WideCharToMultiByte(CP_UTF8,0,selected,-1,path.data(),size,nullptr,nullptr);path.pop_back();setPendingAssistantImage(path);}}
    else if(action==733){clearPendingAssistantImage();g_status="Pending image removed.";}
    else {syncAssistantImageDimensions();g_assistantImageMode=action==732?AssistantImageMode::ImageToCAD:AssistantImageMode::InspireDesign;g_error=false;g_status=action==732?"Image to CAD: optional rough width, height and depth in mm; blank sizes stay up to the model.":"Inspire design: use the image's visual character to create an editable concept.";}
    return true;
}
static void layoutAssistantImageDimensions(){
    const bool visible=g_assistantDockOpen&&!g_toolsOpen&&g_pendingAssistantImage&&g_assistantImageMode==AssistantImageMode::ImageToCAD;
    if(visible)for(size_t i=0;i<3;++i)if(!g_assistantImageDimensionEdits[i]){auto edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,0,0,1,1,g_window,(HMENU)(INT_PTR)(735+i),GetModuleHandleW(nullptr),nullptr);if(!edit)throw std::runtime_error("Could not create rough dimension fields.");g_assistantImageDimensionEdits[i]=edit;SendMessage(edit,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);SendMessage(edit,EM_SETLIMITTEXT,24,0);SendMessage(edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(6,6));if(!g_assistantImageDimensionText[i].empty())SetWindowTextA(edit,g_assistantImageDimensionText[i].c_str());SendMessageW(edit,0x1501,TRUE,(LPARAM)L"Auto");}
    const int width=std::clamp((viewW-360)/3,72,122);for(size_t i=0;i<3;++i)if(auto edit=g_assistantImageDimensionEdits[i]){MoveWindow(edit,viewX+172+(int)i*(width+18),winH-182,width,25,TRUE);ShowWindow(edit,visible?SW_SHOW:SW_HIDE);}
}
template<class Rect,class Button,class Glyph>static void drawAssistantImageAttachmentUI(Rect&& rect,Button&& button,Glyph&& glyph){
    layoutAssistantImageDimensions();const float x=(float)viewX+22,y=(float)winH-(g_pendingAssistantImage?252:188),width=(float)viewW-44;
    rect(x,y,width,g_pendingAssistantImage?104.f:40.f,7,.055f,.068f,.082f,1,.20f,.28f,.34f,.8f,1);
    button("",x+10,y+8,116,730,false,false);glyph(730,x+17,y+12,24);textOut("Attach image",x+43,y+16,.64f,.86f,.88f,.92f);
    button("",x+138,y+8,140,731,false,g_assistantImageMode==AssistantImageMode::InspireDesign);glyph(731,x+145,y+12,24);textOut("Inspire design",x+173,y+16,.64f,.86f,.88f,.92f);
    button("",x+282,y+8,134,732,false,g_assistantImageMode==AssistantImageMode::ImageToCAD);glyph(732,x+289,y+12,24);textOut("Image to CAD",x+318,y+16,.64f,.86f,.88f,.92f);
    if(g_pendingAssistantImage){
        drawAssistantAttachmentThumbnail(*g_pendingAssistantImage,x+10,y+47,48,48);
        std::string caption=g_pendingAssistantImage->name+" / "+std::to_string(g_pendingAssistantImage->width)+" x "+std::to_string(g_pendingAssistantImage->height);
        textOut(fitText(caption,.56f,g_assistantImageMode==AssistantImageMode::ImageToCAD?70.f:width-170),x+70,y+48,.56f,.76f,.86f,.94f);button("Remove",x+width-88,y+49,76,733,false,false);
        if(g_assistantImageMode==AssistantImageMode::ImageToCAD){textOut("ROUGH MM",x+70,y+80,.50f,.56f,.72f,.79f);const int fieldWidth=std::clamp((viewW-360)/3,72,122);const char* names[]={"Width","Height","Depth"};for(int i=0;i<3;++i)textOut(names[i],(float)viewX+172+i*(fieldWidth+18),y+48,.53f,.67f,.79f,.88f);}
        else textOut("Reference stays local until Send / Queue",x+70,y+80,.54f,.56f,.72f,.79f);
    }
}
