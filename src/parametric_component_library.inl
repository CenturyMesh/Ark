V parametricComponentLibrary(const V& args){
    namespace fs=std::filesystem;const auto root=fs::absolute(fs::u8path(text(args,"directory"))).lexically_normal();
    if(!fs::is_directory(root))fail("LIBRARY_DIRECTORY","Choose an existing local component folder");
    auto lower=[](std::string value){for(auto& c:value)c=(char)std::tolower((unsigned char)c);return value;};
    const auto query=lower(get(args,"query").asStr()),category=get(args,"category").asStr(),kind=get(args,"kind").asStr();
    if(!kind.empty()&&kind!="part"&&kind!="assembly")fail("LIBRARY_KIND","Choose part or assembly");
    V out=V::makeObj(),rows=V::makeArr(),errors=V::makeArr();std::vector<fs::path> paths;
    size_t examined=0;bool truncated=false;
    fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied),end;
    for(;it!=end;++it){if(++examined>4096){truncated=true;break;}if(it.depth()>=4&&it->is_directory())it.disable_recursion_pending();if(it->is_symlink()){if(it->is_directory())it.disable_recursion_pending();continue;}if(!it->is_regular_file())continue;const auto ext=it->path().extension().string();if(ext==".arkprt"||ext==".arktop")paths.push_back(it->path());}
    std::sort(paths.begin(),paths.end());
    for(const auto& path:paths){if(rows.arr.size()>=256){truncated=true;break;}const auto utf=path.u8string();const std::string file(utf.begin(),utf.end());try{const auto package=loadParametricComponent(file);const auto relative=fs::relative(path.parent_path(),root).generic_u8string();const std::string group(relative.begin(),relative.end());if(!kind.empty()&&get(package,"kind").asStr()!=kind)continue;if(!category.empty()&&category!=group)continue;
            std::string haystack=get(package,"name").asStr()+" "+group+" "+path.filename().string();for(const auto& parameter:get(package,"parameters").arr)haystack+=" "+parameter.str;
            if(!query.empty()&&lower(haystack).find(query)==std::string::npos)continue;
            V row=brief(package);row.set("path",V::makeStr(file));row.set("category",V::makeStr(group));V values=V::makeObj();for(const auto& name:get(package,"parameters").arr)for(const auto& parameter:get(get(package,"document"),"params").arr)if(get(parameter,"name").str==name.str)values.set(name.str,get(parameter,"expr"));row.set("parameter_expressions",values);rows.arr.push_back(row);
        }catch(const std::exception& e){V rejection=V::makeObj();rejection.set("path",V::makeStr(file));rejection.set("error",V::makeStr(e.what()));errors.arr.push_back(rejection);}}
    out.set("schema",V::makeStr("ark.component-library.v1"));out.set("components",rows);out.set("rejected",errors);out.set("truncated",V::makeBool(truncated));out.set("authority",V::makeStr("authored_component_packages"));out.set("automatic_updates",V::makeBool(false));return out;
}
V parametricComponentThumbnail(const std::string& path){
    const auto package=loadParametricComponent(path);Document d=parse(get(package,"document"));d.recompute();for(const auto& result:d.lastResults)if(!result.ok)fail("THUMBNAIL_GEOMETRY",result.error);
    std::vector<AssemblyPlacement> placements;std::string error;if(!resolveAssembly(d,placements,error))fail("THUMBNAIL_ASSEMBLY",error);
    std::vector<std::array<double,4>> segments;double xmin=1e300,ymin=1e300,xmax=-1e300,ymax=-1e300;bool sampled=false;
    for(const auto& placement:placements){if(!placement.visible)continue;for(const auto& body:d.evalBodies)if(body.id==placement.feature){const auto& mesh=body.tessellate(d.tessOpt);const size_t stride=std::max<size_t>(1,(mesh.triCount()+999)/1000);sampled|=stride>1;
        auto point=[&](u32 index){if(index>=mesh.vertCount())fail("THUMBNAIL_MESH","Invalid derived display index");const Vec3 p=placement.world.transformPoint({mesh.pos[index*3],mesh.pos[index*3+1],mesh.pos[index*3+2]});return std::array<double,2>{(p.x-p.y)*.7071067811865476,(p.x+p.y)*.408248290463863-p.z*.816496580927726};};
        for(size_t t=0;t<mesh.triCount();t+=stride){for(int edge=0;edge<3;++edge){const auto a=point(mesh.idx[t*3+edge]),b=point(mesh.idx[t*3+(edge+1)%3]);segments.push_back({a[0],a[1],b[0],b[1]});xmin=std::min({xmin,a[0],b[0]});xmax=std::max({xmax,a[0],b[0]});ymin=std::min({ymin,a[1],b[1]});ymax=std::max({ymax,a[1],b[1]});if(segments.size()>24000)fail("THUMBNAIL_LIMIT","Too many component display segments");}}
    }}
    if(segments.empty())fail("THUMBNAIL_EMPTY","Component has no visible evaluated geometry");
    const double scale=220./std::max({xmax-xmin,ymax-ymin,1e-9});V lines=V::makeArr();for(const auto& segment:segments){V line=V::makeArr();for(int i=0;i<4;++i)line.arr.push_back(V::makeNum(128+(segment[i]-(i%2?(ymin+ymax)/2:(xmin+xmax)/2))*scale));lines.arr.push_back(line);}
    V out=V::makeObj();out.set("schema",V::makeStr("ark.component-thumbnail.v1"));out.set("size",V::makeNum(256));out.set("lines",lines);out.set("sampled",V::makeBool(sampled));out.set("package_sha256",V::makeStr(hash(package)));out.set("authority",V::makeStr("derived_display_only"));return out;
}
