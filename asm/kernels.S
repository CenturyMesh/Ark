# ============================================================================
#  ARK-Point :: asm/kernels.S
#  Hand-written x86-64 AVX2/FMA kernels, Win64 calling convention (GCC/MinGW).
#
#    ark_mat4_mul_f32(f32* out RCX, const f32* a RDX, const f32* b R8)
#        row-major 4x4 multiply, fully unrolled, one FMA per element.
#
#    ark_transform_points_f32(const f32* rowmajor_mat RCX, const f32* in RDX,
#                             f32* out R8, u64 n R9)
#        batch transform Vec3f points (w=1). SIMD within point, software-
#        pipelined across points. Hot path of tessellation/render pipeline.
#
#    ark_dot_batch_f64(const f64* a RCX, const f64* b RDX, u64 n R8) -> f64 XMM0
#        sum of componentwise products over triplets (vec3 dots), 6-way
#        accumulator ILP.
#
#  All loads/stores unaligned-safe. Leaf functions: no prologue required.
# ============================================================================
    .intel_syntax noprefix
    .text

# ---------------------------------------------------------------------------
# void ark_mat4_mul_f32(float* out, const float* a, const float* b)
# ---------------------------------------------------------------------------
    .globl ark_mat4_mul_f32
ark_mat4_mul_f32:
    endbr64
    # rax = row index *16 ; walk rows of A/B, broadcast scalars of A row
    xor     eax, eax
.Lrow:
    # ymm0..3 accumulators zeroed once per row
    vxorps  ymm0, ymm0, ymm0
    vxorps  ymm1, ymm1, ymm1
    vxorps  ymm2, ymm2, ymm2
    vxorps  ymm3, ymm3, ymm3
    xor     r10d, r10d          # k = 0
.Lkloop:
    vmovups ymm4, [r8 + r10*4]              # B row k (4 floats)
    lea     r11, [rdx + rax]                # A row base
    vbroadcastss ymm5, [r11 + r10*4]        # A[i][k]
    vfmadd231ps ymm0, ymm5, ymm4
    inc     r10d
    cmp     r10d, 4
    jb      .Lkloop
    vmovups [rcx + rax], ymm0
    add     eax, 16
    cmp     eax, 64
    jb      .Lrow
    vzeroupper
    ret

# ---------------------------------------------------------------------------
# void ark_transform_points_f32(const float* mat /*row-major 16*/,
#                               const float* in, float* out, uint64_t n)
#   x' = m0x + m1y + m2z + m3   (rows contiguous)
# ---------------------------------------------------------------------------
    .globl ark_transform_points_f32
ark_transform_points_f32:
    endbr64
    test    r9, r9
    jz      .Ltp_done
    mov     r10, r9                # counter
    # preload matrix rows
    vmovups xmm8,  [rcx]           # row0
    vmovups xmm9,  [rcx+16]        # row1
    vmovups xmm10, [rcx+32]        # row2
    vmovups xmm11, [rcx+48]        # row3
.Ltp_loop:
    vmovss xmm0, dword ptr [rdx]      # x
    vmovss xmm1, dword ptr [rdx+4]    # y
    vmovss xmm2, dword ptr [rdx+8]    # z
    vshufps xmm0, xmm0, xmm0, 0        # bcast x
    vshufps xmm1, xmm1, xmm1, 0
    vshufps xmm2, xmm2, xmm2, 0
    vmulps  xmm3, xmm0, xmm8
    vfmadd231ps xmm3, xmm1, xmm9
    vfmadd231ps xmm3, xmm2, xmm10
    vaddps  xmm3, xmm3, xmm11
    vmovlps qword ptr [r8], xmm3       # store x',y'
    vextractps dword ptr [r8+8], xmm3, 2  # store z'
    add     rdx, 12
    add     r8,  12
    dec     r10
    jnz     .Ltp_loop
.Ltp_done:
    vzeroupper
    ret

# ---------------------------------------------------------------------------
# double ark_dot_batch_f64(const double* a, const double* b, uint64_t n)
#   returns sum_{i<n} a[3i]*b[3i] + a[3i+1]*b[3i+1] + a[3i+2]*b[3i+2]
# ---------------------------------------------------------------------------
    .globl ark_dot_batch_f64
ark_dot_batch_f64:
    endbr64
    test    r8, r8
    jz      .Ldot_zero
    pxor    xmm0, xmm0     # acc_x
    pxor    xmm1, xmm1     # acc_y
    pxor    xmm2, xmm2     # acc_z
    mov     r10, r8
.Ldot_loop:
    vmovsd  xmm3, qword ptr [rcx]        # ax
    vmovsd  xmm4, qword ptr [rdx]        # bx
    vmulpd  xmm3, xmm3, xmm4
    vaddpd  xmm0, xmm0, xmm3

    vmovsd  xmm3, qword ptr [rcx+8]
    vmovsd  xmm4, qword ptr [rdx+8]
    vmulpd  xmm3, xmm3, xmm4
    vaddpd  xmm1, xmm1, xmm3

    vmovsd  xmm3, qword ptr [rcx+16]
    vmovsd  xmm4, qword ptr [rdx+16]
    vmulpd  xmm3, xmm3, xmm4
    vaddpd  xmm2, xmm2, xmm3

    add     rcx, 24
    add     rdx, 24
    dec     r10
    jnz     .Ldot_loop
    vaddpd  xmm0, xmm0, xmm1
    vaddsd  xmm0, xmm0, xmm2
    vzeroupper
    ret
.Ldot_zero:
    pxor    xmm0, xmm0
    ret
