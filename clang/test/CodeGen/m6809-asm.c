// RUN: %clang_cc1 -triple m6809 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple m6800 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple m6809 -fsyntax-only -verify=m6809 -DBAD %s
// RUN: %clang_cc1 -triple m6800 -fsyntax-only -verify=m6800 -DBAD %s

// The constraint letters are the backend's: a an index register, q an
// 8-bit accumulator, d the D accumulator.  One given register is a
// register variable.

// CHECK-LABEL: @letters(
// CHECK: call ptr asm "", "=a,0"(ptr
// CHECK: call i8 asm "", "=q,0"(i8
// CHECK: call i16 asm "", "=d,0"(i16
void letters(char *p, char c, int i, char **pp, char *cp, int *ip) {
  __asm__("" : "=a"(p) : "0"(p));
  __asm__("" : "=q"(c) : "0"(c));
  __asm__("" : "=d"(i) : "0"(i));
  *pp = p, *cp = c, *ip = i;
}

// CHECK-LABEL: @regvar(
// CHECK: call i8 asm sideeffect "", "={a},{a}"(i8
char regvar(char v) {
  register char a __asm__("a") = v;
  __asm__ volatile("" : "=r"(a) : "r"(a));
  return a;
}

#ifdef BAD
void bad(int i) {
  __asm__("" : "=x"(i)); // m6809-error {{invalid output constraint '=x' in asm}} \
                         // m6800-error {{invalid output constraint '=x' in asm}}
  __asm__("" : "=b"(i)); // m6809-error {{invalid output constraint '=b' in asm}} \
                         // m6800-error {{invalid output constraint '=b' in asm}}
}
#endif
