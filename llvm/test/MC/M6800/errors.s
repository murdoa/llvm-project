; RUN: not llvm-mc -triple=m6800 %s 2>&1 | FileCheck %s

; CHECK: :[[@LINE+1]]:3: error: unknown instruction
  ldy #1
; CHECK: :[[@LINE+1]]:8: error: instruction has no immediate form
  staa #1
; CHECK: :[[@LINE+1]]:7: error: instruction has no direct form
  inc <$40
; CHECK: :[[@LINE+1]]:10: error: expected x
  ldaa 5,y
; CHECK: :[[@LINE+1]]:8: error: '<' and '>' need an address, not an index
  ldaa <5,x
; CHECK: :[[@LINE+1]]:7: error: expected '#'
  aim $0f,$40
; CHECK: :[[@LINE+1]]:12: error: instruction has no extended form
  aim #$0f,>$40
; CHECK: :[[@LINE+1]]:7: error: unexpected token after operand
  inx 1
