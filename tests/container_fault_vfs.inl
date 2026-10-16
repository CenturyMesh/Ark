// Faults are injected beneath SQLite in this test executable only. The runtime
// has no environment variable or production switch that weakens durability.
namespace faultVfs {
sqlite3_vfs* parent=nullptr;
int writes=0,failAt=0;
struct File {sqlite3_file base;sqlite3_file* real;};
File* file(sqlite3_file* p){return reinterpret_cast<File*>(p);}
int close(sqlite3_file* p){auto f=file(p);int result=f->real->pMethods->xClose(f->real);sqlite3_free(f->real);f->real=nullptr;return result;}
int read(sqlite3_file* p,void* b,int n,sqlite3_int64 o){auto f=file(p);return f->real->pMethods->xRead(f->real,b,n,o);}
int write(sqlite3_file* p,const void* b,int n,sqlite3_int64 o){if(++writes==failAt)return SQLITE_FULL;auto f=file(p);return f->real->pMethods->xWrite(f->real,b,n,o);}
int truncate(sqlite3_file* p,sqlite3_int64 n){auto f=file(p);return f->real->pMethods->xTruncate(f->real,n);}
int sync(sqlite3_file* p,int n){auto f=file(p);return f->real->pMethods->xSync(f->real,n);}
int size(sqlite3_file* p,sqlite3_int64* n){auto f=file(p);return f->real->pMethods->xFileSize(f->real,n);}
int lock(sqlite3_file* p,int n){auto f=file(p);return f->real->pMethods->xLock(f->real,n);}
int unlock(sqlite3_file* p,int n){auto f=file(p);return f->real->pMethods->xUnlock(f->real,n);}
int reserved(sqlite3_file* p,int* n){auto f=file(p);return f->real->pMethods->xCheckReservedLock(f->real,n);}
int control(sqlite3_file* p,int n,void* v){auto f=file(p);return f->real->pMethods->xFileControl(f->real,n,v);}
int sector(sqlite3_file* p){auto f=file(p);return f->real->pMethods->xSectorSize(f->real);}
int characteristics(sqlite3_file* p){auto f=file(p);return f->real->pMethods->xDeviceCharacteristics(f->real);}
sqlite3_io_methods methods{1,close,read,write,truncate,sync,size,lock,unlock,reserved,control,sector,characteristics};
int open(sqlite3_vfs*,const char* name,sqlite3_file* out,int flags,int* resultFlags){auto f=file(out);f->real=static_cast<sqlite3_file*>(sqlite3_malloc64(parent->szOsFile));if(!f->real)return SQLITE_NOMEM;std::memset(f->real,0,parent->szOsFile);int result=parent->xOpen(parent,name,f->real,flags,resultFlags);if(result!=SQLITE_OK){sqlite3_free(f->real);f->real=nullptr;return result;}f->base.pMethods=&methods;return SQLITE_OK;}
struct Scope {
    sqlite3_vfs vfs{};
    explicit Scope(int offset){parent=sqlite3_vfs_find(nullptr);vfs=*parent;vfs.zName="ark-test-disk-full";vfs.szOsFile=sizeof(File);vfs.xOpen=open;writes=0;failAt=offset;check(sqlite3_vfs_register(&vfs,1)==SQLITE_OK,"fault VFS registers");}
    ~Scope(){sqlite3_vfs_register(parent,1);sqlite3_vfs_unregister(&vfs);failAt=0;}
};
}
void diskFullTests(){
    Workspace workspace;auto baseline=workspace.path/"baseline.ark";V initial=fixture(128*1024),changed=fixture(256*1024),loaded;std::string error;check(ark::saveDocumentContainer(initial,utf8(baseline).c_str(),&error),error);int total=0;
    {auto sample=workspace.path/"measure.ark";fs::copy_file(baseline,sample);faultVfs::Scope fault(0);check(ark::saveDocumentContainer(changed,utf8(sample).c_str(),&error),error);total=faultVfs::writes;}
    check(total>10,"disk-full fixture exercises journal and database writes");
    for(int offset=1;offset<=total;++offset){auto target=workspace.path/("full-"+std::to_string(offset)+".ark");fs::copy_file(baseline,target);bool saved;{faultVfs::Scope fault(offset);saved=ark::saveDocumentContainer(changed,utf8(target).c_str(),&error);}check(!saved,"disk-full rejects save at write "+std::to_string(offset));check(ark::loadDocumentContainer(utf8(target).c_str(),loaded,&error)&&canonical(loaded)==canonical(initial),"disk-full preserves previous logical revision at write "+std::to_string(offset)+": "+error);}
    auto migration=workspace.path/"legacy.ark";auto intact=initial.dump();write(migration,intact);{faultVfs::Scope fault(1);check(!ark::saveDocumentContainer(changed,utf8(migration).c_str(),&error),"disk-full initial stage rejects");}check(bytes(migration)==intact,"disk-full migration leaves original JSON bytes intact");
    std::cout<<"disk-full injection: "<<total<<" successive SQLite write positions preserved previous revision\n";
}
