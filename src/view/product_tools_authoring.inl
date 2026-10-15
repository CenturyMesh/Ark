static void lightUpDialog030(){
    using V=arkjson::Value;const auto revision=g_session.revision,epoch=g_documentEpoch;
    if(selBody<0||(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Select a surface, solid or mesh for Light up");
    const auto id=g_bodyIds[selBody];const auto* feature=g_doc.find(id);if(!feature)throw std::runtime_error("Selected part is unavailable");
    const bool face=g_pickKind==1;SelectionItem selected;if(face)selected=stampsSelectedFace();const auto look=face?resolvedFaceAppearance(g_doc,*feature,selected.name):resolvedAppearance(g_doc,*feature);
    std::vector<EditorField> fields{{"LED color R, G, B / 0..1",stampsVectorText(look.emissionIntensity?look.emissionColor:look.baseColor)},{"Intensity / 0..100",std::to_string(look.emissionIntensity>0?look.emissionIntensity:2)},{"Bloom / 0..4",std::to_string(look.bloom)},{"Glow radius / pixels 0..64",std::to_string(look.glow)},{"Effect",look.emissionEffect,false,{"steady","pulse","blink"}},{"Period / seconds",std::to_string(look.emissionPeriod)}};
    if(!editFields("Light up",face?"LED appearance on the selected exact face. Set intensity to zero to switch it off.":"LED appearance on this part and its occurrences. Set intensity to zero to switch it off.",fields))return;
    V patch=V::makeObj();patch.set("emission_color",stampsVector(stampsParseVector(fields[0].value)));for(int i=1;i<=3;++i)patch.set(i==1?"emission_intensity":i==2?"bloom":"glow",V::makeNum(formNumber(fields[i].value)));patch.set("emission_effect",V::makeStr(trimField(fields[4].value)));patch.set("emission_period",V::makeNum(formNumber(fields[5].value)));
    V command=V::makeObj();command.set("op",V::makeStr("light_up"));command.set("id",V::makeNum((double)id));command.set("appearance",patch);if(face){command.set("face",V::makeStr(selected.name));command.set("source_topology",V::makeStr(stampsTopology(id)));}
    V commands=V::makeArr();commands.arr.push_back(command);stampsApplyBatch(commands,revision,epoch);if(!g_error){g_pathTracing=false;g_status="Light up saved / color, intensity, bloom, glow and effect";}
}
static void createProduct030(int action){
    using V=arkjson::Value;const auto revision=g_session.revision,epoch=g_documentEpoch;const bool net=action==981,wood=action==982||action==983,sheet=action==985;
    std::vector<EditorField> fields;V command=V::makeObj(),p=V::makeObj(),spec=V::makeObj();
    if(net){
        fields={{"Name","Woven net"},{"Width / mm","120"},{"Depth / mm","100"},{"Rows / 2..64","8"},{"Columns / 2..64","10"},{"Strand radius / mm","0.5"},{"Sag / mm","8"},{"Weave / 0 or 1","1"}};
        if(!editFields("Mesh net / fabric","Editable tube network. Sag and weave describe shape; they do not simulate cloth mechanics.",fields))return;
        const char* keys[]={"width","depth","rows","columns","radius","sag","weave"};for(int i=1;i<8;++i)p.set(keys[i-1],V::makeStr(trimField(fields[i].value)));
    }else if(wood){
        fields={{"Name",action==982?"Wood panel":"Timber beam"},{"Width X / mm",action==982?"600":"1500"},{"Depth Y / mm",action==982?"400":"60"},{"Height Z / mm",action==982?"18":"100"},{"Grain axis","x",false,{"x","y","z"}}};
        if(!editFields(action==982?"Wood panel":"Timber beam","Dimensions remain editable. Grain is declared; species and structural properties need explicit material inputs.",fields))return;
        for(int i=1;i<4;++i)p.set(i==1?"width":i==2?"depth":"height",V::makeStr(trimField(fields[i].value)));spec.set("op",V::makeStr("wood"));spec.set("grain_axis",V::makeStr(trimField(fields[4].value)));spec.set("joinery",V::makeArr());V appearance=V::makeObj();appearance.set("base_color",stampsVector({.61,.38,.18}));appearance.set("roughness",V::makeNum(.73));command.set("appearance",appearance);
    }else{
        fields={{"Name","Vent"},{"Rows","3"},{"Columns","5"},{"Pitch X / mm","18"},{"Pitch Y / mm","18"},{"Opening width / mm","12"},{"Opening depth / mm","6"},{"First center X / mm","14"},{"First center Y / mm","14"}};
        u64 sheetId=0;if(sheet){fields[0].name="Vent ID prefix";if(selBody<0||(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Select an authored sheet for its vent");sheetId=g_bodyIds[selBody];const auto* selected=g_doc.find(sheetId);if(!selected||selected->type!=FeatureType::SheetMetal)throw std::runtime_error("Select the originating authored sheet");fields.push_back({"Panel ID","base"});}
        else {fields.push_back({"Stock width / mm","110"});fields.push_back({"Stock depth / mm","70"});fields.push_back({"Stock thickness / mm","3"});}
        if(!editFields(sheet?"Sheet vent":"Solid vent / plastic grille",sheet?"Use an ID with letters, digits, dot, underscore or hyphen. Openings retain flat/formed association.":"Rectangular openings with parametric dimensions and spacing.",fields))return;
        if(sheet){V opening=V::makeObj();opening.set("kind",V::makeStr("window"));for(int i=5;i<=8;++i)opening.set(i==5?"width":i==6?"height":i==7?"x":"y",V::makeStr(trimField(fields[i].value)));command.set("op",V::makeStr("sheet.vent"));command.set("id",V::makeNum((double)sheetId));command.set("name",V::makeStr(trimField(fields[0].value)));command.set("panel",V::makeStr(trimField(fields[9].value)));command.set("opening",opening);command.set("rows",V::makeNum(formNumber(fields[1].value)));command.set("columns",V::makeNum(formNumber(fields[2].value)));command.set("pitch_x",V::makeStr(trimField(fields[3].value)));command.set("pitch_y",V::makeStr(trimField(fields[4].value)));}
        else {const char* keys[]={"rows","columns","pitch_x","pitch_y","opening_width","opening_depth","x","y","width","depth","height"};for(int i=1;i<12;++i)p.set(keys[i-1],V::makeStr(trimField(fields[i].value)));spec.set("op",V::makeStr("vent"));spec.set("kind",V::makeStr("rectangle"));}
    }
    if(!sheet){command.set("op",V::makeStr("feature.add"));command.set("type",V::makeStr(net?"mesh_net":"native_solid"));command.set("name",V::makeStr(trimField(fields[0].value)));command.set("p",p);if(!net)command.set("native",spec);}
    V commands=V::makeArr();commands.arr.push_back(command);
    if(!sheet&&!g_doc.assembly.empty()){V placement=V::makeObj();placement.set("op",V::makeStr("assembly.add"));placement.set("feature",V::makeNum((double)g_doc.nextFeatureId_));placement.set("name",V::makeStr(trimField(fields[0].value)));commands.arr.push_back(placement);}
    stampsApplyBatch(commands,revision,epoch);if(!g_error)frameScene();
}
static void woodJoinery030(){
    using V=arkjson::Value;if(selBody<0||(size_t)selBody>=g_bodyIds.size())throw std::runtime_error("Select an authored Wood panel or beam");const auto id=g_bodyIds[selBody];const auto* feature=g_doc.find(id);if(!feature||featureOperation(*feature)!="wood")throw std::runtime_error("Select the authored Wood stock feature");
    const auto revision=g_session.revision,epoch=g_documentEpoch;std::vector<EditorField> fields{{"Joint kind","groove",false,{"pocket","groove","rabbet"}},{"X / mm","20"},{"Y / mm","0"},{"Width / mm","8"},{"Depth Y / mm",feature->get("depth","100")},{"Cut depth Z / mm","6"}};
    if(!editFields("Wood joinery","Add a rectangular top-face cut. Stock and grain remain editable; dimensions and fit allowances are explicit inputs.",fields))return;
    V joint=V::makeObj(),spec=feature->nativeSpec,list=spec.find("joinery")?*spec.find("joinery"):V::makeArr();joint.set("kind",V::makeStr(trimField(fields[0].value)));const char* keys[]={"x","y","width","depth","cut_depth"};for(int i=1;i<6;++i)joint.set(keys[i-1],V::makeStr(trimField(fields[i].value)));list.arr.push_back(joint);spec.set("joinery",list);V command=V::makeObj(),commands=V::makeArr();command.set("op",V::makeStr("feature.edit"));command.set("id",V::makeNum((double)id));command.set("native",spec);commands.arr.push_back(command);stampsApplyBatch(commands,revision,epoch);
}
static void woodCutlist030(){
    auto result=runTool("wood_inspect",arkjson::Value::makeObj());if(g_error)return;std::ostringstream report;report<<"WOOD STOCK / millimetres\r\n\r\n";
    const auto& items=result.find("structuredContent")->find("items")->arr;
    for(const auto& item:items)report<<item.find("name")->asStr()<<"  |  "<<item.find("quantity")->asNum()<<" pcs  |  "<<item.find("width_mm")->asNum()<<" x "<<item.find("depth_mm")->asNum()<<" x "<<item.find("height_mm")->asNum()<<" mm  |  grain "<<item.find("grain_axis")->asStr()<<"\r\n";
    if(items.empty())report<<"No authored Wood stock in the current physical BOM.\r\n";
    std::vector<EditorField> fields{{"Stock list",report.str()}};editFields("Wood stock cutlist","Blank dimensions before joinery, including hidden occurrences. Cutting allowances and structural verification need explicit inputs.",fields,true,"Close",nullptr,true);
}
