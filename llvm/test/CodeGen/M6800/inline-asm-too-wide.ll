; RUN: not llc -mtriple=m6800 < %s 2>&1 | FileCheck %s

; An i64 in "r" needs four 16-bit registers, and GR16 has two. Allocating
; virtual registers for it must not assert; the allocator reports it.

; CHECK: error: inline assembly requires more registers than available

define i16 @f(i64 %x) {
  %r = call i64 asm "", "=r,0"(i64 %x)
  %t = trunc i64 %r to i16
  ret i16 %t
}
