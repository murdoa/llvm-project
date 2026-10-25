; RUN: not llvm-mc -triple=m6809 -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s

	foo
; CHECK: :[[@LINE-1]]:2: error: unknown instruction
	lda ,a
; CHECK: :[[@LINE-1]]:7: error: expected x, y, u or s
	lda ,-pc
; CHECK: :[[@LINE-1]]:8: error: expected x, y, u or s
	lda [5,x
; CHECK: :[[@LINE-1]]:10: error: expected ']'
	lda <<5
; CHECK: :[[@LINE-1]]:9: error: '<<' needs an index register
	tfr a,x
; CHECK: :[[@LINE-1]]:8: error: registers must be the same size
	pshs s
; CHECK: :[[@LINE-1]]:7: error: expected a register that can be pushed
	pulu u
; CHECK: :[[@LINE-1]]:7: error: cannot push or pull u here
	sta #1
; CHECK: :[[@LINE-1]]:6: error: instruction has no immediate form
	nop 1
; CHECK: :[[@LINE-1]]:6: error: unexpected token after operand
	lda 1,x 2
; CHECK: :[[@LINE-1]]:10: error: unexpected token after operand
	leax label
; CHECK: :[[@LINE-1]]:2: error: instruction needs an indexed address
	lda [,x+]
; CHECK: :[[@LINE-1]]:2: error: single increment or decrement cannot be indirect
	lda <<16,x
; CHECK: :[[@LINE-1]]:2: error: offset does not fit in 5 bits
	ldb #300
; CHECK: :[[@LINE-1]]:2: error: value does not fit in 8 bits
