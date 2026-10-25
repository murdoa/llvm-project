; RUN: llc -mtriple=m6809 -O2 < %s | FileCheck %s

; The loop counter is decremented by LEAX or LEAY, which set Z. A register move
; scheduled between it and the branch leaves the flags alone, so no test of
; X is needed.

define i8 @sum(ptr %p) {
; CHECK-LABEL: sum:
; CHECK:       lea[[R:[xy]]] -1,[[R]]
; CHECK-NEXT:  tfr b, a
; CHECK-NEXT:  bne
entry:
  br label %loop

loop:
  %s = phi i8 [ 0, %entry ], [ %s2, %loop ]
  %i = phi i16 [ 0, %entry ], [ %inc, %loop ]
  %a = getelementptr inbounds i8, ptr %p, i16 %i
  %v = load volatile i8, ptr %a
  %hi = lshr i8 %v, 4
  %lo = and i8 %v, 15
  %s1 = add i8 %lo, %s
  %s2 = add i8 %s1, %hi
  %inc = add nuw nsw i16 %i, 1
  %done = icmp eq i16 %inc, 255
  br i1 %done, label %exit, label %loop

exit:
  ret i8 %s2
}
