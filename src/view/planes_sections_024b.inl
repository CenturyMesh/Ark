static void planeAuthoringDialog024(int action){
    using V=arkjson::Value;const u64 revision=g_session.revision,epoch=g_documentEpoch;V args=V::makeObj();std::vector<EditorField> fields;std::string title,hint;
    if(action==950){title="Sketch to plane";hint="Moves the saved sketch using its unchanged local coordinates, dimensions and constraints. Dependent features recompute.";fields={{"Saved sketch",g_activeSketch},{"Target plane",g_activePlane}};args.set("action",V::makeStr("sketch_to_plane"));}
    else if(action==951){title="Offset plane";hint="Creates a named plane linked to its source. Offset accepts a dimension expression in mm.";fields={{"New plane name","Offset plane"},{"Source plane",g_activePlane},{"Offset / mm expression","10"}};args.set("action",V::makeStr("offset_plane"));}
    else{title=action==952?"Section from plane":"Section from sketch line";hint="Contour creates one connected exact wire. Positive / negative / both create material cuts; the source remains in history.";
        const auto feature=selBody>=0&&(size_t)selBody<g_bodyIds.size()?std::to_string(g_bodyIds[(size_t)selBody]):"";fields={{"Source feature id / name",feature},{"New feature name","Section"},{"Mode","contour"}};
        if(action==952){fields.push_back({"Section plane",g_activePlane});args.set("action",V::makeStr("section"));}
        else{fields.push_back({"Saved sketch",g_activeSketch});std::string entity;for(const auto& selection:g_picked)if(selection.kind=="curve")entity=selection.name;fields.push_back({"Straight line entity",entity});args.set("action",V::makeStr("section_from_line"));}}
    if(!editFields(title.c_str(),hint.c_str(),fields))return;
    if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("Document changed while configuring plane or section; reopen on the current model");
    if(action==950){args.set("sketch",V::makeStr(trimField(fields[0].value)));args.set("plane",V::makeStr(trimField(fields[1].value)));}
    else if(action==951){args.set("name",V::makeStr(trimField(fields[0].value)));args.set("plane",V::makeStr(trimField(fields[1].value)));args.set("offset",V::makeStr(trimField(fields[2].value)));}
    else{const auto selected=trimField(fields[0].value);V id=V::makeStr(selected);if(!selected.empty()&&std::all_of(selected.begin(),selected.end(),[](unsigned char c){return c>='0'&&c<='9';})){bool ok=false;double value=evalExpr(selected,g_doc.buildEnv(),&ok);if(ok)id=V::makeNum(value);}args.set("id",id);args.set("name",V::makeStr(trimField(fields[1].value)));args.set("mode",V::makeStr(trimField(fields[2].value)));if(action==952)args.set("plane",V::makeStr(trimField(fields[3].value)));else{args.set("sketch",V::makeStr(trimField(fields[3].value)));args.set("entity",V::makeStr(trimField(fields[4].value)));}}
    args.set("expected_revision",V::makeNum((double)revision));runTool("plane_authoring",args);
    if(!g_error){if(action==951)g_activePlane=trimField(fields[0].value);if(action==950){g_activeSketch=trimField(fields[0].value);g_activePlane=trimField(fields[1].value);}g_showSketches=true;rebuildSketchOverlay();frameScene();}
}
