; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s -mtriple=i686-unknown-unknown -mattr=+avx | FileCheck %s --check-prefix=X32
; RUN: llc < %s -mtriple=x86_64-unknown-unknown -mattr=+avx | FileCheck %s --check-prefix=X64

define <2 x double> @signbits_sext_v2i64_sitofp_v2f64(i32 %a0, i32 %a1) nounwind {
; X32-LABEL: signbits_sext_v2i64_sitofp_v2f64:
; X32:       # %bb.0:
; X32-NEXT:    vcvtdq2pd {{[0-9]+}}(%esp), %xmm0
; X32-NEXT:    retl
;
; X64-LABEL: signbits_sext_v2i64_sitofp_v2f64:
; X64:       # %bb.0:
; X64-NEXT:    vmovd %edi, %xmm0
; X64-NEXT:    vpinsrd $1, %esi, %xmm0, %xmm0
; X64-NEXT:    vcvtdq2pd %xmm0, %xmm0
; X64-NEXT:    retq
  %1 = sext i32 %a0 to i64
  %2 = sext i32 %a1 to i64
  %3 = insertelement <2 x i64> undef, i64 %1, i32 0
  %4 = insertelement <2 x i64> %3, i64 %2, i32 1
  %5 = sitofp <2 x i64> %4 to <2 x double>
  ret <2 x double> %5
}

define <4 x float> @signbits_sext_v4i64_sitofp_v4f32(i8 signext %a0, i16 signext %a1, i32 %a2, i32 %a3) nounwind {
; X32-LABEL: signbits_sext_v4i64_sitofp_v4f32:
; X32:       # %bb.0:
; X32-NEXT:    movswl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    movsbl {{[0-9]+}}(%esp), %ecx
; X32-NEXT:    vmovd %ecx, %xmm0
; X32-NEXT:    vpinsrd $1, %eax, %xmm0, %xmm0
; X32-NEXT:    vpinsrd $2, {{[0-9]+}}(%esp), %xmm0, %xmm0
; X32-NEXT:    vpinsrd $3, {{[0-9]+}}(%esp), %xmm0, %xmm0
; X32-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X32-NEXT:    retl
;
; X64-LABEL: signbits_sext_v4i64_sitofp_v4f32:
; X64:       # %bb.0:
; X64-NEXT:    vmovd %edi, %xmm0
; X64-NEXT:    vpinsrd $1, %esi, %xmm0, %xmm0
; X64-NEXT:    vpinsrd $2, %edx, %xmm0, %xmm0
; X64-NEXT:    vpinsrd $3, %ecx, %xmm0, %xmm0
; X64-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X64-NEXT:    retq
  %1 = sext i8 %a0 to i64
  %2 = sext i16 %a1 to i64
  %3 = sext i32 %a2 to i64
  %4 = sext i32 %a3 to i64
  %5 = insertelement <4 x i64> undef, i64 %1, i32 0
  %6 = insertelement <4 x i64> %5, i64 %2, i32 1
  %7 = insertelement <4 x i64> %6, i64 %3, i32 2
  %8 = insertelement <4 x i64> %7, i64 %4, i32 3
  %9 = sitofp <4 x i64> %8 to <4 x float>
  ret <4 x float> %9
}

define float @signbits_ashr_extract_sitofp_0(<2 x i64> %a0) nounwind {
; X32-LABEL: signbits_ashr_extract_sitofp_0:
; X32:       # %bb.0:
; X32-NEXT:    pushl %eax
; X32-NEXT:    vpermilps {{.*#+}} xmm0 = xmm0[1,1,2,3]
; X32-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X32-NEXT:    vmovss %xmm0, (%esp)
; X32-NEXT:    flds (%esp)
; X32-NEXT:    popl %eax
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_extract_sitofp_0:
; X64:       # %bb.0:
; X64-NEXT:    vmovq %xmm0, %rax
; X64-NEXT:    shrq $32, %rax
; X64-NEXT:    vcvtsi2ss %eax, %xmm1, %xmm0
; X64-NEXT:    retq
  %1 = ashr <2 x i64> %a0, <i64 32, i64 32>
  %2 = extractelement <2 x i64> %1, i32 0
  %3 = sitofp i64 %2 to float
  ret float %3
}

define float @signbits_ashr_extract_sitofp_1(<2 x i64> %a0) nounwind {
; X32-LABEL: signbits_ashr_extract_sitofp_1:
; X32:       # %bb.0:
; X32-NEXT:    pushl %eax
; X32-NEXT:    vpermilps {{.*#+}} xmm0 = xmm0[1,1,2,3]
; X32-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X32-NEXT:    vmovss %xmm0, (%esp)
; X32-NEXT:    flds (%esp)
; X32-NEXT:    popl %eax
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_extract_sitofp_1:
; X64:       # %bb.0:
; X64-NEXT:    vmovq %xmm0, %rax
; X64-NEXT:    shrq $32, %rax
; X64-NEXT:    vcvtsi2ss %eax, %xmm1, %xmm0
; X64-NEXT:    retq
  %1 = ashr <2 x i64> %a0, <i64 32, i64 63>
  %2 = extractelement <2 x i64> %1, i32 0
  %3 = sitofp i64 %2 to float
  ret float %3
}

define float @signbits_ashr_shl_extract_sitofp(<2 x i64> %a0) nounwind {
; X32-LABEL: signbits_ashr_shl_extract_sitofp:
; X32:       # %bb.0:
; X32-NEXT:    pushl %eax
; X32-NEXT:    vpsrad $29, %xmm0, %xmm0
; X32-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[1,1,3,3]
; X32-NEXT:    vpsllq $20, %xmm0, %xmm0
; X32-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X32-NEXT:    vmovss %xmm0, (%esp)
; X32-NEXT:    flds (%esp)
; X32-NEXT:    popl %eax
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_shl_extract_sitofp:
; X64:       # %bb.0:
; X64-NEXT:    vmovq %xmm0, %rax
; X64-NEXT:    sarq $61, %rax
; X64-NEXT:    shll $20, %eax
; X64-NEXT:    vcvtsi2ss %eax, %xmm1, %xmm0
; X64-NEXT:    retq
  %1 = ashr <2 x i64> %a0, <i64 61, i64 60>
  %2 = shl <2 x i64> %1, <i64 20, i64 16>
  %3 = extractelement <2 x i64> %2, i32 0
  %4 = sitofp i64 %3 to float
  ret float %4
}

define float @signbits_ashr_insert_ashr_extract_sitofp(i64 %a0, i64 %a1) nounwind {
; X32-LABEL: signbits_ashr_insert_ashr_extract_sitofp:
; X32:       # %bb.0:
; X32-NEXT:    pushl %eax
; X32-NEXT:    movl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    movl %eax, %ecx
; X32-NEXT:    sarl $30, %ecx
; X32-NEXT:    shll $2, %eax
; X32-NEXT:    vmovd %eax, %xmm0
; X32-NEXT:    vpinsrd $1, %ecx, %xmm0, %xmm0
; X32-NEXT:    vpsrlq $3, %xmm0, %xmm0
; X32-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X32-NEXT:    vmovss %xmm0, (%esp)
; X32-NEXT:    flds (%esp)
; X32-NEXT:    popl %eax
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_insert_ashr_extract_sitofp:
; X64:       # %bb.0:
; X64-NEXT:    sarq $30, %rdi
; X64-NEXT:    shrq $3, %rdi
; X64-NEXT:    vcvtsi2ss %edi, %xmm0, %xmm0
; X64-NEXT:    retq
  %1 = ashr i64 %a0, 30
  %2 = insertelement <2 x i64> undef, i64 %1, i32 0
  %3 = insertelement <2 x i64> %2, i64 %a1, i32 1
  %4 = ashr <2 x i64> %3, <i64 3, i64 3>
  %5 = extractelement <2 x i64> %4, i32 0
  %6 = sitofp i64 %5 to float
  ret float %6
}

define <4 x double> @signbits_sext_shuffle_sitofp(<4 x i32> %a0, <4 x i64> %a1) nounwind {
; X32-LABEL: signbits_sext_shuffle_sitofp:
; X32:       # %bb.0:
; X32-NEXT:    vpmovsxdq %xmm0, %xmm1
; X32-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; X32-NEXT:    vpmovsxdq %xmm0, %xmm0
; X32-NEXT:    vinsertf128 $1, %xmm0, %ymm1, %ymm0
; X32-NEXT:    vpermilpd {{.*#+}} ymm0 = ymm0[1,0,3,2]
; X32-NEXT:    vperm2f128 {{.*#+}} ymm0 = ymm0[2,3,0,1]
; X32-NEXT:    vextractf128 $1, %ymm0, %xmm1
; X32-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[0,2]
; X32-NEXT:    vcvtdq2pd %xmm0, %ymm0
; X32-NEXT:    retl
;
; X64-LABEL: signbits_sext_shuffle_sitofp:
; X64:       # %bb.0:
; X64-NEXT:    vpmovsxdq %xmm0, %xmm1
; X64-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[2,3,0,1]
; X64-NEXT:    vpmovsxdq %xmm0, %xmm0
; X64-NEXT:    vinsertf128 $1, %xmm0, %ymm1, %ymm0
; X64-NEXT:    vpermilpd {{.*#+}} ymm0 = ymm0[1,0,3,2]
; X64-NEXT:    vperm2f128 {{.*#+}} ymm0 = ymm0[2,3,0,1]
; X64-NEXT:    vextractf128 $1, %ymm0, %xmm1
; X64-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[0,2]
; X64-NEXT:    vcvtdq2pd %xmm0, %ymm0
; X64-NEXT:    retq
  %1 = sext <4 x i32> %a0 to <4 x i64>
  %2 = shufflevector <4 x i64> %1, <4 x i64>%a1, <4 x i32> <i32 3, i32 2, i32 1, i32 0>
  %3 = sitofp <4 x i64> %2 to <4 x double>
  ret <4 x double> %3
}

; TODO: Fix vpshufd+vpsrlq -> vpshufd/vpermilps
define <2 x double> @signbits_ashr_concat_ashr_extract_sitofp(<2 x i64> %a0, <4 x i64> %a1) nounwind {
; X32-LABEL: signbits_ashr_concat_ashr_extract_sitofp:
; X32:       # %bb.0:
; X32-NEXT:    vpermilps {{.*#+}} xmm0 = xmm0[1,3,2,3]
; X32-NEXT:    vcvtdq2pd %xmm0, %xmm0
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_concat_ashr_extract_sitofp:
; X64:       # %bb.0:
; X64-NEXT:    vpsrlq $32, %xmm0, %xmm0
; X64-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[0,2,2,3]
; X64-NEXT:    vcvtdq2pd %xmm0, %xmm0
; X64-NEXT:    retq
  %1 = ashr <2 x i64> %a0, <i64 16, i64 16>
  %2 = shufflevector <2 x i64> %1, <2 x i64> undef, <4 x i32> <i32 0, i32 1, i32 undef, i32 undef>
  %3 = shufflevector <4 x i64> %a1, <4 x i64> %2, <4 x i32> <i32 0, i32 1, i32 4, i32 5>
  %4 = ashr <4 x i64> %3, <i64 16, i64 16, i64 16, i64 16>
  %5 = shufflevector <4 x i64> %4, <4 x i64> undef, <2 x i32> <i32 2, i32 3>
  %6 = sitofp <2 x i64> %5 to <2 x double>
  ret <2 x double> %6
}

define float @signbits_ashr_sext_sextinreg_and_extract_sitofp(<2 x i64> %a0, <2 x i64> %a1, i32 %a2) nounwind {
; X32-LABEL: signbits_ashr_sext_sextinreg_and_extract_sitofp:
; X32:       # %bb.0:
; X32-NEXT:    pushl %eax
; X32-NEXT:    vpsrad $29, %xmm0, %xmm0
; X32-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[1,1,3,3]
; X32-NEXT:    vmovd {{.*#+}} xmm1 = mem[0],zero,zero,zero
; X32-NEXT:    vpand %xmm1, %xmm0, %xmm0
; X32-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X32-NEXT:    vmovss %xmm0, (%esp)
; X32-NEXT:    flds (%esp)
; X32-NEXT:    popl %eax
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_sext_sextinreg_and_extract_sitofp:
; X64:       # %bb.0:
; X64-NEXT:    vpsrad $29, %xmm0, %xmm0
; X64-NEXT:    vpshufd {{.*#+}} xmm0 = xmm0[1,1,3,3]
; X64-NEXT:    vmovd %edi, %xmm1
; X64-NEXT:    vpand %xmm1, %xmm0, %xmm0
; X64-NEXT:    vmovq %xmm0, %rax
; X64-NEXT:    vcvtsi2ss %eax, %xmm2, %xmm0
; X64-NEXT:    retq
  %1 = ashr <2 x i64> %a0, <i64 61, i64 60>
  %2 = sext i32 %a2 to i64
  %3 = insertelement <2 x i64> %a1, i64 %2, i32 0
  %4 = shl <2 x i64> %3, <i64 20, i64 20>
  %5 = ashr <2 x i64> %4, <i64 20, i64 20>
  %6 = and <2 x i64> %1, %5
  %7 = extractelement <2 x i64> %6, i32 0
  %8 = sitofp i64 %7 to float
  ret float %8
}

define float @signbits_ashr_sextvecinreg_bitops_extract_sitofp(<2 x i64> %a0, <4 x i32> %a1) nounwind {
; X32-LABEL: signbits_ashr_sextvecinreg_bitops_extract_sitofp:
; X32:       # %bb.0:
; X32-NEXT:    pushl %eax
; X32-NEXT:    vpsrlq $60, %xmm0, %xmm2
; X32-NEXT:    vpsrlq $61, %xmm0, %xmm0
; X32-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1,2,3],xmm2[4,5,6,7]
; X32-NEXT:    vmovdqa {{.*#+}} xmm2 = [4,0,8,0]
; X32-NEXT:    vpxor %xmm2, %xmm0, %xmm0
; X32-NEXT:    vpsubq %xmm2, %xmm0, %xmm0
; X32-NEXT:    vpmovsxdq %xmm1, %xmm1
; X32-NEXT:    vpand %xmm1, %xmm0, %xmm2
; X32-NEXT:    vpor %xmm1, %xmm2, %xmm1
; X32-NEXT:    vpxor %xmm0, %xmm1, %xmm0
; X32-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X32-NEXT:    vmovss %xmm0, (%esp)
; X32-NEXT:    flds (%esp)
; X32-NEXT:    popl %eax
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_sextvecinreg_bitops_extract_sitofp:
; X64:       # %bb.0:
; X64-NEXT:    vpsrlq $60, %xmm0, %xmm2
; X64-NEXT:    vpsrlq $61, %xmm0, %xmm0
; X64-NEXT:    vpblendw {{.*#+}} xmm0 = xmm0[0,1,2,3],xmm2[4,5,6,7]
; X64-NEXT:    vmovdqa {{.*#+}} xmm2 = [4,8]
; X64-NEXT:    vpxor %xmm2, %xmm0, %xmm0
; X64-NEXT:    vpsubq %xmm2, %xmm0, %xmm0
; X64-NEXT:    vpmovsxdq %xmm1, %xmm1
; X64-NEXT:    vpand %xmm1, %xmm0, %xmm2
; X64-NEXT:    vpor %xmm1, %xmm2, %xmm1
; X64-NEXT:    vpxor %xmm0, %xmm1, %xmm0
; X64-NEXT:    vmovq %xmm0, %rax
; X64-NEXT:    vcvtsi2ss %eax, %xmm3, %xmm0
; X64-NEXT:    retq
  %1 = ashr <2 x i64> %a0, <i64 61, i64 60>
  %2 = shufflevector <4 x i32> %a1, <4 x i32> undef, <2 x i32> <i32 0, i32 1>
  %3 = sext <2 x i32> %2 to <2 x i64>
  %4 = and <2 x i64> %1, %3
  %5 = or <2 x i64> %4, %3
  %6 = xor <2 x i64> %5, %1
  %7 = extractelement <2 x i64> %6, i32 0
  %8 = sitofp i64 %7 to float
  ret float %8
}

define <4 x float> @signbits_ashr_sext_select_shuffle_sitofp(<4 x i64> %a0, <4 x i64> %a1, <4 x i64> %a2, <4 x i32> %a3) nounwind {
; X32-LABEL: signbits_ashr_sext_select_shuffle_sitofp:
; X32:       # %bb.0:
; X32-NEXT:    pushl %ebp
; X32-NEXT:    movl %esp, %ebp
; X32-NEXT:    andl $-16, %esp
; X32-NEXT:    subl $16, %esp
; X32-NEXT:    vpmovsxdq 8(%ebp), %xmm3
; X32-NEXT:    vpmovsxdq 16(%ebp), %xmm4
; X32-NEXT:    vpsrad $31, %xmm2, %xmm5
; X32-NEXT:    vpsrad $1, %xmm2, %xmm6
; X32-NEXT:    vpshufd {{.*#+}} xmm6 = xmm6[1,1,3,3]
; X32-NEXT:    vpblendw {{.*#+}} xmm5 = xmm6[0,1],xmm5[2,3],xmm6[4,5],xmm5[6,7]
; X32-NEXT:    vextractf128 $1, %ymm2, %xmm2
; X32-NEXT:    vpsrad $31, %xmm2, %xmm6
; X32-NEXT:    vpsrad $1, %xmm2, %xmm2
; X32-NEXT:    vpshufd {{.*#+}} xmm2 = xmm2[1,1,3,3]
; X32-NEXT:    vpblendw {{.*#+}} xmm2 = xmm2[0,1],xmm6[2,3],xmm2[4,5],xmm6[6,7]
; X32-NEXT:    vpcmpeqq %xmm1, %xmm0, %xmm6
; X32-NEXT:    vblendvpd %xmm6, %xmm5, %xmm3, %xmm3
; X32-NEXT:    vextractf128 $1, %ymm1, %xmm1
; X32-NEXT:    vextractf128 $1, %ymm0, %xmm0
; X32-NEXT:    vpcmpeqq %xmm1, %xmm0, %xmm0
; X32-NEXT:    vblendvpd %xmm0, %xmm2, %xmm4, %xmm0
; X32-NEXT:    vinsertf128 $1, %xmm0, %ymm3, %ymm0
; X32-NEXT:    vmovddup {{.*#+}} ymm0 = ymm0[0,0,2,2]
; X32-NEXT:    vextractf128 $1, %ymm0, %xmm1
; X32-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[0,2]
; X32-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X32-NEXT:    movl %ebp, %esp
; X32-NEXT:    popl %ebp
; X32-NEXT:    vzeroupper
; X32-NEXT:    retl
;
; X64-LABEL: signbits_ashr_sext_select_shuffle_sitofp:
; X64:       # %bb.0:
; X64-NEXT:    vpsrad $31, %xmm2, %xmm4
; X64-NEXT:    vpsrad $1, %xmm2, %xmm5
; X64-NEXT:    vpshufd {{.*#+}} xmm5 = xmm5[1,1,3,3]
; X64-NEXT:    vpblendw {{.*#+}} xmm4 = xmm5[0,1],xmm4[2,3],xmm5[4,5],xmm4[6,7]
; X64-NEXT:    vextractf128 $1, %ymm2, %xmm2
; X64-NEXT:    vpsrad $31, %xmm2, %xmm5
; X64-NEXT:    vpsrad $1, %xmm2, %xmm2
; X64-NEXT:    vpshufd {{.*#+}} xmm2 = xmm2[1,1,3,3]
; X64-NEXT:    vpblendw {{.*#+}} xmm2 = xmm2[0,1],xmm5[2,3],xmm2[4,5],xmm5[6,7]
; X64-NEXT:    vpmovsxdq %xmm3, %xmm5
; X64-NEXT:    vpshufd {{.*#+}} xmm3 = xmm3[2,3,0,1]
; X64-NEXT:    vpmovsxdq %xmm3, %xmm3
; X64-NEXT:    vpcmpeqq %xmm1, %xmm0, %xmm6
; X64-NEXT:    vblendvpd %xmm6, %xmm4, %xmm5, %xmm4
; X64-NEXT:    vextractf128 $1, %ymm1, %xmm1
; X64-NEXT:    vextractf128 $1, %ymm0, %xmm0
; X64-NEXT:    vpcmpeqq %xmm1, %xmm0, %xmm0
; X64-NEXT:    vblendvpd %xmm0, %xmm2, %xmm3, %xmm0
; X64-NEXT:    vinsertf128 $1, %xmm0, %ymm4, %ymm0
; X64-NEXT:    vmovddup {{.*#+}} ymm0 = ymm0[0,0,2,2]
; X64-NEXT:    vextractf128 $1, %ymm0, %xmm1
; X64-NEXT:    vshufps {{.*#+}} xmm0 = xmm0[0,2],xmm1[0,2]
; X64-NEXT:    vcvtdq2ps %xmm0, %xmm0
; X64-NEXT:    vzeroupper
; X64-NEXT:    retq
  %1 = ashr <4 x i64> %a2, <i64 33, i64 63, i64 33, i64 63>
  %2 = sext <4 x i32> %a3 to <4 x i64>
  %3 = icmp eq <4 x i64> %a0, %a1
  %4 = select <4 x i1> %3, <4 x i64> %1, <4 x i64> %2
  %5 = shufflevector <4 x i64> %4, <4 x i64> undef, <4 x i32> <i32 0, i32 0, i32 2, i32 2>
  %6 = sitofp <4 x i64> %5 to <4 x float>
  ret <4 x float> %6
}

; Make sure we can preserve sign bit information into the second basic block
; so we can avoid having to shift bit 0 into bit 7 for each element due to
; v32i1->v32i8 promotion and the splitting of v32i8 into 2xv16i8. This requires
; ComputeNumSignBits handling for insert_subvector.
define void @cross_bb_signbits_insert_subvec(<32 x i8>* %ptr, <32 x i8> %x, <32 x i8> %z) {
; X32-LABEL: cross_bb_signbits_insert_subvec:
; X32:       # %bb.0:
; X32-NEXT:    movl {{[0-9]+}}(%esp), %eax
; X32-NEXT:    vextractf128 $1, %ymm0, %xmm2
; X32-NEXT:    vpxor %xmm3, %xmm3, %xmm3
; X32-NEXT:    vpcmpeqb %xmm3, %xmm2, %xmm2
; X32-NEXT:    vpcmpeqb %xmm3, %xmm0, %xmm0
; X32-NEXT:    vinsertf128 $1, %xmm2, %ymm0, %ymm0
; X32-NEXT:    vandnps %ymm1, %ymm0, %ymm1
; X32-NEXT:    vandps {{\.LCPI.*}}, %ymm0, %ymm0
; X32-NEXT:    vorps %ymm1, %ymm0, %ymm0
; X32-NEXT:    vmovaps %ymm0, (%eax)
; X32-NEXT:    vzeroupper
; X32-NEXT:    retl
;
; X64-LABEL: cross_bb_signbits_insert_subvec:
; X64:       # %bb.0:
; X64-NEXT:    vextractf128 $1, %ymm0, %xmm2
; X64-NEXT:    vpxor %xmm3, %xmm3, %xmm3
; X64-NEXT:    vpcmpeqb %xmm3, %xmm2, %xmm2
; X64-NEXT:    vpcmpeqb %xmm3, %xmm0, %xmm0
; X64-NEXT:    vinsertf128 $1, %xmm2, %ymm0, %ymm0
; X64-NEXT:    vandnps %ymm1, %ymm0, %ymm1
; X64-NEXT:    vandps {{.*}}(%rip), %ymm0, %ymm0
; X64-NEXT:    vorps %ymm1, %ymm0, %ymm0
; X64-NEXT:    vmovaps %ymm0, (%rdi)
; X64-NEXT:    vzeroupper
; X64-NEXT:    retq
  %a = icmp eq <32 x i8> %x, zeroinitializer
  %b = icmp eq <32 x i8> %x, zeroinitializer
  %c = and <32 x i1> %a, %b
  br label %block

block:
  %d = select <32 x i1> %c, <32 x i8> <i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1, i8 1>, <32 x i8> %z
  store <32 x i8> %d, <32 x i8>* %ptr, align 32
  br label %exit

exit:
  ret void
}

