; RUN: llc -mtriple=m6809 -verify-machineinstrs < %s | FileCheck %s
; RUN: llc -mtriple=m6809 -verify-machineinstrs \
; RUN:   -m6809-disable-post-ra-cleanup < %s | FileCheck %s --check-prefix=OFF

; Register allocation stores D to a spill slot and loads it straight back.
; The load reads the value D already holds, so the cleanup removes it.

define i8 @parity(i16 %v) {
; CHECK-LABEL: parity:
; CHECK:         std ,s
; CHECK-NOT:     ldd ,s
; CHECK:         tfr a, b
; OFF-LABEL: parity:
; OFF:         std ,s
; OFF-NEXT:    ldd ,s
entry:
  %shr = lshr i16 %v, 8
  %xor = xor i16 %shr, %v
  %shr1 = lshr i16 %xor, 4
  %xor2 = xor i16 %shr1, %xor
  %shr3 = lshr i16 %xor2, 2
  %xor4 = xor i16 %shr3, %xor2
  %shr5 = lshr i16 %xor4, 1
  %xor6 = xor i16 %shr5, %xor4
  %0 = trunc i16 %xor6 to i8
  %conv = and i8 %0, 1
  ret i8 %conv
}
