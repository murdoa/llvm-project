// RUN: %clang_cc1 -triple m6809 -emit-llvm -O1 -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple m6800 -emit-llvm -O1 -o - %s | FileCheck %s

// Member functions are not aligned to two bytes: the method pointers keep
// the virtual bit in the this adjustment, as ARM's do, so no filler byte
// goes before each one.

struct S {
  int x;
  S();
  int f();
  virtual int g();
};

// CHECK: define {{.*}} ptr @_ZN1SC2Ev({{.*}}){{.*}} #{{[0-9]+}} {
S::S() : x(1) {}
// CHECK-NOT: align 2
// CHECK: define {{.*}} i16 @_ZN1S1fEv({{.*}}){{.*}} #{{[0-9]+}} {
int S::f() { return x; }
// CHECK-NOT: align 2
// CHECK: define {{.*}} i16 @_ZN1S1gEv({{.*}}){{.*}} #{{[0-9]+}} {
int S::g() { return x; }

// CHECK-LABEL: @_Z4callR1SMS_FivE
// CHECK: ashr i16 %{{.*}}, 1
// CHECK: and i16 %{{.*}}, 1
int call(S &s, int (S::*m)()) { return (s.*m)(); }
