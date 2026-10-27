; RUN: llc -mtriple=m6809 -m6809-copy-entry=5 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s
; RUN: llc -mtriple=m6809 -verify-machineinstrs < %s \
; RUN:   | FileCheck %s --check-prefix=NONE

; With -m6809-copy-entry=5 a copy of five bytes calls the routine's entry
; that loads B itself, which falls into the loop. Other lengths load B as
; before. The group's name carries the length, so a group from an object
; built without the option (which lacks the entry) never replaces it.

declare void @use(ptr)

define void @five(ptr %d, ptr %s) minsize {
; CHECK-LABEL: five:
; CHECK-NOT:     ldb #5
; CHECK:         jsr __m6809_copy5_xu
; CHECK:         jsr use
; NONE-LABEL: five:
; NONE:          ldb #5
; NONE-NEXT:     jsr __m6809_copy_xu
  call void @llvm.memcpy.p0.p0.i16(ptr %d, ptr %s, i16 5, i1 false)
  call void @use(ptr %d)
  ret void
}

define void @seven(ptr %d, ptr %s) minsize {
; CHECK-LABEL: seven:
; CHECK:         ldb #7
; CHECK-NEXT:    jmp __m6809_copy_{{[xyu][xyu]}}
  call void @llvm.memcpy.p0.p0.i16(ptr %d, ptr %s, i16 7, i1 false)
  ret void
}

; CHECK:       .section .text.__m6809_copy_xu.5,"axG",@progbits,__m6809_copy_xu.5,comdat
; CHECK:       __m6809_copy5_xu:
; CHECK-NEXT:    ldb #5
; CHECK:       __m6809_copy_xu:
; CHECK-NEXT:  .Ltmp{{[0-9]+}}:
; CHECK-NEXT:    lda ,x+
; CHECK-NEXT:    sta ,u+
; CHECK-NEXT:    decb
; CHECK-NEXT:    bne .Ltmp{{[0-9]+}}
; CHECK-NEXT:    rts
; NONE:        .section .text.__m6809_copy_xu,"axG",@progbits,__m6809_copy_xu,comdat
; NONE-NOT:    __m6809_copy5_xu
