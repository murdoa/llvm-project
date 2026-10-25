; RUN: llc -mtriple=m6809 -O2 < %s | FileCheck %s

; A loop counter is tested against zero after each decrement. LEAX and LEAY
; set Z, LEAU does not, so the counter should not get U: that would need a
; "cmpu #0" on every pass.

define void @copy(ptr %d, ptr %s, i16 %n) {
; CHECK-LABEL: copy:
; CHECK:       lea{{[xy]}} -1,{{[xy]}}
; CHECK-NEXT:  bne
; CHECK-NOT:   cmpu
entry:
  %z = icmp eq i16 %n, 0
  br i1 %z, label %exit, label %loop

loop:
  %c = phi i16 [ %c1, %loop ], [ %n, %entry ]
  %pd = phi ptr [ %pd1, %loop ], [ %d, %entry ]
  %ps = phi ptr [ %ps1, %loop ], [ %s, %entry ]
  %c1 = add i16 %c, -1
  %ps1 = getelementptr inbounds i8, ptr %ps, i16 1
  %v = load i8, ptr %ps
  %pd1 = getelementptr inbounds i8, ptr %pd, i16 1
  store i8 %v, ptr %pd
  %done = icmp eq i16 %c1, 0
  br i1 %done, label %exit, label %loop

exit:
  ret void
}
