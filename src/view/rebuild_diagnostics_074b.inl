static std::string rebuildReportText074B(const arkjson::Value& report){
    using V=arkjson::Value;auto field=[](const V& v,const char* key)->const V&{static V absent;const auto* item=v.find(key);return item?*item:absent;};
    auto words=[](std::string value){std::replace(value.begin(),value.end(),'_',' ');return value;};
    std::ostringstream out;out<<"Action: "<<words(field(report,"mode").asStr())<<"\r\n";
    out<<"Evaluated: "<<field(report,"evaluated").asNum()<<" / reused: "<<field(report,"reused").asNum()<<" / failed: "<<field(report,"failed").asNum()<<"\r\n";
    out<<"Geometry evaluation: "<<field(report,"recompute_ms").asNum()<<" ms\r\n";
    out<<"Projection attempts: "<<field(report,"projection_builds").asNum()<<" / reused: "<<field(report,"projection_reused").asNum()<<"\r\n";
    const auto& rows=field(report,"features");if(rows.t==V::Arr)for(const auto& row:rows.arr){out<<"\r\n#"<<field(row,"id").asNum()<<"  "<<field(row,"name").asStr()<<"  ["<<words(field(row,"status").asStr())<<"]\r\n";
        out<<"Reason: ";const auto& reasons=field(row,"reasons");for(size_t i=0;i<reasons.arr.size();++i){if(i)out<<", ";out<<words(reasons.arr[i].asStr());}
        out<<"\r\nScope: "<<words(field(row,"dependency_scope").asStr());const auto& inputs=field(row,"dependency_ids");if(!inputs.arr.empty()){out<<" / inputs:";for(const auto& id:inputs.arr)out<<" #"<<id.asNum();}
        const auto& named=field(row,"named_sources");if(!named.arr.empty()){out<<"\r\nSources:";for(const auto& name:named.arr)out<<" "<<name.asStr();}out<<"\r\n";
    }
    if(rows.arr.empty())out<<"\r\nNo per-feature evaluation was performed in this action.\r\n";
    if(const auto* prepared=report.find("preparation");prepared&&prepared->t==V::Obj)out<<"\r\nPREVIEW PREPARATION\r\n"<<rebuildReportText074B(*prepared);
    return out.str();
}
static void rebuildDiagnosticsDialog074B(){
    using V=arkjson::Value;V args=V::makeObj();args.set("detail",V::makeStr("evaluation"));const auto response=runTool("doc_info",args);if(g_error)return;
    const auto* report=response.find("structuredContent");if(!report)throw std::runtime_error("Rebuild evidence is unavailable");
    std::vector<EditorField> fields{{"Rebuild evidence",rebuildReportText074B(*report)}};
    editFields("Why was it rebuilt?","Reasons for the latest action. Reused shapes keep their validated geometry; this report does not change the drawing.",fields,true,"Close",nullptr,true);
}
