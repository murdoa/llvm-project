// RUN: %clang_cc1 -triple m6809 -emit-llvm -o - %s | FileCheck %s

// Variadic arguments are packed one after another, structs by value.

#include <stdarg.h>

struct s3 { char a, b, c; };

// CHECK-LABEL: define{{.*}} void @get_struct(ptr {{.*}}sret(%struct.s3)
// CHECK: %argp.cur = load ptr, ptr %ap
// CHECK: %argp.next = getelementptr inbounds i8, ptr %argp.cur, i16 3
// CHECK: call void @llvm.memcpy.p0.p0.i16(ptr align 1 %{{.*}}, ptr align 1 %argp.cur, i16 3, i1 false)
struct s3 get_struct(int n, ...) {
  va_list ap;
  va_start(ap, n);
  struct s3 r = va_arg(ap, struct s3);
  va_end(ap);
  return r;
}

// CHECK-LABEL: define{{.*}} i32 @get_long
// CHECK: %argp.next = getelementptr inbounds i8, ptr %argp.cur, i16 4
// CHECK: load i32, ptr %argp.cur, align 1
long get_long(int n, ...) {
  va_list ap;
  va_start(ap, n);
  long r = va_arg(ap, long);
  va_end(ap);
  return r;
}

// CHECK-LABEL: define{{.*}} void @pass_struct
// CHECK: call void (ptr, i16, ...) @get_struct(ptr {{.*}}sret(%struct.s3) {{.*}}, i16 noundef 1, ptr noundef byval(%struct.s3) align 1
struct s3 pass_struct(struct s3 x) { return get_struct(1, x); }

struct empty {};

// A zero-sized struct takes no argument space.
// CHECK-LABEL: define{{.*}} void @pass_empty
// CHECK: call void (ptr, i16, ...) @get_struct(ptr {{.*}}, i16 noundef 2, ptr noundef byval(%struct.s3) align 1 %{{.*}})
struct s3 pass_empty(struct empty e, struct s3 x) { return get_struct(2, e, x); }
