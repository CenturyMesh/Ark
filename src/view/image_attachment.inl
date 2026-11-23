// Native Studio user-image composer. Selecting/previewing never contacts a provider.
static std::vector<AssistantImageAttachment> g_pendingAssistantImages;
static size_t g_assistantImageIndex=0;
static AssistantImageMode g_assistantImageMode=AssistantImageMode::InspireDesign;
static std::array<HWND,3> g_assistantImageDimensionEdits{};
static std::array<std::string,3> g_assistantImageDimensionText{};
static u64 g_pendingAssistantImageEpoch=0,g_pendingAssistantImageObjectEpoch=0;
static std::string g_pendingAssistantImageTarget;
static int assistantImageComposerHeight(){return g_pendingAssistantImages.empty()?40:112;}
static void drawAssistantAttachmentThumbnail(const AssistantImageAttachment&,float,float,float,float);
static std::string assistantImageTargetKey(){return g_pendingObjectTarget?std::to_string(g_pendingObjectTarget->feature)+"/"+std::to_string(g_pendingObjectTarget->node)+"/"+std::to_string(g_pendingObjectTarget->copy):std::string{};}
static bool isAssistantImageDimensionEdit(HWND control){return control&&std::find(g_assistantImageDimensionEdits.begin(),g_assistantImageDimensionEdits.end(),control)!=g_assistantImageDimensionEdits.end();}
static void syncAssistantImageDimensions(){
    for(size_t i=0;i<3;++i)if(g_assistantImageDimensionEdits[i]){int length=GetWindowTextLengthW(g_assistantImageDimensionEdits[i]);std::vector<wchar_t> text((size_t)length+1);GetWindowTextW(g_assistantImageDimensionEdits[i],text.data(),length+1);int size=WideCharToMultiByte(CP_UTF8,0,text.data(),length,nullptr,0,nullptr,nullptr);g_assistantImageDimensionText[i].resize((size_t)size);if(size)WideCharToMultiByte(CP_UTF8,0,text.data(),length,g_assistantImageDimensionText[i].data(),size,nullptr,nullptr);}
}
static void clearPendingAssistantImage(){
    g_pendingAssistantImages.clear();g_assistantImageIndex=0;g_pendingAssistantImageTarget.clear();g_assistantImageDimensionText={};for(auto edit:g_assistantImageDimensionEdits)if(edit){SetWindowTextW(edit,L"");ShowWindow(edit,SW_HIDE);}
    layout();
}
static void checkPendingAssistantImageContext(){
    if(!g_pendingAssistantImages.empty()&&(g_pendingAssistantImageEpoch!=g_documentEpoch||g_pendingAssistantImageObjectEpoch!=g_objectDocumentEpoch||g_pendingAssistantImageTarget!=assistantImageTargetKey())){clearPendingAssistantImage();throw std::runtime_error("Drawing, connector or object target changed. Attach the references again before sending.");}
}
static void addPendingAssistantImages(const std::vector<std::string>& paths){
    if(paths.empty())return;
    checkPendingAssistantImageContext();
    if(paths.size()>assistantReferenceLimit-g_pendingAssistantImages.size())
        throw std::runtime_error("Attach at most "+std::to_string(assistantReferenceLimit)+" reference images per message.");
    std::vector<AssistantImageAttachment> batch;
    for(const auto& path:paths){
        try{batch.push_back(loadAssistantImageAttachment(path));}
        catch(const std::exception& error){
            const auto name=std::filesystem::u8path(path).filename().u8string();
            throw std::runtime_error(std::string(name.begin(),name.end())+": "+error.what());
        }
    }
    const auto first=g_pendingAssistantImages.size();
    g_pendingAssistantImages.reserve(first+batch.size());
    for(auto& image:batch)g_pendingAssistantImages.push_back(std::move(image));
    g_assistantImageIndex=first;
    g_pendingAssistantImageEpoch=g_documentEpoch;g_pendingAssistantImageObjectEpoch=g_objectDocumentEpoch;g_pendingAssistantImageTarget=assistantImageTargetKey();
    showAssistantDock();g_toolsOpen=false;g_error=false;g_status="References attached locally. Choose an intent, then Send or Queue.";layout();
}
static arkjson::Value pendingAssistantImageMessage(const std::string& prompt){
    checkPendingAssistantImageContext();syncAssistantImageDimensions();const auto dimensions=!g_pendingAssistantImages.empty()&&g_assistantImageMode==AssistantImageMode::ImageToCAD?parseAssistantImageDimensions(g_assistantImageDimensionText):AssistantImageDimensions{};return assistantUserMessageWithImages(prompt,g_pendingAssistantImages,g_assistantImageMode,dimensions);
}
static std::string assistantImageVisiblePrompt(const std::string& prompt){
    if(g_pendingAssistantImages.empty())return prompt;
    std::string label="["+std::string(assistantImageModeName(g_assistantImageMode))+": ";
    for(size_t i=0;i<g_pendingAssistantImages.size();++i){if(i)label+=", ";label+=g_pendingAssistantImages[i].name;}
    label+="]";
    if(g_assistantImageMode==AssistantImageMode::ImageToCAD){syncAssistantImageDimensions();const auto sizes=parseAssistantImageDimensions(g_assistantImageDimensionText);for(const auto& item:std::initializer_list<std::pair<const char*,std::optional<double>>>{{"width",sizes.width},{"height",sizes.height},{"depth",sizes.depth}})if(item.second){std::ostringstream number;number.imbue(std::locale::classic());number<<*item.second;label+=" "+std::string(item.first)+" ~"+number.str()+" mm";}}
    return label+(prompt.empty()?"":"\n"+prompt);
}
// OFN_EXPLORER returns either a full path or a directory followed by basenames.
static std::vector<std::string> assistantImagePickerPaths(const wchar_t* selection){
    const std::filesystem::path first(selection);
    const auto* next=selection+wcslen(selection)+1;
    std::vector<std::string> paths;
    auto append=[&](const std::filesystem::path& path){const auto utf8=path.u8string();paths.emplace_back(utf8.begin(),utf8.end());};
    if(!*next)append(first);
    else for(;*next;next+=wcslen(next)+1)append(first/next);
    return paths;
}
static bool handleAssistantImageAction(int action){
    if((action<730||action>733)&&action!=738&&action!=739)return false;
    if(action==730){
        const auto epoch=g_documentEpoch,objectEpoch=g_objectDocumentEpoch;
        const auto target=assistantImageTargetKey();
        std::vector<wchar_t> selected(65536);
        OPENFILENAMEW picker{};picker.lStructSize=sizeof picker;picker.hwndOwner=g_window;picker.lpstrFile=selected.data();picker.nMaxFile=(DWORD)selected.size();
        picker.lpstrFilter=L"Image references (PNG, JPEG)\0*.png;*.jpg;*.jpeg\0";picker.lpstrTitle=L"Attach reference images";
        picker.Flags=OFN_EXPLORER|OFN_ALLOWMULTISELECT|OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST;
        if(GetOpenFileNameW(&picker)){
            if(epoch!=g_documentEpoch||objectEpoch!=g_objectDocumentEpoch||target!=assistantImageTargetKey())
                throw std::runtime_error("The image target changed while selecting references. Attach them again.");
            addPendingAssistantImages(assistantImagePickerPaths(selected.data()));
        }else if(CommDlgExtendedError())throw std::runtime_error("Could not open the selected references. Try a smaller selection.");
    }
    else if(action==733){
        if(!g_pendingAssistantImages.empty()){
            g_pendingAssistantImages.erase(g_pendingAssistantImages.begin()+g_assistantImageIndex);
            if(g_pendingAssistantImages.empty())clearPendingAssistantImage();
            else g_assistantImageIndex=std::min(g_assistantImageIndex,g_pendingAssistantImages.size()-1);
            g_error=false;g_status="Reference removed.";
        }
    }
    else if(action==738||action==739){
        const auto count=g_pendingAssistantImages.size();
        if(count)g_assistantImageIndex=(g_assistantImageIndex+(action==738?count-1:1))%count;
    }
    else {syncAssistantImageDimensions();g_assistantImageMode=action==732?AssistantImageMode::ImageToCAD:AssistantImageMode::InspireDesign;g_error=false;g_status=action==732?"Image to CAD: optional rough width, height and depth in mm; blank sizes stay up to the model.":"Inspire design: use the image's visual character to create an editable concept.";}
    return true;
}
// Rough-dimension fields sit on the pending-image card inside the right dock.
static void assistantImageDimensionSlot(size_t index,int& x,int& width){const auto dock=chatDockGeometry();width=std::clamp((dock.inner-76)/3-22,48,122);x=dock.x+dock.pad+82+(int)index*(width+22);}
static void layoutAssistantImageDimensions(){
    const bool visible=g_assistantDockOpen&&!g_chatDockRail&&!g_toolsOpen&&!g_pendingAssistantImages.empty()&&g_assistantImageMode==AssistantImageMode::ImageToCAD;
    if(visible)for(size_t i=0;i<3;++i)if(!g_assistantImageDimensionEdits[i]){auto edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,0,0,1,1,g_window,(HMENU)(INT_PTR)(735+i),GetModuleHandleW(nullptr),nullptr);if(!edit)throw std::runtime_error("Could not create rough dimension fields.");g_assistantImageDimensionEdits[i]=edit;SendMessage(edit,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE);SendMessage(edit,EM_SETLIMITTEXT,24,0);SendMessage(edit,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(6,6));if(!g_assistantImageDimensionText[i].empty())SetWindowTextA(edit,g_assistantImageDimensionText[i].c_str());SendMessageW(edit,0x1501,TRUE,(LPARAM)L"Auto");}
    const auto dock=chatDockGeometry();for(size_t i=0;i<3;++i)if(auto edit=g_assistantImageDimensionEdits[i]){int x=0,width=0;assistantImageDimensionSlot(i,x,width);MoveWindow(edit,x,dock.imageTop+75,width,25,TRUE);ShowWindow(edit,visible?SW_SHOW:SW_HIDE);}
}
// Pending-image card above the composer. The attach action lives in the
// composer's action row; intent toggles appear once an image is attached.
template<class Rect,class Button,class Glyph>static void drawAssistantImageAttachmentUI(Rect&& rect,Button&& button,Glyph&& glyph){
    layoutAssistantImageDimensions();const auto dock=chatDockGeometry();if(g_pendingAssistantImages.empty()||!dock.open||dock.rail)return;
    namespace th=ui_theme;const float x=(float)(dock.x+dock.pad),y=(float)dock.imageTop,width=(float)dock.inner;
    auto ink=[](const std::string& value,float px,float py,float scale,th::Tone color){textOut(value,px,py,scale,color.r,color.g,color.b);};
    rect(x,y,width,104.f,th::rowRadius,th::raised.r,th::raised.g,th::raised.b,1,1,1,1,th::border,1);
    const auto& image=g_pendingAssistantImages[g_assistantImageIndex];
    drawAssistantAttachmentThumbnail(image,x+10,y+10,48,48);
    const bool multiple=g_pendingAssistantImages.size()>1;
    std::string caption=multiple?std::to_string(g_assistantImageIndex+1)+"/"+std::to_string(g_pendingAssistantImages.size())+"  ":"";
    caption+=image.name+" / "+std::to_string(image.width)+" x "+std::to_string(image.height);
    ink(fitText(caption,.56f,std::max(1.f,width-68-(multiple?154:90))),x+68,y+12,.56f,th::text2);
    if(multiple){button("<",x+width-148,y+4,28,738,false,false);button(">",x+width-116,y+4,28,739,false,false);}
    button("Remove",x+width-84,y+4,76,733,false,false);
    const bool cad=g_assistantImageMode==AssistantImageMode::ImageToCAD;const float modeW=std::max(60.f,(width-84)*.5f);
    button("",x+68,y+38,modeW,731,false,!cad);glyph(731,x+74,y+42,24);ink(fitText("Inspire design",.62f,modeW-40),x+102,y+46,.62f,th::text);
    button("",x+74+modeW,y+38,modeW,732,false,cad);glyph(732,x+80+modeW,y+42,24);ink(fitText("Image to CAD",.62f,modeW-40),x+108+modeW,y+46,.62f,th::text);
    if(cad){ink("ROUGH MM",x+10,y+80,.50f,th::faint);const char* letters[]={"W","H","D"};for(size_t i=0;i<3;++i){int fx=0,fw=0;assistantImageDimensionSlot(i,fx,fw);ink(letters[i],(float)fx-13,y+80,.52f,th::muted);}}
    else ink(fitText("Reference stays local until Send / Queue",.54f,width-78),x+68,y+80,.54f,th::muted);
}
