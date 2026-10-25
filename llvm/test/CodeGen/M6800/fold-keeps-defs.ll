; RUN: llc -mtriple=m6800 -mcpu=6301 -pre-RA-sched=source -verify-machineinstrs < %s | FileCheck %s

; A spill folded into "and" keeps the dead X def of the register-register
; pseudo. Without it the spiller removed the segment from X's live range
; while the greedy allocator's interference cache still pointed into it.

; CHECK-LABEL: fold:
; CHECK: rts
define i1 @fold(i16 %a, i16 %n) {
entry:
  %nz.a = icmp ne i16 %a, 0
  store i16 0, ptr null, align 1
  %nz.n = icmp ne i16 %n, 0
  %both = select i1 %nz.n, i1 %nz.a, i1 false
  %both.ext = zext i1 %both to i16
  br label %head

head:
  %v = phi i16 [ 0, %entry ], [ %next, %latch ]
  %done = icmp eq i16 %n, 0
  br i1 %done, label %exit, label %body

body:
  %never = icmp ult i16 0, 0
  br i1 %never, label %then, label %latch

then:
  br label %latch

latch:
  %next = phi i16 [ %both.ext, %then ], [ 0, %body ]
  br label %head

exit:
  %z = icmp eq i16 %v, 0
  ret i1 %z
}
