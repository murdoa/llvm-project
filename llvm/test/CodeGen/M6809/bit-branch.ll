; RUN: llc -mtriple=m6809 -verify-machineinstrs < %s | FileCheck %s

; A branch on (x & mask) tests with BIT, so x stays where it is.

@g = global i8 0
declare void @use(i8)

; CHECK-LABEL: keep:
; CHECK-NOT: tfr
; CHECK: bitb #32
; CHECK-NEXT: beq
define void @keep(i8 %x) {
  %m = and i8 %x, 32
  %c = icmp ne i8 %m, 0
  br i1 %c, label %a, label %b
a:
  call void @use(i8 %x)
  ret void
b:
  store i8 %x, ptr @g
  ret void
}

; CHECK-LABEL: zero:
; CHECK: bitb #4
; CHECK-NEXT: bne
define void @zero(i8 %x) {
  %m = and i8 %x, 4
  %c = icmp eq i8 %m, 0
  br i1 %c, label %a, label %b
a:
  store i8 1, ptr @g
  ret void
b:
  ret void
}

; A mask that keeps every bit x can have tests x itself.
; CHECK-LABEL: bool:
; CHECK-NOT: bit
; CHECK: ldb g
; CHECK-NEXT: beq
define void @bool() {
  %v = load i8, ptr @g, !range !0
  %m = and i8 %v, 1
  %c = icmp ne i8 %m, 0
  br i1 %c, label %a, label %b
a:
  call void @use(i8 %v)
  ret void
b:
  ret void
}

!0 = !{i8 0, i8 2}
