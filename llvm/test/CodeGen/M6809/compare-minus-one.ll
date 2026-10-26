; RUN: llc -mtriple=m6809 < %s | FileCheck %s

; x > -1 and x <= -1 are compares with zero: TST, not CMP #-1.

; CHECK-LABEL: gt8:
; CHECK-NOT: cmpb
; CHECK: tstb
; CHECK-NEXT: blt
define i8 @gt8(i8 %x, i8 %y) {
  %c = icmp sgt i8 %x, -1
  br i1 %c, label %a, label %b
a:
  ret i8 %y
b:
  ret i8 0
}

; CHECK-LABEL: le8:
; CHECK-NOT: cmpb
; CHECK: tstb
; CHECK-NEXT: bge
define i8 @le8(i8 %x, i8 %y) {
  %c = icmp sle i8 %x, -1
  br i1 %c, label %a, label %b
a:
  ret i8 %y
b:
  ret i8 0
}

; CHECK-LABEL: gt16:
; CHECK-NOT: cmpd
; CHECK: subd #0
; CHECK-NEXT: blt
define i8 @gt16(i16 %x, i8 %y) {
  %c = icmp sgt i16 %x, -1
  br i1 %c, label %a, label %b
a:
  ret i8 %y
b:
  ret i8 0
}
