#pragma once
#include "ark/core.hpp"
namespace ark {
bool packedTransformAvx2Available070();
const char* packedTransformCpuBackend070();
void packedTransformScalar070(const float* input,float* output,size_t vertices,const Mat4& matrix);
void packedTransformCpu070(const float* input,float* output,size_t vertices,const Mat4& matrix);
}
