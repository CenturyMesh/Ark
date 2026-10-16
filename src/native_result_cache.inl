// Derived local results are reusable only for identical explicit native inputs
// and identical producer executable/DLL bytes. External I/O operations bypass.
static std::string nativeCacheDigest(const std::string& bytes){
#ifdef _WIN32
    // CNG uses the platform SHA-256 implementation, including CPU acceleration.
    // The portable digest remains the reference and fallback, with identical bytes.
    struct Provider {
        BCRYPT_ALG_HANDLE handle=nullptr;
        Provider(){if(BCryptOpenAlgorithmProvider(&handle,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)handle=nullptr;}
        ~Provider(){if(handle)BCryptCloseAlgorithmProvider(handle,0);}
    };
    static Provider provider;
    struct Hash {BCRYPT_HASH_HANDLE handle=nullptr;~Hash(){if(handle)BCryptDestroyHash(handle);}} hash;
    unsigned char digest[32]{};
    if(provider.handle&&bytes.size()<=ULONG_MAX&&BCryptCreateHash(provider.handle,&hash.handle,nullptr,0,nullptr,0,0)>=0&&
        BCryptHashData(hash.handle,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),(ULONG)bytes.size(),0)>=0&&
        BCryptFinishHash(hash.handle,digest,sizeof digest,0)>=0){
        constexpr char hex[]="0123456789abcdef";std::string out;out.reserve(64);
        for(auto byte:digest){out+=hex[byte>>4];out+=hex[byte&15];}return out;
    }
#endif
    return iterationSha256(bytes);
}
struct NativeResultCache {
    fs::path directory,entry;
    std::string key,producerIdentity;
    static constexpr size_t maxEntry=64u*1024u*1024u;
    static void canonicalRequest(V& value){
        if(value.t==V::Obj){
            std::sort(value.obj.begin(),value.obj.end(),[](const auto& a,const auto& b){return a.first<b.first;});
            for(size_t i=0;i<value.obj.size();++i){if(i&&value.obj[i-1].first==value.obj[i].first)throw std::runtime_error("Duplicate native request field");canonicalRequest(value.obj[i].second);}
        }else if(value.t==V::Arr)for(auto& item:value.arr)canonicalRequest(item);
    }
    static fs::path cacheDirectory(){
#ifdef _WIN32
        if(const wchar_t* custom=_wgetenv(L"ARK_NATIVE_CACHE_DIRECTORY");custom&&*custom)return custom;
        if(const wchar_t* local=_wgetenv(L"LOCALAPPDATA");local&&*local)return fs::path(local)/"ARK"/"cache"/"native-v1";
#endif
        return {};
    }
    static bool pure(const V& envelope){
        const auto* operation=envelope.find("operation"),*request=envelope.find("request");
        if(!operation||operation->t!=V::Str||!request||request->t!=V::Obj)return false;
        const auto* op=request->find("op");
        if(operation->str=="model_electronics"){
            const auto* kind=request->find("kind");
            return kind&&kind->t==V::Str&&(kind->str=="cable_route"||kind->str=="pcb");
        }
        if(operation->str=="model_solid"){
            if(!op||op->t!=V::Str)return false;
            static const std::set<std::string> allowed{"box","sphere","cylinder","cone","torus","extrude","union","cut","common","hole","fillet","chamfer","transform","linear_pattern","circular_pattern","mirror","shell","draft","split_plane","curve_pattern","hole_pattern","holes_orchestrator","vertex_pull","split_line","rib","gusset","mount_pad","boss","screw_support","nut_seat","lid_lip","lid_groove","split_surface","split_tool"};
            return allowed.count(op->str)!=0;
        }
        if(operation->str=="model"){
            if(!op||op->t!=V::Str)return false;
            static const std::set<std::string> allowed{"surface_nurbs","surface_fill","extrude_surface","revolve_surface","sweep","loft","thicken","offset_surface","sew","surface_extract_face","surface_trim_plane","surface_trim_wire","surface_untrim","surface_extend","surface_mutual_trim","surface_repair","helix","curve","curve_interpolate","curve_analytic","surface_spline","curve_project_surface","intersection_curve","bridge"};
            return allowed.count(op->str)!=0;
        }
        return false;
    }
    static std::string producer(const fs::path& worker){
        struct File {fs::file_time_type time;uintmax_t bytes;std::string digest,identity;std::shared_ptr<void> pin;};
        static std::mutex mutex;static std::map<fs::path,File> known;
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<fs::path> files{worker};
#ifdef _WIN32
        for(const auto& candidate:fs::directory_iterator(worker.parent_path()))if(candidate.is_regular_file()){
            auto ext=candidate.path().extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);
            if(ext==L".dll")files.push_back(candidate.path());
        }
#endif
        std::sort(files.begin(),files.end());std::string identity="ark-native-result-v1\n";
        for(const auto& file:files){const auto time=fs::last_write_time(file);const auto bytes=fs::file_size(file);auto found=known.find(file);
            std::string fileIdentity;std::shared_ptr<void> pin;
#ifdef _WIN32
            // Retain a read-only sharing handle alongside each measured digest.
            // Size/mtime alone cannot identify replaced producer bytes.
            HANDLE handle=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot pin native cache producer");
            pin=std::shared_ptr<void>(handle,[](void* value){CloseHandle(value);});BY_HANDLE_FILE_INFORMATION info{};
            if(!GetFileInformationByHandle(handle,&info))throw std::runtime_error("Cannot identify native cache producer");
            fileIdentity=std::to_string(info.dwVolumeSerialNumber)+":"+std::to_string(info.nFileIndexHigh)+":"+std::to_string(info.nFileIndexLow);
#endif
            if(found==known.end()||found->second.time!=time||found->second.bytes!=bytes||found->second.identity!=fileIdentity){
                if(known.size()>=256)known.clear();File data{time,bytes,nativeCacheDigest(read(file)),fileIdentity,std::move(pin)};found=known.insert_or_assign(file,std::move(data)).first;
            }
            identity+=utf8(file.filename())+":"+found->second.digest+"\n";
        }
        return nativeCacheDigest(identity);
    }
    NativeResultCache(const V& request,const fs::path& worker){
#ifndef _WIN32
        // Persistent reuse awaits custody of the actual ELF dependency closure.
        return;
#endif
        if(const char* enabled=std::getenv("ARK_NATIVE_RESULT_CACHE");enabled&&std::string(enabled)=="0")return;
        try{
            producerIdentity=producer(worker);
            if(!pure(request))return;
#ifdef _WIN32
            if(const wchar_t* custom=_wgetenv(L"ARK_NATIVE_CACHE_DIRECTORY");custom&&*custom)directory=custom;
            else if(const wchar_t* local=_wgetenv(L"LOCALAPPDATA");local&&*local)directory=fs::path(local)/"ARK"/"cache"/"native-v1";
#else
            if(const char* custom=std::getenv("ARK_NATIVE_CACHE_DIRECTORY");custom&&*custom)directory=fs::u8path(custom);
            else if(const char* cache=std::getenv("XDG_CACHE_HOME");cache&&*cache)directory=fs::u8path(cache)/"ark"/"native-v1";
            else if(const char* home=std::getenv("HOME");home&&*home)directory=fs::u8path(home)/".cache"/"ark"/"native-v1";
#endif
            if(directory.empty())return;
            V canonical=request;canonicalRequest(canonical);
            key=nativeCacheDigest(producerIdentity+"\n"+canonical.dump());entry=directory/(key+".arknative");
        }catch(...){entry.clear();}
    }
    static bool decode(const std::string& expectedKey,const std::string& bytes,V& value){
        try{
            constexpr char magic[]="ARK-NATIVE-CACHE1\n";constexpr size_t start=sizeof(magic)-1,prefix=start+130;
            if(expectedKey.size()!=64||bytes.size()>maxEntry||bytes.size()<prefix||bytes.compare(0,start,magic)||bytes.compare(start,64,expectedKey)||bytes[start+64]!='\n'||bytes[prefix-1]!='\n')return false;
            const std::string payload=bytes.substr(prefix);
            if(nativeCacheDigest(payload)!=bytes.substr(start+65,64))return false;
            V result;if(!arkjson::parse(payload,result)||result.t!=V::Obj||result.find("error"))return false;
            const auto* local=result.find("native_cache_producer_local");if(!local||local->t!=V::Bool||!local->b)return false;
            const auto* report=result.find("report"),*brep=result.find("brep_base64"),*topology=result.find("topology");
            if(!report||!report->find("brep_valid")||!report->find("brep_valid")->b||!brep||brep->t!=V::Str||!topology||!topology->find("brep_sha256"))return false;
            value=std::move(result);return true;
        }catch(...){return false;}
    }
    bool lookup(V& value)const{
        if(entry.empty())return false;
        try{return fs::is_regular_file(entry)&&fs::file_size(entry)<=maxEntry&&decode(key,read(entry),value);}catch(...){return false;}
    }
    V identified(V result)const{
        if(!entry.empty())result.set("native_cache_key",V::makeStr(key));
        return result;
    }
    void store(const V& result)const{
        const auto* local=result.find("native_cache_producer_local");
        if(entry.empty()||!local||local->t!=V::Bool||!local->b||result.find("error")||!result.find("brep_base64"))return;
        fs::path temporary;
        try{
            const auto payload=result.dump();if(payload.size()+148>maxEntry)return;
            fs::create_directories(directory);
            static std::atomic<unsigned long long> sequence{0};temporary=entry;temporary+="."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence++)+".tmp";
            {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);file<<"ARK-NATIVE-CACHE1\n"<<key<<'\n'<<nativeCacheDigest(payload)<<'\n';file.write(payload.data(),(std::streamsize)payload.size());if(!file)throw std::runtime_error("Native cache write failed");}
#ifdef _WIN32
            if(!MoveFileExW(temporary.c_str(),entry.c_str(),MOVEFILE_REPLACE_EXISTING))throw std::runtime_error("Native cache publication failed");
#else
            fs::rename(temporary,entry);
#endif
            // Bound only our own flat cache entries; never traverse or remove
            // caller documents, directories or unrelated cache contents.
            struct Item {fs::path path;fs::file_time_type time;uintmax_t bytes;};std::vector<Item> entries;uintmax_t total=0;
            for(const auto& file:fs::directory_iterator(directory))if(file.is_regular_file()&&!file.is_symlink()){
                const auto name=file.path().filename().string();if(name.size()!=74||name.substr(64)!=".arknative"||name.substr(0,64).find_first_not_of("0123456789abcdef")!=std::string::npos)continue;
                auto size=file.file_size();entries.push_back({file.path(),file.last_write_time(),size});total+=size;
            }
            std::sort(entries.begin(),entries.end(),[](const Item& a,const Item& b){return a.time<b.time;});size_t count=entries.size();
            for(const auto& item:entries){if(total<=256u*1024u*1024u&&count<=1024)break;std::error_code error;if(fs::remove(item.path,error)){total-=item.bytes;--count;}}
        }catch(...){if(!temporary.empty()){std::error_code error;fs::remove(temporary,error);}}
    }
};
