// 050B drag chain: Properties rows for a selected link occurrence. The state is solved from
// the displayed copy-0 instance frames, so Baked Motions playback shows the loop rolling.
static bool dragChainProperties(u64 node,std::string& title,std::vector<std::pair<std::string,std::string>>& rows){
    int index=-1;const auto name=dragChainOfNode(g_doc,node,&index);if(name.empty())return false;
    title="DRAG CHAIN / "+name;rows.clear();
    auto mm=[](double value){char text[48];std::snprintf(text,sizeof text,"%.2f mm",value);return std::string(text);};
    try{std::unordered_map<u64,Mat4> world;for(size_t i=0;i<g_instanceIds.size()&&i<g_instanceWorld.size()&&i<g_instanceCopies.size();++i)if(!g_instanceCopies[i])world[g_instanceIds[i]]=g_instanceWorld[i];
        const auto state=dragChainState(g_doc,name,world);const auto* loop=state.find("loop");
        rows.push_back({"Link",std::to_string(index+1)+" of "+std::to_string((int)state.find("link_count")->num)});
        char dimensions[64];std::snprintf(dimensions,sizeof dimensions,"%.2f / %.2f mm",state.find("pitch_mm")->num,state.find("bend_radius_mm")->num);
        rows.push_back({"Pitch / R",dimensions});
        rows.push_back({"Carriage c",mm(state.find("carriage_mm")->num)});
        rows.push_back({"Fixed run",mm(state.find("fixed_run_mm")->num)});
        rows.push_back({"Moving run",mm(state.find("moving_run_mm")->num)});
        rows.push_back({"Loop apex",mm(loop->find("apex_along_run_mm")->num)});
        rows.push_back({"Driving joint","mate #"+std::to_string((u64)state.find("joint")->num)});
    }catch(const std::exception& error){rows.push_back({"State",std::string(error.what()).substr(0,34)});}
    return true;
}
