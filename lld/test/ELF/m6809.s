; REQUIRES: m6809
; RUN: llvm-mc -filetype=obj -triple=m6809 -o %t1.o %s
; RUN: printf '.global far\nfar: nop\n' | llvm-mc -filetype=obj -triple=m6809 -o %t2.o -
; RUN: ld.lld -o %t.exe --Ttext=0x1000 --Tdata=0x2000 --defsym=_byte=0x21 \
; RUN:   --defsym=_dp=0xff40 --defsym=near=0x1080 %t1.o %t2.o
; RUN: llvm-objdump -s %t.exe | FileCheck %s
; RUN: llvm-readelf -h %t.exe | FileCheck --check-prefix=HDR %s

;; The 6809 relocations. Branch and ,pcr offsets count from the end of the
;; instruction. The assembler makes a branch to an undefined symbol long,
;; so R_M6809_PCREL8 is not produced here.

; HDR: Machine: Motorola 6809
; HDR: Entry point address: 0x1000

  .text
  .global _start
_start:
  bra near          ; R_M6809_PCREL16: 0x1080 - 0x1003 = 0x007d
  lbra far          ; R_M6809_PCREL16: 0x1014 - 0x1006 = 0x000e
  lda <_dp          ; R_M6809_DP8:     0x40
  ldx #_start       ; R_M6809_16:      0x1000
  leax _start,pcr   ; R_M6809_PCREL16: 0x1000 - 0x100f = 0xfff1
  ldb #_byte        ; R_M6809_8:       0x21

;; The second file's .text starts at 0x1014, after SWI padding.
; CHECK:      Contents of section .text:
; CHECK-NEXT: 1000 16007d16 000e9640 8e100030 8dfff1c6
; CHECK-NEXT: 1010 213f3f3f 12

  .data
  .byte _byte       ; R_M6809_8
  .short _start     ; R_M6809_16
  .long far         ; R_M6809_32

; CHECK:      Contents of section .data:
; CHECK-NEXT: 2000 21100000 001014
