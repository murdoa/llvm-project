; RUN: llvm-mc -triple=m6800 -filetype=obj %s -o %t.o
; RUN: llvm-objdump -s -j .text %t.o | FileCheck %s
; RUN: llvm-objdump -r %t.o | FileCheck --check-prefix=RELOC %s

;; Branches within reach stay short. Out of reach, bra becomes jmp, bsr
;; becomes jsr, and b<cc> becomes the opposite branch over a jmp.

start:
  bra near        ; 20 02
  beq near        ; 27 00
near:
  bsr start       ; 8d fa
  bne far         ; 27 03 7e, then the address
  bra far         ; 7e
  bsr far         ; bd
  .space 300
far:
  rts

; CHECK: 0000 20022700 8dfa2703 7e00007e 0000bd00

; RELOC:      00000009 R_M6800_16 .text+0x13d
; RELOC-NEXT: 0000000c R_M6800_16 .text+0x13d
; RELOC-NEXT: 0000000f R_M6800_16 .text+0x13d
