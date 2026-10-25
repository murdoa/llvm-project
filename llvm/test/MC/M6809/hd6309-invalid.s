; RUN: not llvm-mc -triple=m6809 %s -o /dev/null 2>&1 | FileCheck %s
; RUN: not llvm-mc -triple=m6809 -mcpu=6309 -filetype=obj %s -o /dev/null 2>&1 \
; RUN:   | FileCheck --check-prefix=H6309 %s

; Without -mcpu=6309 the HD6309 instructions and registers do not exist.

	ldw #1
; CHECK: :[[@LINE-1]]:2: error: instruction requires -mcpu=6309
	tfr w,x
; CHECK: :[[@LINE-1]]:6: error: expected a register
	lda ,w
; CHECK: :[[@LINE-1]]:7: error: expected x, y, u or s

; Modes the HD6309 does not have.
	lda 5,w+
; H6309: :[[@LINE-1]]:9: error: unexpected token after operand
	lda ,w+
; H6309: :[[@LINE-1]]:2: error: W takes only ,w  n,w  ,w++ and ,--w
	tfm x+,y-
; H6309: :[[@LINE-1]]:6: error: tfm takes r+,r+  r-,r-  r+,r  or r,r+
	tfm a+,b+
; H6309: :[[@LINE-1]]:6: error: tfm takes d, x, y, u or s
