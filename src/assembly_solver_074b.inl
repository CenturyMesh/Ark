// Included inside assembly.cpp's private namespace after residual/rank helpers.
thread_local AssemblySolveMode074B assemblySolveMode074B=AssemblySolveMode074B::Incremental;
struct AssemblySolveWork074B {
    size_t components=0,reused=0,solved=0,warm=0,coldFallbacks=0,iterations=0,residualCalls=0,jacobianColumns=0;
    bool grouped=false;V rows=V::makeArr();
    V receipt()const{V out=V::makeObj();out.set("schema",V::makeStr("ark.assembly-solver.074b.v1"));out.set("mode",V::makeStr(grouped?"independent_components":"monolithic_reference_or_fallback"));out.set("components",V::makeNum(components));out.set("components_reused",V::makeNum(reused));out.set("components_solved",V::makeNum(solved));out.set("warm_starts",V::makeNum(warm));out.set("cold_fallbacks",V::makeNum(coldFallbacks));out.set("iterations",V::makeNum(iterations));out.set("residual_calls",V::makeNum(residualCalls));out.set("jacobian_columns",V::makeNum(jacobianColumns));out.set("groups",rows);out.set("warm_scope",V::makeStr("Changed-target warm starts only for certified affine distance components with one moving B endpoint, fixed A endpoints and zero local B origins; other changed problems start cold. Every reused result is checked against current residuals."));return out;}
};
struct AssemblySolveCache074B {
    struct Entry {std::string structural;std::vector<double> x,jacobian;int rows=0;size_t bytes=0;std::uint64_t used=0;};
    std::mutex mutex;std::map<std::string,Entry> entries;size_t bytes=0;std::uint64_t clock=0;
    static AssemblySolveCache074B& get(){static AssemblySolveCache074B value;return value;}
    static size_t target()noexcept{try{if(const char* value=std::getenv("ARK_ASSEMBLY_SOLVER_CACHE");value&&std::string(value)=="0")return 0;return size_t(std::min<std::uint64_t>(64ull*1024*1024,currentResourcePolicy071D().payloadLimitBytes/16));}catch(...){return 0;}}
    void trim(size_t limit){while(!entries.empty()&&(bytes>limit||entries.size()>256)){auto oldest=std::min_element(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});bytes-=oldest->second.bytes;entries.erase(oldest);}}
    static std::optional<Entry> find(const std::string& exact,const std::string& structural,bool allowWarm,bool& matched)noexcept{
        matched=false;try{auto& cache=get();std::lock_guard lock(cache.mutex);cache.trim(target());auto at=cache.entries.find(exact);if(at!=cache.entries.end()){at->second.used=++cache.clock;matched=true;return at->second;}if(allowWarm){auto latest=cache.entries.end();for(auto it=cache.entries.begin();it!=cache.entries.end();++it)if(it->second.structural==structural&&(latest==cache.entries.end()||it->second.used>latest->second.used))latest=it;if(latest!=cache.entries.end())return latest->second;}return {};}catch(...){return {};}
    }
    static void store(const std::string& key,Entry entry)noexcept{try{auto& cache=get();std::lock_guard lock(cache.mutex);const auto limit=target();entry.bytes=sizeof(Entry)+key.capacity()+entry.structural.capacity()+(entry.x.capacity()+entry.jacobian.capacity())*sizeof(double)+128;cache.trim(limit);if(!limit||entry.bytes>limit)return;if(const auto old=cache.entries.find(key);old!=cache.entries.end()){cache.bytes-=old->second.bytes;cache.entries.erase(old);}entry.used=++cache.clock;const auto bytes=entry.bytes;cache.entries.emplace(key,std::move(entry));cache.bytes+=bytes;cache.trim(limit);}catch(...){}}
};
using AssemblyEvaluate074B=std::function<std::vector<double>(const std::vector<double>&)>;
std::vector<double> componentJacobian074B(const std::vector<double>& x,const std::vector<int>& columns,int rows,const AssemblyEvaluate074B& evaluate,AssemblySolveWork074B& work){
    std::vector<double> j(size_t(rows)*columns.size());for(size_t c=0;c<columns.size();++c){++work.jacobianColumns;const int global=columns[c];const double h=global%6<3?1e-4:1e-6;auto plus=x,minus=x;plus[global]+=h;minus[global]-=h;const auto rp=evaluate(plus),rm=evaluate(minus);for(int r=0;r<rows;++r)j[size_t(r)*columns.size()+c]=(rp[r]-rm[r])/(2*h);}return j;
}
bool solveComponent074B(std::vector<double>& x,const std::vector<int>& columns,const AssemblyEvaluate074B& evaluate,AssemblySolveWork074B& work,std::string& error){
    const int count=int(columns.size());for(int phase=0;phase<2;++phase){const bool lockRotation=phase==0;double damping=1e-3;
        for(int iteration=0;iteration<100;++iteration){++work.iterations;const auto r=evaluate(x);const double cost=squared(r);if(!std::isfinite(cost)){error="Non-finite assembly mate residual";return false;}if(cost<1e-16)break;
            const int rows=int(r.size());const auto j=componentJacobian074B(x,columns,rows,evaluate,work);std::vector<double> h(size_t(count)*count),g(count);
            for(int row=0;row<rows;++row)for(int c=0;c<count;++c){const auto value=j[size_t(row)*count+c];g[c]+=value*r[row];for(int q=0;q<=c;++q)h[size_t(c)*count+q]+=value*j[size_t(row)*count+q];}
            for(int c=0;c<count;++c)for(int q=0;q<c;++q)h[size_t(q)*count+c]=h[size_t(c)*count+q];bool improved=false;
            for(int trial=0;trial<12;++trial){auto regularized=h;for(int c=0;c<count;++c)regularized[size_t(c)*count+c]+=damping*(1+h[size_t(c)*count+c]);std::vector<double> step,rhs=g;for(auto& v:rhs)v=-v;if(!solveLinear(regularized,rhs,step,count)){error="Assembly mate linear solve failed";return false;}
                if(lockRotation)for(int c=0;c<count;++c)if(columns[c]%6>=3)step[c]=0;auto candidate=x;for(int c=0;c<count;++c)candidate[columns[c]]+=step[c];const auto nextCost=squared(evaluate(candidate));if(std::isfinite(nextCost)&&nextCost<cost){x=std::move(candidate);damping=std::max(1e-12,damping*.25);improved=true;break;}damping=std::min(1e12,damping*10);}
            if(!improved)break;
        }
    }return true;
}

template<class Movers,class Pose>
bool solveGroups074B(const std::vector<PreparedMate>& mates,const Movers& moving,const std::unordered_map<u64,const AssemblyNode*>& byId,
                     const std::unordered_map<u64,u64>& representative,const std::unordered_set<u64>& anchoredClusters,
                     const std::unordered_map<u64,Mat4>& authored,const std::unordered_map<u64,Mat4>& relativeFrame,
                     const Pose& posed,std::vector<double>& x,AssemblySolveWork074B& work,int& rank,std::string& error){
    struct Group{std::vector<int> movers,columns,mates;std::vector<double> jacobian;int rows=0;};
    std::vector<int> parent(moving.size());std::unordered_map<u64,int> indices;for(size_t i=0;i<moving.size();++i){parent[i]=int(i);indices[moving[i].id]=int(i);}
    auto root=[&](int v){while(parent[v]!=v){parent[v]=parent[parent[v]];v=parent[v];}return v;};
    auto moverOf=[&](u64 id){if(auto at=representative.find(id);at!=representative.end())id=at->second;const auto at=indices.find(id);return at==indices.end()?-1:at->second;};
    for(const auto& mate:mates){int a=moverOf(mate.mate->a),b=moverOf(mate.mate->b);if(a>=0&&b>=0){a=root(a);b=root(b);parent[std::max(a,b)]=std::min(a,b);}}
    std::map<int,Group> grouped;for(size_t i=0;i<moving.size();++i){auto& group=grouped[root(int(i))];group.movers.push_back(int(i));for(int axis=0;axis<6;++axis)group.columns.push_back(int(i)*6+axis);}
    for(size_t i=0;i<mates.size();++i){const auto& mate=mates[i];int mover=moverOf(mate.mate->a);if(mover<0)mover=moverOf(mate.mate->b);if(mover>=0)grouped[root(mover)].mates.push_back(int(i));}
    work.grouped=true;work.components=grouped.size();
    auto matrix=[](const Mat4& value){V out=V::makeArr();for(double v:value.m)out.arr.push_back(V::makeNum(v));return out;};
    auto fixed=[&](u64 id){return byId.at(id)->fixed||(representative.count(id)&&anchoredClusters.count(representative.at(id)));};
    double largestColumn=0;
    for(auto& [groupId,group]:grouped){
        V structure=V::makeObj(),nodes=V::makeArr(),relations=V::makeArr(),targets=V::makeArr(),nodeIds=V::makeArr(),mateIds=V::makeArr();
        for(int index:group.movers){V node=V::makeObj();const auto id=moving[index].id;node.set("id",V::makeNum(double(id)));node.set("authored",matrix(authored.at(id)));nodes.arr.push_back(node);nodeIds.arr.push_back(V::makeNum(double(id)));}
        bool affineWarm=group.movers.size()==1&&!moving[group.movers.front()].group;
        for(int index:group.mates){const auto& prepared=mates[index];const auto& mate=*prepared.mate;V row=V::makeObj();row.set("id",V::makeNum(double(mate.id)));row.set("type",V::makeStr(mate.type));row.set("a",V::makeNum(double(mate.a)));row.set("b",V::makeNum(double(mate.b)));row.set("local_a",matrix(prepared.a));row.set("local_b",matrix(prepared.b));row.set("authored_a",matrix(authored.at(mate.a)));row.set("authored_b",matrix(authored.at(mate.b)));row.set("limited",V::makeBool(prepared.limited));row.set("min",V::makeNum(prepared.minimum));row.set("max",V::makeNum(prepared.maximum));
            if(auto r=relativeFrame.find(mate.a);r!=relativeFrame.end())row.set("relative_a",matrix(r->second));if(auto r=relativeFrame.find(mate.b);r!=relativeFrame.end())row.set("relative_b",matrix(r->second));relations.arr.push_back(std::move(row));targets.arr.push_back(V::makeNum(prepared.target));mateIds.arr.push_back(V::makeNum(double(mate.id)));
            affineWarm=affineWarm&&mate.type=="distance"&&fixed(mate.a)&&mate.b==moving[group.movers.front()].id&&prepared.b.m[12]==0&&prepared.b.m[13]==0&&prepared.b.m[14]==0;
        }
        structure.set("movers",nodes);structure.set("relations",relations);const std::string structural="assembly-component-074b:"+structure.dump();structure.set("targets",targets);const std::string key="assembly-component-074b:"+structure.dump();
        AssemblyEvaluate074B evaluate=[&](const std::vector<double>& value){++work.residualCalls;const auto world=posed(value);std::vector<double> values;for(int index:group.mates)residual(mates[index],world,values);return values;};
        auto accepted=[&](const std::vector<double>& value){const auto world=posed(value);for(int index:group.mates)if(!satisfiedResidual(measuredResidual(mates[index],world)))return false;return true;};
        const auto beforeIterations=work.iterations,beforeColumns=work.jacobianColumns;const auto original=x;bool exact=false;const auto cached=AssemblySolveCache074B::find(key,structural,affineWarm,exact);bool reused=false,warmed=false;
        if(cached&&cached->x.size()==group.columns.size()){
            auto seed=x;for(size_t c=0;c<group.columns.size();++c)seed[group.columns[c]]=cached->x[c];
            if(exact&&accepted(seed)){const auto values=evaluate(seed);if(cached->rows==int(values.size())&&cached->jacobian.size()==values.size()*group.columns.size()){x=std::move(seed);group.rows=cached->rows;group.jacobian=cached->jacobian;reused=true;++work.reused;}}
            else if(!exact&&affineWarm&&cached->x.size()==6&&cached->x[3]==0&&cached->x[4]==0&&cached->x[5]==0&&squared(evaluate(seed))<squared(evaluate(x))){x=std::move(seed);warmed=true;++work.warm;}
        }
        if(!reused){++work.solved;bool solved=solveComponent074B(x,group.columns,evaluate,work,error);if(warmed&&(!solved||!accepted(x))){++work.coldFallbacks;for(int column:group.columns)x[column]=original[column];error.clear();solved=solveComponent074B(x,group.columns,evaluate,work,error);}if(!solved)return false;
            const auto values=evaluate(x);group.rows=int(values.size());group.jacobian=componentJacobian074B(x,group.columns,group.rows,evaluate,work);
            if(accepted(x)){AssemblySolveCache074B::Entry stored;stored.structural=structural;stored.rows=group.rows;stored.jacobian=group.jacobian;for(int column:group.columns)stored.x.push_back(x[column]);AssemblySolveCache074B::store(key,std::move(stored));}
        }
        for(size_t column=0;column<group.columns.size();++column){double norm=0;for(int row=0;row<group.rows;++row){const double value=group.jacobian[size_t(row)*group.columns.size()+column];norm+=value*value;}largestColumn=std::max(largestColumn,std::sqrt(norm));}
        V evidence=V::makeObj();evidence.set("moving_nodes",nodeIds);evidence.set("mate_ids",mateIds);evidence.set("result",V::makeStr(reused?"exact_problem_reused":warmed?"affine_target_warm_start":"cold_component_solve"));evidence.set("iterations",V::makeNum(work.iterations-beforeIterations));evidence.set("jacobian_columns",V::makeNum(work.jacobianColumns-beforeColumns));evidence.set("current_residuals_passed",V::makeBool(accepted(x)));work.rows.arr.push_back(std::move(evidence));
    }
    // Block-diagonal rank uses the SAME global scale threshold as the original
    // matrix. Per-component scaling would change reported DOFs for mixed units.
    const double threshold=std::max(1e-9,largestColumn*1e-6);rank=0;for(const auto& [_,group]:grouped)rank+=matrixRank(group.jacobian,group.rows,int(group.columns.size()),threshold);return true;
}
