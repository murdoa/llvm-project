; RUN: llc -mtriple=m6800 -verify-machineinstrs < %s | FileCheck %s

; Storing the i64 at %p needs a word in D and %p in X, while %flag, an 8-bit
; argument, is live across the loop. The only free registers are A and B,
; which make up D, so %flag must be reloaded from its argument slot where it
; is used instead of being kept in B. This ran out of registers in libc++'s
; std::sort for long long.

; CHECK-LABEL: store_loop:
; CHECK: ldab {{[0-9]+}},x
; CHECK: rts
define ptr @store_loop(ptr %first, i64 %v, i1 %flag) {
entry:
  br label %loop

loop:
  %p = phi ptr [ null, %entry ], [ %next, %store ], [ %next, %zero ]
  br label %inner

inner:
  br i1 %flag, label %latch, label %exit

exit:
  ret ptr null

latch:
  br i1 %flag, label %step, label %inner

step:
  %next = getelementptr i8, ptr %p, i16 -8
  %z = icmp eq ptr null, null
  br i1 %z, label %zero, label %store

zero:
  store i64 0, ptr null, align 1
  br label %loop

store:
  store i64 %v, ptr %next, align 1
  br label %loop
}
