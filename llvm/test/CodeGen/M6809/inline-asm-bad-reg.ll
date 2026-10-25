; RUN: not llc -mtriple=m6809 < %s 2>&1 | FileCheck %s --check-prefixes=CHECK,M6809
; RUN: not llc -mtriple=m6809 -mcpu=6309 < %s 2>&1 | FileCheck %s --check-prefixes=CHECK,H6309

; Registers that do not copy to and from the allocatable ones are an error
; as asm operands, and so is W on the 6809.  As clobbers they stand.

; CHECK: error: could not allocate output register for constraint '{e}'
define i8 @e(i8 %a) {
  %r = call i8 asm "", "={e},{e}"(i8 %a)
  ret i8 %r
}

; CHECK: error: could not allocate output register for constraint '{f}'
define i8 @f(i8 %a) {
  %r = call i8 asm "", "={f},{f}"(i8 %a)
  ret i8 %r
}

; CHECK: error: could not allocate output register for constraint '{cc}'
define i8 @cc(i8 %a) {
  %r = call i8 asm "", "={cc},{cc}"(i8 %a)
  ret i8 %r
}

; CHECK: error: could not allocate output register for constraint '{dp}'
define i8 @dp(i8 %a) {
  %r = call i8 asm "", "={dp},{dp}"(i8 %a)
  ret i8 %r
}

; CHECK: error: could not allocate output register for constraint '{v}'
define i16 @v(i16 %a) {
  %r = call i16 asm "", "={v},{v}"(i16 %a)
  ret i16 %r
}

; M6809: error: could not allocate output register for constraint '{w}'
; H6309-NOT: constraint '{w}'
define i16 @w(i16 %a) {
  %r = call i16 asm "", "={w},{w}"(i16 %a)
  ret i16 %r
}

; CHECK-NOT: error
define void @clobbers() {
  call void asm sideeffect "", "~{cc},~{dp},~{e},~{f},~{v}"()
  ret void
}
