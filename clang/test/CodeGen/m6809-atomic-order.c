// RUN: %clang_cc1 -triple m6809 -emit-llvm %s -o - | FileCheck %s
// REQUIRES: m6809-registered-target

// With a 16-bit int, a failure order that is not a constant is an i16, and
// the switch on it must still have cases of its own type.

// CHECK-LABEL: @cas(
// CHECK: [[ORDER:%.*]] = zext i16 {{%.*}} to i32
// CHECK: switch i32 [[ORDER]], label %monotonic_fail [
// CHECK-NEXT: i32 1, label %acquire_fail
// CHECK-NEXT: i32 2, label %acquire_fail
// CHECK-NEXT: i32 5, label %seqcst_fail
_Bool cas(int *p, int *expected, int desired, int failure) {
  return __atomic_compare_exchange_n(p, expected, desired, 0, __ATOMIC_SEQ_CST,
                                     failure);
}
