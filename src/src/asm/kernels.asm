; Prism Player - x86-64 hand-written assembly kernels.
;
; The hot path of every software-decoded frame is YUV -> BGRA conversion.
; This file implements it with AVX2 integer math (Q16 fixed point), 8 pixels
; per iteration, memory-bound and branch-free.
;
; Windows x64 ABI.  All YMM6..YMM15 state is preserved.
;
; void prism_yuv420_row_avx2(uint8_t* dst, const uint8_t* y, const uint8_t* u,
;                            const uint8_t* v, int groups8, const int32_t* coef);
;   rcx = dst, rdx = y, r8 = u, r9 = v, [rsp+28h] = groups8, [rsp+30h] = coef
;   coef[0]=ys, [1]=rv, [2]=gu, [3]=gv, [4]=bu, [5]=offY, [6]=offC

bits 64
default rel

section .text
global prism_yuv420_row_avx2

align 32
prism_yuv420_row_avx2:
    mov     r10d, dword [rsp+28h]
    mov     r11,  qword [rsp+30h]
    test    r10d, r10d
    jle     .empty

    sub     rsp, 328                    ; spill YMM6..YMM15 (320 bytes) + pad
    vmovdqu [rsp+0],   ymm6
    vmovdqu [rsp+32],  ymm7
    vmovdqu [rsp+64],  ymm8
    vmovdqu [rsp+96],  ymm9
    vmovdqu [rsp+128], ymm10
    vmovdqu [rsp+160], ymm11
    vmovdqu [rsp+192], ymm12
    vmovdqu [rsp+224], ymm13
    vmovdqu [rsp+256], ymm14
    vmovdqu [rsp+288], ymm15

    vpbroadcastd ymm6,  dword [r11+0]   ; ys
    vpbroadcastd ymm7,  dword [r11+4]   ; rv
    vpbroadcastd ymm8,  dword [r11+8]   ; gu
    vpbroadcastd ymm9,  dword [r11+12]  ; gv
    vpbroadcastd ymm10, dword [r11+16]  ; bu
    vpbroadcastd ymm11, dword [r11+20]  ; offY
    vpbroadcastd ymm12, dword [r11+24]  ; offC
    vpbroadcastd ymm13, dword [round16] ; 1 << 15
    vmovdqa     ymm14, [dup_idx]
    vmovdqa     ymm15, [alpha255]

align 16
.loop:
    ; ---- luma: y' = ((y - offY) * ys + round) >> 16
    vmovq       xmm0, [rdx]
    vpmovzxbd   ymm0, xmm0
    vpsubd      ymm0, ymm0, ymm11
    vpmulld     ymm0, ymm0, ymm6
    vpaddd      ymm0, ymm0, ymm13
    vpsrad      ymm0, ymm0, 16

    ; ---- chroma: widen 4 samples to 8 lanes by duplication
    vmovd       xmm1, [r8]
    vpmovzxbd   ymm1, xmm1
    vpermd      ymm1, ymm14, ymm1
    vpsubd      ymm1, ymm1, ymm12

    vmovd       xmm2, [r9]
    vpmovzxbd   ymm2, xmm2
    vpermd      ymm2, ymm14, ymm2
    vpsubd      ymm2, ymm2, ymm12

    ; ---- R = y' + (v * rv + round) >> 16
    vpmulld     ymm3, ymm2, ymm7
    vpaddd      ymm3, ymm3, ymm13
    vpsrad      ymm3, ymm3, 16
    vpaddd      ymm3, ymm3, ymm0

    ; ---- G = y' + (u * gu + v * gv + round) >> 16
    vpmulld     ymm4, ymm1, ymm8
    vpmulld     ymm5, ymm2, ymm9
    vpaddd      ymm4, ymm4, ymm5
    vpaddd      ymm4, ymm4, ymm13
    vpsrad      ymm4, ymm4, 16
    vpaddd      ymm4, ymm4, ymm0

    ; ---- B = y' + (u * bu + round) >> 16
    vpmulld     ymm5, ymm1, ymm10
    vpaddd      ymm5, ymm5, ymm13
    vpsrad      ymm5, ymm5, 16
    vpaddd      ymm5, ymm5, ymm0

    ; ---- saturating pack to 8-bit and interleave B,G,R,A
    vpackusdw   ymm3, ymm3, ymm4
    vpackusdw   ymm5, ymm5, ymm15
    vpackuswb   ymm3, ymm3, ymm5
    vpshufb     ymm3, ymm3, [bgra_shuffle]
    vmovdqu     [rcx], ymm3

    add         rcx, 32
    add         rdx, 8
    add         r8, 4
    add         r9, 4
    dec         r10d
    jnz         .loop

    vmovdqu ymm6,  [rsp+0]
    vmovdqu ymm7,  [rsp+32]
    vmovdqu ymm8,  [rsp+64]
    vmovdqu ymm9,  [rsp+96]
    vmovdqu ymm10, [rsp+128]
    vmovdqu ymm11, [rsp+160]
    vmovdqu ymm12, [rsp+192]
    vmovdqu ymm13, [rsp+224]
    vmovdqu ymm14, [rsp+256]
    vmovdqu ymm15, [rsp+288]
    add     rsp, 328
.empty:
    ret

section .rdata align=32
dup_idx:       dd 0, 0, 1, 1, 2, 2, 3, 3
alpha255:      dd 255, 255, 255, 255, 255, 255, 255, 255
round16:       dd 32768
bgra_shuffle:  db 8,4,0,12, 9,5,1,13, 10,6,2,14, 11,7,3,15
               db 8,4,0,12, 9,5,1,13, 10,6,2,14, 11,7,3,15
