// ARK-Point :: io.cpp — mesh export (binary STL/OBJ), minimal PNG writer,
// and an offscreen software rasterizer used by agents to "see" the model.
#include "ark/io.hpp"
#include "ark/mesh_model.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <locale>

namespace ark {

// ---------------------------------------------------------------------------
// Export -----------------------------------------------------------------------
bool exportSTL(const char* path, const Mesh& m) {
    std::ofstream f(path, std::ios::binary);
    if(!f) return false;
    char hdr[80]{}; std::memcpy(hdr,"ARK-Point binary STL",20);
    f.write(hdr,80);
    u32 n=(u32)m.triCount();
    f.write((char*)&n,4);
    for(size_t t=0;t<m.idx.size();t+=3){
        u32 i=m.idx[t],j=m.idx[t+1],k=m.idx[t+2];
        Vec3 p0{m.pos[i*3],m.pos[i*3+1],m.pos[i*3+2]};
        Vec3 p1{m.pos[j*3],m.pos[j*3+1],m.pos[j*3+2]};
        Vec3 p2{m.pos[k*3],m.pos[k*3+1],m.pos[k*3+2]};
        Vec3 nn=(p1-p0).cross(p2-p0).normalized();
        float rec[12]={ (float)nn.x,(float)nn.y,(float)nn.z,
                        (float)p0.x,(float)p0.y,(float)p0.z,
                        (float)p1.x,(float)p1.y,(float)p1.z,
                        (float)p2.x,(float)p2.y,(float)p2.z };
        f.write((char*)rec,sizeof(rec));
        u16 attr=0; f.write((char*)&attr,2);
    }
    return true;
}

bool exportOBJ(const char* path, const Mesh& m) {
    std::ofstream f(path);
    if(!f) return false;
    f.imbue(std::locale::classic());f<<std::setprecision(9);
    f<<"# ARK-Point export; units millimetres\n";
    for(size_t i=0;i<m.vertCount();++i)
        f<<"v "<<m.pos[i*3]<<" "<<m.pos[i*3+1]<<" "<<m.pos[i*3+2]<<"\n";
    bool normals=m.nrm.size()==m.pos.size();
    if(normals)
        for(size_t i=0;i<m.vertCount();++i)
            f<<"vn "<<m.nrm[i*3]<<" "<<m.nrm[i*3+1]<<" "<<m.nrm[i*3+2]<<"\n";
    for(size_t t=0;t<m.idx.size();t+=3){
        u32 a=m.idx[t]+1,b=m.idx[t+1]+1,c=m.idx[t+2]+1;
        if(normals)f<<"f "<<a<<"//"<<a<<" "<<b<<"//"<<b<<" "<<c<<"//"<<c<<"\n";
        else f<<"f "<<a<<" "<<b<<" "<<c<<"\n";
    }
    return bool(f);
}

bool exportFBX(const char* path,const Mesh& mesh){
    if(mesh.pos.empty()||mesh.pos.size()%3||mesh.idx.empty()||mesh.idx.size()%3)return false;
    for(float x:mesh.pos)if(!std::isfinite(x))return false;
    for(u32 i:mesh.idx)if(i>=mesh.vertCount())return false;
    std::ofstream file(std::filesystem::u8path(path));if(!file)return false;
    file.imbue(std::locale::classic());file<<std::setprecision(9);
    file<<"; FBX 7.4.0 project file\n; ARK triangle mesh export, units millimetres\n"
        "FBXHeaderExtension: {\n FBXHeaderVersion: 1003\n FBXVersion: 7400\n Creator: \"ARK C++\"\n}\n"
        "GlobalSettings: {\n Version: 1000\n Properties70: {\n"
        " P: \"UpAxis\", \"int\", \"Integer\", \"\",1\n P: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n"
        " P: \"FrontAxis\", \"int\", \"Integer\", \"\",2\n P: \"FrontAxisSign\", \"int\", \"Integer\", \"\",1\n"
        " P: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n P: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n"
        " P: \"UnitScaleFactor\", \"double\", \"Number\", \"\",0.1\n }\n}\n"
        "Definitions: {\n Version: 100\n Count: 2\n ObjectType: \"Geometry\" { Count: 1 }\n ObjectType: \"Model\" { Count: 1 }\n}\n"
        "Objects: {\n Geometry: 1001, \"Geometry::ARK\", \"Mesh\" {\n GeometryVersion: 124\n Vertices: *"<<mesh.pos.size()<<" {\n a: ";
    for(size_t i=0;i<mesh.pos.size();++i)file<<(i?",":"")<<mesh.pos[i];
    file<<"\n }\n PolygonVertexIndex: *"<<mesh.idx.size()<<" {\n a: ";
    for(size_t i=0;i<mesh.idx.size();++i){long long n=mesh.idx[i];if(i%3==2)n=-n-1;file<<(i?",":"")<<n;}
    file<<"\n }\n";
    if(mesh.nrm.size()==mesh.pos.size()){
        file<<" LayerElementNormal: 0 {\n Version: 101\n Name: \"\"\n MappingInformationType: \"ByVertice\"\n ReferenceInformationType: \"Direct\"\n Normals: *"<<mesh.nrm.size()<<" {\n a: ";
        for(size_t i=0;i<mesh.nrm.size();++i)file<<(i?",":"")<<mesh.nrm[i];
        file<<"\n }\n }\n Layer: 0 { Version: 100\n LayerElement: { Type: \"LayerElementNormal\"\n TypedIndex: 0 }\n }\n";
    }
    file<<" }\n Model: 1002, \"Model::ARK\", \"Mesh\" {\n Version: 232\n Properties70: {\n"
        " P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0,0\n"
        " P: \"Lcl Rotation\", \"Lcl Rotation\", \"\", \"A\",0,0,0\n"
        " P: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",1,1,1\n }\n Shading: T\n Culling: \"CullingOff\"\n }\n}\n"
        "Connections: {\n C: \"OO\",1001,1002\n C: \"OO\",1002,0\n}\n";
    return bool(file);
}

namespace {
std::string lowerExt(const std::string& path) {
    auto dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? std::string() : path.substr(dot);
    for (char& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}
void requireMeshLimits(const Mesh& mesh) {
    if (!mesh.triCount()) throw std::runtime_error("Imported mesh has no triangles");
    if (mesh.vertCount() > kMeshModelMaxVertices || mesh.triCount() > kMeshModelMaxTriangles)
        throw std::runtime_error("Imported mesh exceeds 12288 vertices or 4096 triangles");
    for (float value : mesh.pos)
        if (!std::isfinite(value) || std::fabs(value) > 1.0e6f)
            throw std::runtime_error("Imported mesh coordinate is not finite or exceeds 1e6 mm");
}
Mesh meshFromTriangles(const std::vector<Vec3>& triangles) {
    if (triangles.empty() || triangles.size() % 3) throw std::runtime_error("Imported mesh has no triangles");
    Mesh mesh;
    mesh.pos.reserve(triangles.size() * 3);
    mesh.idx.reserve(triangles.size());
    for (size_t i = 0; i < triangles.size(); ++i) {
        mesh.addVertex(triangles[i]);
        mesh.idx.push_back((u32)i);
    }
    requireMeshLimits(mesh);
    mesh.computeNormals();
    return mesh;
}
Mesh readBinaryStl(std::istream& in, std::uintmax_t bytes) {
    char header[80];
    in.read(header, 80);
    u32 count = 0;
    in.read(reinterpret_cast<char*>(&count), 4);
    if (!in || bytes != 84ull + 50ull * count) throw std::runtime_error("STL binary length does not match its triangle count");
    if (count > kMeshModelMaxTriangles) throw std::runtime_error("Imported mesh exceeds 4096 triangles");
    std::vector<Vec3> triangles;
    triangles.reserve((size_t)count * 3);
    for (u32 i = 0; i < count; ++i) {
        float record[12];
        in.read(reinterpret_cast<char*>(record), sizeof(record));
        u16 attribute = 0;
        in.read(reinterpret_cast<char*>(&attribute), 2);
        if (!in) throw std::runtime_error("Truncated binary STL");
        for (int v = 0; v < 3; ++v)
            triangles.push_back({record[3 + v * 3], record[4 + v * 3], record[5 + v * 3]});
    }
    return meshFromTriangles(triangles);
}
Mesh readAsciiStl(const std::string& text) {
    std::vector<Vec3> triangles;
    std::istringstream in(text);
    std::string word;
    while (in >> word) {
        if (word != "vertex") continue;
        double x, y, z;
        if (!(in >> x >> y >> z)) throw std::runtime_error("ASCII STL vertex is incomplete");
        triangles.push_back({x, y, z});
    }
    return meshFromTriangles(triangles);
}
int objIndex(int authored, int count) {
    int index = authored > 0 ? authored - 1 : count + authored;
    if (index < 0 || index >= count) throw std::runtime_error("OBJ face index is out of range");
    return index;
}
Mesh readObj(const std::string& text) {
    std::vector<Vec3> vertices;
    Mesh mesh;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        std::string kind;
        row >> kind;
        if (kind == "v") {
            double x, y, z;
            if (!(row >> x >> y >> z)) throw std::runtime_error("OBJ vertex is incomplete");
            vertices.push_back({x, y, z});
        } else if (kind == "f") {
            std::vector<int> face;
            std::string token;
            while (row >> token) {
                auto slash = token.find('/');
                int authored = std::atoi(slash == std::string::npos ? token.c_str() : token.substr(0, slash).c_str());
                if (!authored) throw std::runtime_error("OBJ face index is invalid");
                face.push_back(objIndex(authored, (int)vertices.size()));
            }
            if (face.size() < 3) throw std::runtime_error("OBJ face needs at least three vertices");
            if (face.size() > 64) throw std::runtime_error("OBJ face exceeds 64 corners");
            for (size_t i = 1; i + 1 < face.size(); ++i) mesh.addTri3((u32)face[0], (u32)face[i], (u32)face[i + 1]);
        }
    }
    mesh.pos.reserve(vertices.size() * 3);
    for (const Vec3& vertex : vertices) mesh.addVertex(vertex);
    requireMeshLimits(mesh);
    mesh.computeNormals();
    return mesh;
}
}

Mesh importMeshPath(const std::string& path) {
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) throw std::runtime_error("Could not open mesh file");
    file.seekg(0, std::ios::end);
    const auto bytes = file.tellg();
    file.seekg(0);
    if (bytes < 0 || (std::uintmax_t)bytes > 32ull * 1024ull * 1024ull)
        throw std::runtime_error("Mesh file exceeds the 32 MiB import limit");
    const std::string ext = lowerExt(path);
    if (ext == ".stl") {
        std::string text((size_t)bytes, '\0');
        file.read(text.data(), bytes);
        if (bytes >= 84) {
            file.clear();
            file.seekg(0);
            u32 count = 0;
            file.seekg(80);
            file.read(reinterpret_cast<char*>(&count), 4);
            if (file && (std::uintmax_t)bytes == 84ull + 50ull * count) {
                file.clear();
                file.seekg(0);
                return readBinaryStl(file, (std::uintmax_t)bytes);
            }
        }
        return readAsciiStl(text);
    }
    if (ext == ".obj") {
        std::string text((size_t)bytes, '\0');
        file.read(text.data(), bytes);
        return readObj(text);
    }
    throw std::runtime_error("Mesh import supports .stl and .obj");
}

// ---------------------------------------------------------------------------
// PNG (deflate stored blocks — zero compression deps) ----------------------------

static void png_chunk(std::ostream& o,const char* type,const std::vector<u8>& data){
    auto be32=[](u32 v){ std::vector<u8> b{(u8)(v>>24),(u8)(v>>16),(u8)(v>>8),(u8)v}; return b; };
    static const auto crcTable=[]{std::array<u32,256> table{};for(u32 n=0;n<256;++n){u32 c=n;for(int k=0;k<8;++k)c=c&1?0xEDB88320u^(c>>1):c>>1;table[n]=c;}return table;}();
    auto crc32=[&](const u8* d,size_t n_){
        u32 c=0xFFFFFFFFu;
        for(size_t i=0;i<n_;++i) c=crcTable[(c^d[i])&0xFF]^(c>>8);
        return c^0xFFFFFFFFu;
    };
    std::vector<u8> lenb=be32((u32)data.size());
    o.write((char*)lenb.data(),4);
    o.write(type,4);
    o.write((char*)data.data(),(std::streamsize)data.size());
    std::vector<u8> crcIn; crcIn.insert(crcIn.end(),type,type+4);
    crcIn.insert(crcIn.end(),data.begin(),data.end());
    std::vector<u8> crcb=be32(crc32(crcIn.data(),crcIn.size()));
    o.write((char*)crcb.data(),4);
}

static bool encodePngStream(std::ostream& f,const Image& img){
    if(img.w<=0||img.h<=0||img.w>8192||img.h>8192||img.px.size()!=(size_t)img.w*img.h*3)return false;
    static const u8 sig[8]={137,80,78,71,13,10,26,10};
    f.write((char*)sig,8);

    std::vector<u8> ihdr;
    auto push32=[&](u32 v){ ihdr.push_back((u8)(v>>24));ihdr.push_back((u8)(v>>16));ihdr.push_back((u8)(v>>8));ihdr.push_back(v&255); };
    push32((u32)img.w); push32((u32)img.h);
    ihdr.push_back(8); ihdr.push_back(2); ihdr.push_back(0);ihdr.push_back(0);ihdr.push_back(0);
    png_chunk(f,"IHDR",ihdr);

    // raw scanlines with filter 0
    std::vector<u8> raw;
    raw.reserve((size_t)(img.w*3+1)*img.h);
    for(int y=0;y<img.h;++y){
        raw.push_back(0);
        raw.insert(raw.end(), img.px.begin()+(size_t)y*img.w*3,
                                 img.px.begin()+(size_t)(y+1)*img.w*3);
    }
    // zlib stream: 2-byte header, stored deflate blocks, adler32
    std::vector<u8> idat;
    idat.push_back(0x78); idat.push_back(0x01);
    size_t pos=0;
    while(pos<raw.size()){
        size_t blk=tmin<size_t>(65535, raw.size()-pos);
        bool last = pos+blk>=raw.size();
        idat.push_back(last?1:0);
        idat.push_back(blk&255); idat.push_back((blk>>8)&255);
        idat.push_back((u8)~(blk&255)); idat.push_back((u8)~((blk>>8)&255));
        idat.insert(idat.end(), raw.begin()+pos, raw.begin()+pos+blk);
        pos+=blk;
    }
    auto adler=[&](const u8* d,size_t n_){
        u32 a=1,b=0;
        for(size_t i=0;i<n_;++i){ a=(a+d[i])%65521; b=(b+a)%65521; }
        return (b<<16)|a;
    };
    u32 ad=adler(raw.data(),raw.size());
    idat.push_back((u8)(ad>>24));idat.push_back((u8)(ad>>16));idat.push_back((u8)(ad>>8));idat.push_back(ad&255);
    png_chunk(f,"IDAT",idat);
    png_chunk(f,"IEND",{});
    return bool(f);
}
std::vector<u8> encodePNG(const Image& img){std::ostringstream stream(std::ios::binary|std::ios::out);if(!encodePngStream(stream,img))throw std::runtime_error("Invalid PNG RGB image");const auto bytes=stream.str();return {bytes.begin(),bytes.end()};}
bool writePNG(const char* path,const Image& img){std::ofstream stream(std::filesystem::u8path(path),std::ios::binary);return stream&&encodePngStream(stream,img);}

// ---------------------------------------------------------------------------
// Software snapshot renderer ------------------------------------------------------
static Vec3 shadePixelNormal(Vec3 n, Vec3 viewDir) {
    n=n.normalized();
    Vec3 key=Vec3{-0.32,0.86,0.42}.normalized();
    Vec3 fill=Vec3{0.62,0.18,-0.28}.normalized();
    f64 d1=tmax(n.dot(key),0.0);
    f64 d2=tmax(n.dot(fill),0.0);
    f64 wrap=n.dot(key)*0.5+0.5;
    f64 rim=std::pow(1.0-tmax(-n.dot(viewDir),0.0),3.0);
    Vec3 clay{0.76,0.73,0.68};
    Vec3 c = clay*(0.62+0.34*d1) + Vec3{0.70,0.74,0.78}*(0.10+0.08*d2) + Vec3{0.98,0.96,0.92}*rim*0.06;
    c = c + Vec3{1.0,0.98,0.94}*std::pow(wrap,18.0)*0.05;
    return {clamp(c.x,0.,1.),clamp(c.y,0.,1.),clamp(c.z,0.,1.)};
}

Image renderSnapshot(const Mesh& m, int W, int H, const SnapCam& cam) {
    if(W<=0||H<=0||W>4096||H>4096)return {};
    Image img; img.w=W; img.h=H; img.px.assign((size_t)W*H*3,0);

    // vertical gradient sky
    for(int y=0;y<H;++y){
        f64 t=(f64)y/(f64)tmax(H-1,1);
        Vec3 top{0.93,0.94,0.96}, bot{0.86,0.88,0.91};
        Vec3 c=bot+(top-bot)*(1.0-t);
        for(int x=0;x<W;++x){
            u8* p=&img.px[(size_t)(y*W+x)*3];
            p[0]=(u8)(c.x*255); p[1]=(u8)(c.y*255); p[2]=(u8)(c.z*255);
        }
    }

    AABB bb=m.computeBBox();
    if(!bb.valid()) return img;
    Vec3 ctr=cam.useTarget?cam.target:bb.center();
    Vec3 eyeOff{ std::cos(cam.pitch)*std::sin(cam.yaw),
                 std::sin(cam.pitch),
                 std::cos(cam.pitch)*std::cos(cam.yaw) };
    Vec3 eye = ctr + eyeOff*cam.dist;
    Mat4 view=Mat4::lookAt(eye, ctr, Vec3{0,1,0});
    f64 far=std::max(1000.0, cam.dist*8.0);
    f64 near=0.05; if(near>=far*0.01) near=far*1.0e-4;
    Mat4 proj=Mat4::perspective(deg2rad(cam.fovDeg),(f64)W/H,near,far);
    Mat4 vp=proj*view;

    size_t nv=m.vertCount();
    std::vector<Vec3> wpos(nv), wposVp(nv);
    for(size_t i=0;i<nv;++i)
        wpos[i]={m.pos[i*3],m.pos[i*3+1],m.pos[i*3+2]};
    std::vector<f64> clipW(nv);
    for(size_t i=0;i<nv;++i){
        auto p=wpos[i];clipW[i]=vp.m[3]*p.x+vp.m[7]*p.y+vp.m[11]*p.z+vp.m[15];
        if(clipW[i]>K_EPS)wposVp[i]=vp.transformPoint(p)/clipW[i];
    }

    std::vector<f32> depth((size_t)W*H, 1e30f);
    for(size_t t=0;t<m.idx.size();t+=3){
        u32 ia=m.idx[t],ib=m.idx[t+1],ic=m.idx[t+2];
        if(clipW[ia]<=K_EPS||clipW[ib]<=K_EPS||clipW[ic]<=K_EPS)continue;
        Vec3 A=wposVp[ia],B=wposVp[ib],C=wposVp[ic];
        Vec3 wa=wpos[ia],wb=wpos[ib],wc=wpos[ic];
        Vec3 wn=(wb-wa).cross(wc-wa);
        if(wn.lensq()<1e-24) continue;
        Vec3 faceC=(wa+wb+wc)*(1.0/3.0);
        Vec3 eyeDir=faceC-eye;                       // away from eye
        if(wn.dot(eyeDir)>0) continue;               // backface

        auto toScreen=[&](Vec3 p)->Vec3{
            return { (p.x*0.5+0.5)*W, (1-(p.y*0.5+0.5))*H, p.z };
        };
        Vec3 s0=toScreen(A),s1=toScreen(B),s2=toScreen(C);
        f64 minx=tmax(tmin(s0.x,tmin(s1.x,s2.x)),0.0);
        f64 maxx=tmin(tmax(s0.x,tmax(s1.x,s2.x)),(f64)W-1);
        f64 miny=tmax(tmin(s0.y,tmin(s1.y,s2.y)),0.0);
        f64 maxy=tmin(tmax(s0.y,tmax(s1.y,s2.y)),(f64)H-1);
        if(minx>maxx||miny>maxy) continue;
        f64 den=(s1.y-s2.y)*(s0.x-s2.x)+(s2.x-s1.x)*(s0.y-s2.y);
        if(std::fabs(den)<1e-12) continue;
        for(int y=(int)miny;y<=(int)maxy;++y)
            for(int x=(int)minx;x<=(int)maxx;++x){
                f64 px=x+0.5,py=y+0.5;
                f64 l0=((s1.y-s2.y)*(px-s2.x)+(s2.x-s1.x)*(py-s2.y))/den;
                f64 l1=((s2.y-s0.y)*(px-s2.x)+(s0.x-s2.x)*(py-s2.y))/den;
                f64 l2=1-l0-l1;
                if(l0<0||l1<0||l2<0) continue;
                f64 z=l0*A.z+l1*B.z+l2*C.z;
                f32& dref=depth[(size_t)y*W+x];
                if(z<dref){
                    dref=(f32)z;
                    Vec3 wp=wa*l0+wb*l1+wc*l2;
                    Vec3 nrm=wn.normalized();
                    if(nrm.dot(eyeDir)>0) nrm=-nrm;
                    Vec3 vd=(eye-wp).normalized();
                    Vec3 c=shadePixelNormal(nrm,vd);
                    u8* q=&img.px[(size_t)(y*W+x)*3];
                    q[0]=(u8)(c.x*255);q[1]=(u8)(c.y*255);q[2]=(u8)(c.z*255);
                }
            }
    }
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){
        f32 z=depth[(size_t)y*W+x];
        if(z>1e20f) continue;
        bool edge=false;
        for(int oy=-1;oy<=1&&!edge;++oy)for(int ox=-1;ox<=1;++ox){
            if(!ox&&!oy) continue;
            int nx=x+ox,ny=y+oy;
            if(nx<0||ny<0||nx>=W||ny>=H){edge=true;break;}
            f32 nz=depth[(size_t)ny*W+nx];
            if(nz>1e20f||std::fabs(nz-z)>0.04f){edge=true;break;}
        }
        if(!edge) continue;
        u8* q=&img.px[(size_t)(y*W+x)*3];
        q[0]=(u8)(q[0]*0.62);q[1]=(u8)(q[1]*0.62);q[2]=(u8)(q[2]*0.62);
    }
    return img;
}

} // namespace ark
