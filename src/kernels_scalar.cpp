// Portable reference kernels. The historical Win64 assembly is retained in asm/
// for profiling, but is not enabled until ABI and numerical parity are verified.
#include "ark/core.hpp"
namespace ark {
extern "C" void ark_mat4_mul_f32(f32* out,const f32* a,const f32* b){
    f32 r[16]{};
    for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)
        r[i*4+j]+=a[i*4+k]*b[k*4+j];
    std::memcpy(out,r,sizeof r);
}
extern "C" void ark_transform_points_f32(const f32* m,const f32* in,f32* out,u64 n){
    for(u64 i=0;i<n;++i){
        f32 x=in[i*3],y=in[i*3+1],z=in[i*3+2];
        for(int r=0;r<3;++r)out[i*3+r]=m[r*4]*x+m[r*4+1]*y+m[r*4+2]*z+m[r*4+3];
    }
}
extern "C" f64 ark_dot_batch_f64(const f64* a,const f64* b,u64 n){
    f64 sum=0; for(u64 i=0;i<n*3;++i)sum+=a[i]*b[i]; return sum;
}
}
