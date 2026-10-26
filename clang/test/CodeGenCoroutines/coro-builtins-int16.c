// The coroutine builtins take int where the intrinsics take i32, so on a
// target with a 16-bit int the arguments are widened.
// (The alignment must be a constant.)
// RUN: %clang_cc1 -triple m6809 -fcoroutines -emit-llvm %s -o - -disable-llvm-passes | FileCheck %s
// RUN: %clang_cc1 -triple m6800 -fcoroutines -emit-llvm %s -o - -disable-llvm-passes | FileCheck %s

void *promise(void *p) {
  // CHECK: call ptr @llvm.coro.promise(ptr %{{.*}}, i32 2, i1 false)
  return __builtin_coro_promise(p, 2, 0);
}

