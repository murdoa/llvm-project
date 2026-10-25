; RUN: not llc -mtriple=m6800 < %s 2>&1 | FileCheck %s

; CC does not copy to and from the allocatable registers: as an asm
; operand it is an error.  As a clobber it stands.

; CHECK: error: could not allocate output register for constraint '{cc}'
define i8 @cc(i8 %a) {
  %r = call i8 asm "", "={cc},{cc}"(i8 %a)
  ret i8 %r
}

; CHECK-NOT: error
define void @clobbers() {
  call void asm sideeffect "", "~{cc}"()
  ret void
}
