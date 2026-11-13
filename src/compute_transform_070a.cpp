#include "compute_transform_070a.hpp"
#include <cstdlib>
#if defined(ARK_PACKED_AVX2) && defined(_MSC_VER)
#include <intrin.h>
#endif
namespace ark {
#ifdef ARK_PACKED_AVX2
void packedTransformAvx2070(const float*,float*,size_t,const Mat4&);
#endif
bool packedTransformAvx2Available070(){
    static const bool available=[] {
#if defined(ARK_PACKED_AVX2) && defined(_MSC_VER) && defined(_M_X64)
        int registers[4]{};__cpuid(registers,0);if(registers[0]<7)return false;
        __cpuidex(registers,1,0);
        if(!(registers[2]&(1<<27))||!(registers[2]&(1<<28)))return false;
        if((_xgetbv(0)&6)!=6)return false; // OS saves both XMM and YMM state.
        __cpuidex(registers,7,0);return (registers[1]&(1<<5))!=0;
#else
        return false;
#endif
    }();return available;
}
const char* packedTransformCpuBackend070(){
    const auto* disabled=std::getenv("ARK_DISABLE_SIMD");
    return packedTransformAvx2Available070()&&!(disabled&&*disabled)?"AVX2 f64 / no FMA":"scalar f64";
}
void packedTransformScalar070(const float* input,float* output,size_t vertices,const Mat4& matrix){
    for(size_t i=0;i<vertices;++i){const double x=input[3*i],y=input[3*i+1],z=input[3*i+2];
        for(int r=0;r<3;++r)output[3*i+r]=(float)(matrix.m[r]*x+matrix.m[4+r]*y+matrix.m[8+r]*z+matrix.m[12+r]);}
}
void packedTransformCpu070(const float* input,float* output,size_t vertices,const Mat4& matrix){
#ifdef ARK_PACKED_AVX2
    if(packedTransformCpuBackend070()[0]=='A'){packedTransformAvx2070(input,output,vertices,matrix);return;}
#endif
    packedTransformScalar070(input,output,vertices,matrix);
}
}
