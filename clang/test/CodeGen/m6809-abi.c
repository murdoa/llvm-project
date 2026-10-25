// RUN: %clang_cc1 -triple m6809 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple m6809 -target-cpu 6309 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple m6800 -emit-llvm -o - %s | FileCheck %s

// The M6809 and M6800 share one C ABI. Integers narrower than int are
// extended, wider scalars are passed whole, and every aggregate, however
// small, is passed by value in the argument area and returned through a
// hidden pointer. Zero-sized aggregates take no space.

// CHECK: target datalayout = "E-m:e-p:16:8-i16:8-i32:8-i64:8-f32:8-f64:8-a:8-n8:16-S8"

struct s1 { char a; };
struct s4 { int a, b; };
struct empty {};
union u { long l; char c; };

// CHECK-LABEL: define{{.*}} zeroext i1 @f_bool(i1 noundef zeroext %x)
_Bool f_bool(_Bool x) { return x; }
// CHECK-LABEL: define{{.*}} signext i8 @f_char(i8 noundef signext %x)
char f_char(char x) { return x; }
// CHECK-LABEL: define{{.*}} zeroext i8 @f_uchar(i8 noundef zeroext %x)
unsigned char f_uchar(unsigned char x) { return x; }
// CHECK-LABEL: define{{.*}} signext i16 @f_short(i16 noundef signext %x)
short f_short(short x) { return x; }
// CHECK-LABEL: define{{.*}} i16 @f_int(i16 noundef %x, i16 noundef %y)
int f_int(int x, int y) { return x + y; }
// CHECK-LABEL: define{{.*}} i32 @f_long(i32 noundef %x)
long f_long(long x) { return x; }
// CHECK-LABEL: define{{.*}} i64 @f_llong(i64 noundef %x)
long long f_llong(long long x) { return x; }
// CHECK-LABEL: define{{.*}} float @f_float(float noundef %x)
float f_float(float x) { return x; }
// CHECK-LABEL: define{{.*}} double @f_double(double noundef %x)
double f_double(double x) { return x; }
// CHECK-LABEL: define{{.*}} ptr @f_ptr(ptr noundef %p)
void *f_ptr(void *p) { return p; }

// CHECK-LABEL: define{{.*}} void @f_s1(ptr {{.*}}sret(%struct.s1) align 1 %agg.result, ptr noundef byval(%struct.s1) align 1 %x)
struct s1 f_s1(struct s1 x) { return x; }
// CHECK-LABEL: define{{.*}} void @f_s4(ptr {{.*}}sret(%struct.s4) align 1 %agg.result, ptr noundef byval(%struct.s4) align 1 %x)
struct s4 f_s4(struct s4 x) { return x; }
// CHECK-LABEL: define{{.*}} void @f_union(ptr {{.*}}sret(%union.u) align 1 %agg.result, ptr noundef byval(%union.u) align 1 %x)
union u f_union(union u x) { return x; }
// CHECK-LABEL: define{{.*}} i16 @f_empty(i16 noundef %x)
int f_empty(struct empty e, int x) { return x; }
