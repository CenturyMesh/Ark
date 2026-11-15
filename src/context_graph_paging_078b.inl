// Read-only deterministic neighborhood pages. A continuation is a selector,
// never edit authority, and is invalid after a source/revision/request change.
struct GraphPage078B {
    std::vector<std::string> order;
    std::set<std::string> selected,neighborhood;
    V page=V::makeObj(),frontier=V::makeArr(),links=V::makeArr(),boundary=V::makeArr(),next=V{},expand=V{};
    bool partial=false,emitNodes=true;
};
GraphPage078B graphPage078B(const V& nodes,const V& edges,const std::set<std::string>& roots,const V& args,
                          const std::string& scope,const std::string& source,u64 revision,int depth,int limit,int historyLimit){
    GraphPage078B out;const auto* directionValue=args.find("direction");
    const std::string direction=directionValue&&directionValue->t==V::Str?directionValue->str:"both";
    if((directionValue&&directionValue->t!=V::Str)||(direction!="both"&&direction!="outgoing"&&direction!="incoming"))throw std::runtime_error("context_graph direction must be both, outgoing or incoming");
    V selection=V::makeObj();if(const auto* keys=args.find("keys")){V sorted=V::makeArr();for(const auto& key:roots)sorted.arr.push_back(V::makeStr(key));selection.set("keys",std::move(sorted));}else selection.set("query",V::makeStr(graphText(args,"query")));
    selection.set("depth",V::makeNum(depth));selection.set("limit",V::makeNum(limit));selection.set("history_limit",V::makeNum(historyLimit));selection.set("direction",V::makeStr(direction));
    const auto request=iterationSha256(contextCanonical(selection).dump());
    std::map<std::string,std::set<std::string>> adjacent;
    size_t dangling=0;for(const auto& edge:edges.arr){const auto from=graphText(edge,"from"),to=graphText(edge,"to");if(!nodes.find(from.c_str())||!nodes.find(to.c_str())){++dangling;continue;}if(direction!="incoming")adjacent[from].insert(to);if(direction!="outgoing")adjacent[to].insert(from);}
    std::vector<std::string> all(roots.begin(),roots.end());out.neighborhood=roots;auto level=roots;
    for(int i=0;i<depth&&!level.empty();++i){std::set<std::string> next;for(const auto& key:level)for(const auto& neighbor:adjacent[key])if(!out.neighborhood.count(neighbor))next.insert(neighbor);for(const auto& key:next){out.neighborhood.insert(key);all.push_back(key);}level=std::move(next);}
    size_t offset=0,edgeOffset=0;
    if(const auto* cursor=args.find("cursor")){
        if(cursor->t!=V::Obj||cursor->obj.size()<5||cursor->obj.size()>6)throw std::runtime_error("Invalid context_graph cursor; restart discovery");
        const std::set<std::string> accepted{"scope","source_sha256","request_sha256","revision","offset","edge_offset"};std::set<std::string> fields;for(const auto& field:cursor->obj)if(!accepted.count(field.first)||!fields.insert(field.first).second)throw std::runtime_error("Unknown/duplicate context_graph cursor field");
        const auto* position=cursor->find("offset"),*observed=cursor->find("revision");
        if(graphText(*cursor,"scope")!=scope||graphText(*cursor,"source_sha256")!=source||graphText(*cursor,"request_sha256")!=request||!observed||observed->t!=V::Num||observed->num!=(double)revision)throw std::runtime_error("Stale context_graph cursor: source, revision or selection changed; restart discovery");
        if(!position||position->t!=V::Num||!std::isfinite(position->num)||position->num<0||position->num!=std::floor(position->num)||position->num>(double)all.size())throw std::runtime_error("Invalid context_graph cursor offset");
        offset=(size_t)position->num;
        if(const auto* edge=cursor->find("edge_offset")){if(edge->t!=V::Num||!std::isfinite(edge->num)||edge->num<0||edge->num!=std::floor(edge->num)||edge->num>(double)edges.arr.size())throw std::runtime_error("Invalid context_graph cursor edge offset");edgeOffset=(size_t)edge->num;}
    }
    const auto end=std::min(all.size(),offset+(size_t)limit);for(size_t i=offset;i<end;++i){out.order.push_back(all[i]);out.selected.insert(all[i]);}
    out.emitNodes=edgeOffset==0;
    out.page.set("offset",V::makeNum(offset));out.page.set("returned",V::makeNum(out.emitNodes?out.order.size():0));out.page.set("node_slice_size",V::makeNum(out.order.size()));out.page.set("matched_roots",V::makeNum(roots.size()));out.page.set("neighborhood_nodes",V::makeNum(all.size()));out.page.set("nodes_have_more",V::makeBool(end<all.size()));out.page.set("depth",V::makeNum(depth));out.page.set("direction",V::makeStr(direction));out.page.set("request_sha256",V::makeStr(request));
    out.page.set("dangling_edges_ignored",V::makeNum(dangling));
    selection.set("expected_revision",V::makeNum((double)revision));
    std::set<std::string> boundaryKeys,depthKeys;
    for(const auto& key:out.selected)for(const auto& neighbor:adjacent[key])if(!out.neighborhood.count(neighbor))depthKeys.insert(neighbor);
    // Node traversal is directional; reporting an incident edge is not. An
    // edge from a previous-page root must remain visible on a later leaf page.
    // Dense internal and boundary edges share one bounded continuation stream.
    size_t incidentCount=0,boundaryCount=0;for(const auto& edge:edges.arr){const auto from=graphText(edge,"from"),to=graphText(edge,"to");if(!nodes.find(from.c_str())||!nodes.find(to.c_str()))continue;const bool fromHere=out.selected.count(from),toHere=out.selected.count(to);if(!fromHere&&!toHere)continue;const bool crossing=fromHere!=toHere;if(crossing){++boundaryCount;boundaryKeys.insert(fromHere?to:from);}const auto index=incidentCount++;if(index<edgeOffset||index-edgeOffset>=128)continue;if(crossing)out.boundary.arr.push_back(edge);else out.links.arr.push_back(edge);}
    if(edgeOffset>incidentCount)throw std::runtime_error("Context graph cursor edge offset is outside its node slice");
    const auto returnedEdges=out.links.arr.size()+out.boundary.arr.size();const bool moreEdges=edgeOffset+returnedEdges<incidentCount;out.partial=moreEdges||end<all.size();
    out.page.set("edge_offset",V::makeNum(edgeOffset));out.page.set("returned_edges",V::makeNum(returnedEdges));out.page.set("incident_edges",V::makeNum(incidentCount));out.page.set("edges_have_more",V::makeBool(moreEdges));out.page.set("has_more",V::makeBool(out.partial));out.page.set("edge_scope",V::makeStr("all incident edges of the current node slice, independent of traversal direction; edge-only continuations omit duplicate node facts"));
    if(out.partial){V cursor=V::makeObj();cursor.set("scope",V::makeStr(scope));cursor.set("source_sha256",V::makeStr(source));cursor.set("revision",V::makeNum((double)revision));cursor.set("request_sha256",V::makeStr(request));cursor.set("offset",V::makeNum(moreEdges?offset:end));if(moreEdges)cursor.set("edge_offset",V::makeNum(edgeOffset+returnedEdges));out.next=selection;out.next.set("cursor",std::move(cursor));}
    for(const auto& key:boundaryKeys){if(out.frontier.arr.size()>=64)break;const auto* node=nodes.find(key.c_str());if(!node)continue;V item=V::makeObj();item.set("key",V::makeStr(key));item.set("kind",V::makeStr(graphText(*node,"kind")));item.set("reason",V::makeStr(out.neighborhood.count(key)?"another_page":depthKeys.count(key)?"depth_boundary":"outside_traversal_direction"));out.frontier.arr.push_back(std::move(item));}
    out.page.set("boundary_nodes",V::makeNum(boundaryKeys.size()));out.page.set("boundary_edges",V::makeNum(boundaryCount));out.page.set("frontier_truncated",V::makeBool(boundaryKeys.size()>out.frontier.arr.size()||boundaryCount>out.boundary.arr.size()));out.page.set("depth_boundary_nodes",V::makeNum(depthKeys.size()));
    // Expand from the entire current page, not the truncated frontier excerpt.
    // Its own continuation can therefore reach every omitted adjacent node.
    if(!depthKeys.empty()){V keys=V::makeArr();for(const auto& key:out.selected)keys.arr.push_back(V::makeStr(key));out.expand=V::makeObj();out.expand.set("keys",std::move(keys));out.expand.set("depth",V::makeNum(1));out.expand.set("limit",V::makeNum(64));out.expand.set("direction",V::makeStr(direction));out.expand.set("expected_revision",V::makeNum((double)revision));}
    return out;
}
