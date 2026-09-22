// ARK-Point :: io.cpp — mesh export (binary STL/OBJ), minimal PNG writer,
// and an offscreen software rasterizer used by agents to "see" the model.
#include "ark/io.hpp"
#include <cstdio>
#include <fstream>
#include <cmath>

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
    f<<"# ARK-Point export\n";
    for(size_t i=0;i<m.vertCount();++i)
        f<<"v "<<m.pos[i*3]<<" "<<m.pos[i*3+1]<<" "<<m.pos[i*3+2]<<"\n";
    if(!m.nrm.empty())
        for(size_t i=0;i<m.vertCount();++i)
            f<<"vn "<<m.nrm[i*3]<<" "<<m.nrm[i*3+1]<<" "<<m.nrm[i*3+2]<<"\n";
    for(size_t t=0;t<m.idx.size();t+=3){
        u32 a=m.idx[t]+1,b=m.idx[t+1]+1,c=m.idx[t+2]+1;
        f<<"f "<<a<<"//"<<a<<" "<<b<<"//"<<b<<" "<<c<<"//"<<c<<"\n";
    }
    return true;
}

// ---------------------------------------------------------------------------
// PNG (deflate stored blocks — zero compression deps) ----------------------------

static void png_chunk(std::ostream& o,const char* type,const std::vector<u8>& data){
    auto be32=[](u32 v){ std::vector<u8> b{(u8)(v>>24),(u8)(v>>16),(u8)(v>>8),(u8)v}; return b; };
    static u32 crcTable[256]; static bool crcInit=false;
    if(!crcInit){
        for(u32 n=0;n<256;++n){ u32 c=n; for(int k=0;k<8;++k) c=c&1?0xEDB88320u^(c>>1):c>>1; crcTable[n]=c; }
        crcInit=true;
    }
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

bool writePNG(const char* path,const Image& img){
    std::ofstream f(path,std::ios::binary);
    if(!f) return false;
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
    return true;
}

// ---------------------------------------------------------------------------
// Software snapshot renderer ------------------------------------------------------
static Vec3 shadePixelNormal(Vec3 n, Vec3 viewDir) {
    // procedural "matcap": two area lights + fresnel rim
    n=n.normalized();
    Vec3 l1=Vec3{-0.5,0.85,0.4}.normalized();
    Vec3 l2=Vec3{ 0.7,-0.25,-0.55}.normalized();
    f64 d1=tmax(n.dot(l1),0.0);
    f64 d2=tmax(n.dot(l2),0.0);
    f64 rim=std::pow(1.0-tmax(-n.dot(viewDir),0.0),3.0);
    Vec3 base{0.62,0.68,0.78};
    Vec3 hi{1.05,1.02,0.95};
    Vec3 c = base*(0.28+0.75*d1) + base*(0.22*d2) + Vec3{0.9,0.5,0.35}*rim*0.35;
    c = c + hi*std::pow(d1,48.0)*0.9;
    return {clamp(c.x,0.,1.),clamp(c.y,0.,1.),clamp(c.z,0.,1.)};
}

Image renderSnapshot(const Mesh& m, int W, int H, const SnapCam& cam) {
    if(W<=0||H<=0||W>4096||H>4096)return {};
    Image img; img.w=W; img.h=H; img.px.assign((size_t)W*H*3,0);

    // vertical gradient sky
    for(int y=0;y<H;++y){
        f64 t=(f64)y/(f64)tmax(H-1,1);
        Vec3 top{0.078,0.086,0.106}, bot{0.043,0.047,0.062};
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
    Mat4 proj=Mat4::perspective(deg2rad(cam.fovDeg),(f64)W/H,0.05,1000);
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
    return img;
}

} // namespace ark
