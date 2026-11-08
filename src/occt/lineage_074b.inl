// Recover only currently bound descendants. Historical indices and stale
// handles are never treated as persistent face identities.
bool historicalReferenceGroup074(const Ref& ref){
    if(ref.status!="split")return false;const auto* members=ref.meta.find("merged_from");const auto* split=ref.meta.find("split_into");const auto* reason=ref.meta.find("reason");
    if(!members||!split||!reason||members->t!=V::Arr||split->t!=V::Arr||reason->t!=V::Str||reason->str.empty()||members->arr.empty()||members->arr.size()!=split->arr.size())return false;
    for(size_t i=0;i<members->arr.size();++i)if(members->arr[i].t!=V::Str||split->arr[i].t!=V::Str||members->arr[i].str!=split->arr[i].str)return false;return true;
}
void bindDescendantReferences074(std::vector<Ref>& refs){
    std::map<std::string,const Ref*> byName;for(const auto& ref:refs)byName.emplace(ref.name,&ref);
    for(auto& root:refs){if(root.status!="split"||root.name.find("@ark.")!=std::string::npos)continue;std::set<std::string> visited,active;std::vector<TopoDS_Shape> descendants;bool complete=true;std::vector<std::pair<const Ref*,bool>> pending{{&root,false}};
        while(!pending.empty()){const auto [current,leaving]=pending.back();pending.pop_back();const auto& ref=*current;if(leaving){active.erase(ref.name);visited.insert(ref.name);continue;}if(active.count(ref.name)){complete=false;continue;}if(visited.count(ref.name))continue;active.insert(ref.name);pending.push_back({current,true});if(refType(ref.kind)!=refType(root.kind)){complete=false;continue;}if(selectable(ref.status)&&!ref.shape.IsNull()){bool duplicate=false;for(const auto& old:descendants)duplicate|=old.IsSame(ref.shape);if(!duplicate)descendants.push_back(ref.shape);continue;}if(ref.status=="deleted")continue;if(ref.status!="split"){complete=false;continue;}const auto* links=ref.meta.find("split_into");if(!links||links->t!=V::Arr){complete=false;continue;}for(const auto& link:links->arr){if(link.t!=V::Str){complete=false;continue;}const auto found=byName.find(link.str);if(found!=byName.end())pending.push_back({found->second,false});else complete=false;}}
        if(complete)root.descendants=std::move(descendants);
    }
}
V descendantHandles074(const Ref& ref,const TopoDS_Shape& shape,const std::string& identity){
    V result=V::makeArr();std::set<int> seen;const auto type=refType(ref.kind);for(const auto& descendant:ref.descendants){const int index=indexOf(shape,descendant,type);if(index<0||!seen.insert(index).second)continue;result.arr.push_back(V::makeStr("@ark."+std::string(type==TopAbs_FACE?"face":"edge")+"."+identity+"."+std::to_string(index)));}return result;
}
