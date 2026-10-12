// Native BOM workspace. Filtered rows retain original definition IDs and revisions.

struct BomPanel {HWND window=nullptr,list=nullptr,header=nullptr,search=nullptr;HFONT font=nullptr,smallFont=nullptr,title=nullptr,big=nullptr,semibold=nullptr;arkjson::Value bom;u64 revision=0,epoch=0;std::vector<size_t> visible;int filter=0,sort=0,side=900;bool descending=false,refreshing=false,done=false;RECT table{};};

static int g_bomSmokeAction=0;static std::string g_bomExportTestPath;static bool g_bomStyleProbe=false;

static std::wstring bomWide(const std::string& s){int n=MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,nullptr,0);std::wstring w((size_t)std::max(n,1),0);if(n)MultiByteToWideChar(CP_UTF8,0,s.c_str(),-1,w.data(),n);return w;}

static std::string bomText(const arkjson::Value& v,const char* key){auto* p=v.find(key);return p?p->asStr():"";}

#include "bom_panel_style.inl"

static void bomTableRefresh(BomPanel& state){u64 keep=0;if(const auto* selected=bomSelected(state))keep=(u64)selected->find("feature")->num;state.bom=inspectBom(g_doc,g_session.revision);state.revision=g_session.revision;state.epoch=g_documentEpoch;bomFilterRows(state,keep);auto status=std::to_wstring(state.bom.find("items")->arr.size())+L" part definitions | Includes hidden placed parts | Snapshot r"+std::to_wstring(state.revision);SetWindowTextW(state.header,status.c_str());}

static void bomEditSelected(BomPanel& state,bool sourcing){

    using V=arkjson::Value;int selection=ListView_GetNextItem(state.list,-1,LVNI_SELECTED);const auto& rows=state.bom.find("items")->arr;if(selection<0||(size_t)selection>=state.visible.size())throw std::runtime_error("Select a BOM row first");if(state.revision!=g_session.revision||state.epoch!=g_documentEpoch)throw std::runtime_error("BOM snapshot changed; Refresh before editing");

    auto row=rows[state.visible[(size_t)selection]];auto meta=*row.find("metadata");auto get=[&](const char* key){return bomText(meta,key);};std::vector<EditorField> fields;

    if(sourcing)fields={{"Manufacturer",get("manufacturer")},{"Manufacturer reference",get("manufacturer_part_number")},{"Supplier",get("supplier")},{"Supplier reference",get("supplier_part_number")},{"Source URL / HTTPS",get("source_url")},{"Datasheet URL / HTTPS",get("datasheet_url")},{"Sourcing status",get("sourcing_status").empty()?"unresolved":get("sourcing_status"),false,{"unresolved","candidate","selected"}}};

    else fields={{"Internal part number",get("part_number")},{"Description",get("description")},{"Category",get("category")},{"Make / buy",get("procurement").empty()?"unspecified":get("procurement"),false,{"unspecified","make","buy"}},{"Representation",get("representation").empty()?"unspecified":get("representation"),false,{"unspecified","exact","exact_imported","simplified","envelope"}},{"Existing material name",bomText(row,"material")},{"Manufacturing route",bomText(row,"manufacturing_route")}};

    EnableWindow(state.window,FALSE);bool accepted=false;try{accepted=editFields(sourcing?"BOM / sourcing":"BOM / part metadata",sourcing?"Keep references and links backed by supplier evidence. Unknown sourcing remains unresolved.":"Edits apply to this definition and every occurrence. Material must exist in the document; blank remains unknown.",fields);}catch(...){EnableWindow(state.window,TRUE);throw;}EnableWindow(state.window,TRUE);if(!accepted)return;if(state.epoch!=g_documentEpoch)throw std::runtime_error("Project changed during BOM edit");

    const char* basic[]={"part_number","description","category","procurement","representation"};const char* commercial[]={"manufacturer","manufacturer_part_number","supplier","supplier_part_number","source_url","datasheet_url","sourcing_status"};for(size_t i=0;i<(sourcing?7u:5u);++i)meta.set(sourcing?commercial[i]:basic[i],V::makeStr(trimField(fields[i].value)));

    V command=V::makeObj(),commands=V::makeArr(),args=V::makeObj();command.set("op",V::makeStr("feature.edit"));command.set("id",*row.find("feature"));command.set("part",meta);if(!sourcing){if(trimField(fields[5].value)!=bomText(row,"material"))command.set("material",V::makeStr(trimField(fields[5].value)));V manufacturing=V::makeObj();manufacturing.set("route",V::makeStr(trimField(fields[6].value).empty()?"unspecified":trimField(fields[6].value)));if(trimField(fields[6].value)!=bomText(row,"manufacturing_route"))command.set("manufacturing",manufacturing);}commands.arr.push_back(command);args.set("commands",commands);args.set("expected_revision",V::makeNum((double)state.revision));runTool("doc_apply",args);if(g_error)throw std::runtime_error(g_status);bomTableRefresh(state);

}

static void bomExportSnapshot(BomPanel& state){

    using V=arkjson::Value;if(state.revision!=g_session.revision||state.epoch!=g_documentEpoch)throw std::runtime_error("BOM snapshot changed; Refresh before exporting");std::string path=g_bomExportTestPath;

    if(path.empty()){wchar_t file[32768]=L"BOM.json";OPENFILENAMEW choose{};choose.lStructSize=sizeof choose;choose.hwndOwner=state.window;choose.lpstrFilter=L"BOM JSON\0*.json\0\0";choose.lpstrFile=file;choose.nMaxFile=32768;choose.lpstrDefExt=L"json";choose.Flags=OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(!GetSaveFileNameW(&choose))return;auto encoded=std::filesystem::path(file).u8string();path=std::string(encoded.begin(),encoded.end());}

    V args=V::makeObj();args.set("path",V::makeStr(path));args.set("expected_revision",V::makeNum((double)state.revision));runTool("bom_export",args);if(g_error)throw std::runtime_error(g_status);SetWindowTextW(state.header,L"BOM exported as local JSON. Existing exports are never overwritten.");

}

static void bomCapture(HWND h,const std::string& path){RECT r{};GetWindowRect(h,&r);int width=r.right-r.left,height=r.bottom-r.top;HDC screen=GetDC(h),dc=CreateCompatibleDC(screen);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* bits=nullptr;auto bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);if(PrintWindow(h,dc,0)){Image image;image.w=width;image.h=height;image.px.resize((size_t)width*height*3);for(size_t i=0;i<(size_t)width*height;++i){auto* pixel=(unsigned char*)bits+i*4;image.px[i*3]=pixel[2];image.px[i*3+1]=pixel[1];image.px[i*3+2]=pixel[0];}writePNG(path.c_str(),image);}SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(h,screen);}


#include "bom_panel_window.inl"
