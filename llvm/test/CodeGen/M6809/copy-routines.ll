; RUN: llc -mtriple=m6809 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=m6809 -filetype=obj < %s | llvm-readelf -S -s - \
; RUN:   | FileCheck %s --check-prefix=ELF

; A short copy or fill loop is a call to a shared routine for its pair of
; registers, which the module emits once, in a COMDAT group so that the
; linker keeps one copy. Clear falls into fill. A call just before the
; return jumps.

declare void @use(ptr)

define void @copy_then_call(ptr %d, ptr %s) minsize {
; CHECK-LABEL: copy_then_call:
; CHECK:         ldb #9
; CHECK-NEXT:    jsr __m6809_copy_xu
; CHECK:         jsr use
  call void @llvm.memcpy.p0.p0.i16(ptr %d, ptr %s, i16 9, i1 false)
  call void @use(ptr %d)
  ret void
}

define void @copy_again(ptr %d, ptr %s) minsize {
; CHECK-LABEL: copy_again:
; CHECK:         ldb #7
; CHECK-NEXT:    jmp __m6809_copy_{{[xyu][xyu]}}
  call void @llvm.memcpy.p0.p0.i16(ptr %d, ptr %s, i16 7, i1 false)
  ret void
}

define void @fill(ptr %d) minsize {
; CHECK-LABEL: fill:
; CHECK:         lda #85
; CHECK-NEXT:    ldb #30
; CHECK-NEXT:    jmp __m6809_fill_x
  call void @llvm.memset.p0.i16(ptr %d, i8 85, i16 30, i1 false)
  ret void
}

define void @clear(ptr %d) minsize {
; CHECK-LABEL: clear:
; CHECK:         ldb #30
; CHECK-NEXT:    jmp __m6809_clear_x
  call void @llvm.memset.p0.i16(ptr %d, i8 0, i16 30, i1 false)
  ret void
}

; CHECK:         .section .text.__m6809_copy_xu,"axG",@progbits,__m6809_copy_xu,comdat
; CHECK-NEXT:    .weak __m6809_copy_xu
; CHECK-NEXT:    .hidden __m6809_copy_xu
; CHECK-NEXT:    .type __m6809_copy_xu,@function
; CHECK-NEXT:  __m6809_copy_xu:
; CHECK-NEXT:  [[L:.Ltmp[0-9]+]]:
; CHECK-NEXT:    lda ,x+
; CHECK-NEXT:    sta ,u+
; CHECK-NEXT:    decb
; CHECK-NEXT:    bne [[L]]
; CHECK-NEXT:    rts
; CHECK:         .section .text.__m6809_fill_x,"axG",@progbits,__m6809_fill_x,comdat
; CHECK-NEXT:    .weak __m6809_clear_x
; CHECK-NEXT:    .hidden __m6809_clear_x
; CHECK-NEXT:    .type __m6809_clear_x,@function
; CHECK-NEXT:  __m6809_clear_x:
; CHECK-NEXT:    clra
; CHECK-NEXT:    .weak __m6809_fill_x
; CHECK-NEXT:    .hidden __m6809_fill_x
; CHECK-NEXT:    .type __m6809_fill_x,@function
; CHECK-NEXT:  __m6809_fill_x:
; CHECK-NEXT:  [[M:.Ltmp[0-9]+]]:
; CHECK-NEXT:    sta ,x+
; CHECK-NEXT:    decb
; CHECK-NEXT:    bne [[M]]
; CHECK-NEXT:    rts
; CHECK-NOT:     .section .text.__m6809

; ELF-DAG: .text.__m6809_copy_xu PROGBITS {{.*}} AXG
; ELF-DAG: .text.__m6809_fill_x PROGBITS {{.*}} 000007 00 AXG
; ELF-DAG: FUNC WEAK HIDDEN {{.*}} __m6809_copy_xu
; ELF-DAG: FUNC WEAK HIDDEN {{.*}} __m6809_clear_x
; ELF-DAG: FUNC WEAK HIDDEN {{.*}} __m6809_fill_x
