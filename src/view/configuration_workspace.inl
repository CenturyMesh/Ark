// Native editor for retained configuration and selective-loading recipes. Every authoring
// operation goes through the same observed-revision transaction as agent edits.
static std::string g_configurationEditorShot;
static void configurationEditorShot(const char* suffix){if(!g_configurationEditorShot.empty())g_dialogShot=g_configurationEditorShot+suffix;}
static std::string configurationText(const arkjson::Value& v,const char* key,const std::string& fallback={}){auto p=v.find(key);return p&&p->t==arkjson::Value::Str?p->str:fallback;}
static std::vector<std::string> configurationNames(const arkjson::Value& rows){std::vector<std::string> names;for(const auto& row:rows.arr)names.push_back(configurationText(row,"name"));return names.empty()?std::vector<std::string>{"(none)"}:names;}
static arkjson::Value configurationNamed(const arkjson::Value& rows,const std::string& name){for(const auto& row:rows.arr)if(configurationText(row,"name")==name)return row;throw std::runtime_error("The selected recipe no longer exists; reopen its workspace");}
static u64 configurationNodeId(const std::string& label){if(label=="(none)"||label=="(keep all)")return 0;auto mark=label.find("  ");auto number=label.substr(0,mark);if(number.empty()||number.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Select an assembly node from the list");return std::stoull(number);}
static std::vector<std::string> configurationNodes(bool keep=false){std::vector<std::string> rows;if(keep)rows.push_back("(keep all)");for(const auto& node:g_doc.assembly)rows.push_back(std::to_string(node.id)+"  "+node.name);if(rows.empty())rows.push_back("(none)");return rows;}
static void configurationRevision(u64 revision,u64 epoch){if(revision!=g_session.revision||epoch!=g_documentEpoch)throw std::runtime_error("The document changed while the workspace was open; inspect its current state before retrying");}
static void showConfigurationWorkspace(){
    using V=arkjson::Value;const auto revision=g_session.revision,epoch=g_documentEpoch;auto names=configurationNames(g_doc.assemblyConfigurations),nodes=configurationNodes();
    std::vector<EditorField> choose{{"Action","Create snapshot",false,{"Create snapshot","Edit recipe","Apply","Remove","Load subtree","Load all"}},{"Configuration",names.front(),false,names},{"Assembly root",nodes.front(),false,nodes}};
    if(!editFields("Configurations and selective loading","Capture and apply named parameter/visibility recipes. Subtree loading retains required dependencies and mates.",choose))return;configurationRevision(revision,epoch);
    const auto action=choose[0].value;V command=V::makeObj();
    if(action=="Create snapshot"||action=="Edit recipe"){
        V recipe=V::makeObj(),parameters=V::makeObj(),visibility=V::makeObj();std::string name="Configuration "+std::to_string(g_doc.assemblyConfigurations.arr.size()+1);
        if(action=="Edit recipe"){recipe=configurationNamed(g_doc.assemblyConfigurations,choose[1].value);name=choose[1].value;if(auto p=recipe.find("parameters"))parameters=*p;if(auto p=recipe.find("visibility"))visibility=*p;}
        else{for(const auto& p:g_doc.params)parameters.set(p.name,V::makeStr(p.expr));for(const auto& node:g_doc.assembly)visibility.set(std::to_string(node.id),V::makeBool(node.visible));}
        std::vector<std::string> parameterNames{"(keep all)"};for(const auto& p:g_doc.params)parameterNames.push_back(p.name);auto visibilityNodes=configurationNodes(true);
        std::vector<EditorField> fields{{"Recipe name",name},{"Parameter override","(keep all)",false,parameterNames},{"Expression","10"},{"Visibility override","(keep all)",false,visibilityNodes},{"Visible","1",true}};
        configurationEditorShot(".recipe.png");if(!editFields(action=="Edit recipe"?"Edit configuration recipe":"Capture configuration","Retain current values, optionally override one parameter and one occurrence. Reopen Edit recipe for more overrides.",fields))return;configurationRevision(revision,epoch);
        if(fields[1].value!="(keep all)"){bool ok=false;auto value=evalExpr(fields[2].value,g_doc.buildEnv(),&ok);if(!ok||!std::isfinite(value))throw std::runtime_error("Configuration expression is unresolved");parameters.set(fields[1].value,V::makeStr(trimField(fields[2].value)));}
        if(auto node=configurationNodeId(fields[3].value))visibility.set(std::to_string(node),V::makeBool(fields[4].value=="1"));
        if(action=="Edit recipe"&&trimField(fields[0].value)!=name)throw std::runtime_error("Keep the recipe name when editing; capture a new snapshot to create another configuration");
        command.set("op",V::makeStr("configuration.set"));command.set("name",V::makeStr(trimField(fields[0].value)));command.set("parameters",parameters);command.set("visibility",visibility);
    }else if(action=="Apply"||action=="Remove"){if(choose[1].value=="(none)")throw std::runtime_error("Create a configuration first");command.set("op",V::makeStr(action=="Apply"?"configuration.apply":"configuration.remove"));command.set("name",V::makeStr(choose[1].value));}
    else{command.set("op",V::makeStr("assembly.load"));V roots;if(action=="Load subtree"){auto node=configurationNodeId(choose[2].value);if(!node)throw std::runtime_error("Create or select an assembly root first");roots=V::makeArr();roots.arr.push_back(V::makeNum((double)node));}command.set("roots",roots);}
    applyUiCommandAtRevision(command,revision,epoch);if(!g_error){g_status="Configuration workspace / "+action+" / revision "+std::to_string(g_session.revision);if(action=="Apply"||action=="Load subtree"||action=="Load all")frameScene();}
}
