; RUN: opt -S %s -mtriple=m6809 -passes='require<libcall-lowering-info>,atomic-expand' | FileCheck %s

; The memory order arguments of the __atomic_* calls are C ints, which are
; 16 bits here.

define i32 @load32(ptr %p) {
; CHECK-LABEL: @load32(
; CHECK: call void @__atomic_load(i16 4, ptr %p, ptr %{{.*}}, i16 5)
  %v = load atomic i32, ptr %p seq_cst, align 2
  ret i32 %v
}

define i32 @cmpxchg32(ptr %p, i32 %o, i32 %n) {
; CHECK-LABEL: @cmpxchg32(
; CHECK: call zeroext i1 @__atomic_compare_exchange_4(ptr %p, ptr %{{.*}}, i32 %n, i16 4, i16 2)
  %r = cmpxchg ptr %p, i32 %o, i32 %n acq_rel acquire
  %v = extractvalue {i32, i1} %r, 0
  ret i32 %v
}
