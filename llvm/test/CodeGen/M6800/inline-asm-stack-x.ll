; RUN: not llc -mtriple=m6800 < %s 2>&1 | FileCheck %s

; A stack slot operand is reached through X, so the asm cannot use X too.

; CHECK: error: inline asm with a stack slot operand cannot also use X
define void @uses_x() {
  %v = alloca i16
  call void asm sideeffect "clr $0", "*m,~{x}"(ptr elementtype(i16) %v)
  ret void
}
