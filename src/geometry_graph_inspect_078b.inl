namespace {
V inspectGeometryGraph078B(const Document& document,u64 revision,const V& args){
    const auto* requested=args.find("ids");std::set<u64> requestedIds;
    if(requested){if(requested->t!=V::Arr||requested->arr.empty()||requested->arr.size()>64)throw std::runtime_error("Geometry inspect ids requires1..64 distinct existing feature IDs");for(const auto& id:requested->arr){const auto value=integer(id);if(!document.find(value)||!requestedIds.insert(value).second)throw std::runtime_error("Geometry inspect IDs must exist and be distinct");}}
    size_t limit=16;if(const auto* size=args.find("limit")){limit=(size_t)integer(*size);if(limit>64)throw std::runtime_error("Geometry inspect limit must be1..64");}
    V selection=V::makeObj();selection.set("limit",V::makeNum(limit));if(requested){V selected=V::makeArr();for(auto id:requestedIds)selected.arr.push_back(V::makeNum((double)id));selection.set("ids",std::move(selected));}
    const auto source=iterationSha256(documentToJson(document,false,false)),selectionHash=iterationSha256(selection.dump());
    V full=geometryNodeSnapshot078B(document,revision);std::vector<const V*> selected;
    for(const auto& row:get(full,"nodes").arr)if(!requested||requestedIds.count(integer(get(row,"id"))))selected.push_back(&row);
    size_t offset=0;if(const auto* cursor=args.find("cursor")){
        allowed(*cursor,{"source_sha256","revision","selection_sha256","offset"});
        if(text(*cursor,"source_sha256")!=source||integer(get(*cursor,"revision"),true)!=revision||text(*cursor,"selection_sha256")!=selectionHash)throw std::runtime_error("Stale geometry graph cursor; inspect the current graph again");
        offset=(size_t)integer(get(*cursor,"offset"),true);if(offset>selected.size())throw std::runtime_error("Geometry graph cursor offset is outside the selected nodes");
    }
    V nodes=V::makeArr(),links=V::makeArr(),boundary=V::makeArr(),page=V::makeObj();std::set<u64> pageIds,neighbors;
    const auto end=std::min(selected.size(),offset+limit);
    for(size_t i=offset;i<end;++i){V row=*selected[i];pageIds.insert(integer(get(row,"id")));for(const char* field:{"inputs","input_ports"})if(const auto* entries=row.find(field);entries&&entries->arr.size()>64){V clipped=*entries;clipped.arr.resize(64);row.set(std::string(field)+"_total",V::makeNum(entries->arr.size()));row.set(std::string(field)+"_truncated",V::makeBool(true));row.set(field,std::move(clipped));}nodes.arr.push_back(std::move(row));}
    size_t incident=0;for(const auto& link:get(full,"links").arr){const auto sourceId=integer(get(link,"source")),targetId=integer(get(link,"target"));if(!pageIds.count(sourceId)&&!pageIds.count(targetId))continue;++incident;if(links.arr.size()<128)links.arr.push_back(link);if(!pageIds.count(sourceId))neighbors.insert(sourceId);if(!pageIds.count(targetId))neighbors.insert(targetId);}
    for(auto id:neighbors){if(boundary.arr.size()==64)break;boundary.arr.push_back(V::makeNum((double)id));}
    page.set("offset",V::makeNum(offset));page.set("returned",V::makeNum(nodes.arr.size()));page.set("selected_nodes",V::makeNum(selected.size()));page.set("total_nodes",V::makeNum(document.features.size()));page.set("has_more",V::makeBool(end<selected.size()));page.set("incident_links",V::makeNum(incident));page.set("links_truncated",V::makeBool(incident>links.arr.size()));page.set("boundary_nodes",V::makeNum(neighbors.size()));page.set("boundary_truncated",V::makeBool(neighbors.size()>boundary.arr.size()));
    full.set("nodes",std::move(nodes));full.set("links",std::move(links));full.set("page",std::move(page));full.set("boundary_ids",std::move(boundary));full.set("source_sha256",V::makeStr(source));full.set("partial",V::makeBool(end<selected.size()||!neighbors.empty()||incident>128));full.set("next_call",V{});full.set("context_call",V{});
    if(end<selected.size()){V cursor=V::makeObj();cursor.set("source_sha256",V::makeStr(source));cursor.set("revision",V::makeNum((double)revision));cursor.set("selection_sha256",V::makeStr(selectionHash));cursor.set("offset",V::makeNum(end));V next=selection;next.set("action",V::makeStr("inspect"));next.set("expected_revision",V::makeNum((double)revision));next.set("cursor",std::move(cursor));full.set("next_call",std::move(next));}
    if(!pageIds.empty()){V args=V::makeObj(),keys=V::makeArr();for(auto id:pageIds)keys.arr.push_back(V::makeStr("feature:"+std::to_string(id)));args.set("keys",std::move(keys));args.set("depth",V::makeNum(1));args.set("limit",V::makeNum(64));args.set("expected_revision",V::makeNum((double)revision));full.set("context_call",std::move(args));}
    full.set("inspection_scope",V::makeStr("Bounded node/port metadata, with incident links and explicit boundary. next_call continues geometry_graph; context_call uses context_graph for all adjacent sources/consumers, including links beyond the excerpt. Native recipes, image/mesh buffers and full parameters stay out of this overview."));return full;
}
}
