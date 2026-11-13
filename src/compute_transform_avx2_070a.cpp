// Compiled separately for AVX2; called only after CPUID + OS YMM checks.
// Explicit multiply/add preserves scalar evaluation order, without FMA.
#include "compute_transform_070a.hpp"
#include <immintrin.h>
namespace ark {
void packedTransformAvx2070(const float* input,float* output,size_t vertices,const Mat4& matrix){
    __m256d coefficients[3][4];for(int r=0;r<3;++r)for(int c=0;c<4;++c)coefficients[r][c]=_mm256_set1_pd(matrix.m[4*c+r]);
    size_t i=0;for(;i+4<=vertices;i+=4){
        const float* p=input+3*i;
        const auto x=_mm256_cvtps_pd(_mm_setr_ps(p[0],p[3],p[6],p[9]));
        const auto y=_mm256_cvtps_pd(_mm_setr_ps(p[1],p[4],p[7],p[10]));
        const auto z=_mm256_cvtps_pd(_mm_setr_ps(p[2],p[5],p[8],p[11]));
        for(int r=0;r<3;++r){
            auto value=_mm256_add_pd(_mm256_mul_pd(coefficients[r][0],x),_mm256_mul_pd(coefficients[r][1],y));
            value=_mm256_add_pd(value,_mm256_mul_pd(coefficients[r][2],z));value=_mm256_add_pd(value,coefficients[r][3]);
            float packed[4];_mm_storeu_ps(packed,_mm256_cvtpd_ps(value));
            for(int j=0;j<4;++j)output[3*(i+j)+r]=packed[j];
        }
    }
    _mm256_zeroupper();if(i<vertices)packedTransformScalar070(input+3*i,output+3*i,vertices-i,matrix);
}
}
