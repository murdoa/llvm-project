// RUN: %clang_cc1 -triple m6809 -emit-llvm -o - %s | FileCheck %s

// llvm.eh.sjlj.setjmp gives an i32, which is wider than int here.

void *buf[5];

// CHECK-LABEL: define{{.*}} i16 @f(
// CHECK: [[R:%.*]] = call i32 @llvm.eh.sjlj.setjmp(ptr @buf)
// CHECK: trunc i32 [[R]] to i16
int f(void) { return __builtin_setjmp(buf); }

// CHECK-LABEL: define{{.*}} void @g(
// CHECK: call void @llvm.eh.sjlj.longjmp(ptr @buf)
void g(void) { __builtin_longjmp(buf, 1); }
