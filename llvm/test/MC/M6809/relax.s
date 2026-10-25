; RUN: llvm-mc -triple=m6809 -filetype=obj %s | llvm-objdump -d -r - | FileCheck %s

; Short branches whose target is out of range, or in another section or
; object, become the long form.

; CHECK:      0: 20 fe   bra 0
; CHECK-NEXT: 2: 27 7f   beq 131
near:
  bra near
  beq edge
  .space 127
edge:

; CHECK:      83: 16 00 c8       lbra 334
; CHECK-NEXT: 86: 10 26 00 c4    lbne 334
; CHECK-NEXT: 8a: 17 00 00       lbsr 141
; CHECK-NEXT: 0000008b: R_M6809_PCREL16 ext
far:
  bra over
  bne over
  bsr ext
  .space 193
over:

; CHECK:      14e: 10 2b ff 31   lbmi 131
  bmi far
